/* =========================================================
 * right_panel.cpp — system stats + app launcher + settings
 *
 * Sections (top → bottom):
 *   PAINTED:  header, CPU, RAM, DISK, TOP PROCESSES, QUICK KEYS
 *   DOCK:     APPS (search + scrollable list), PANELS, SETTINGS,
 *             COLOR THEME
 *
 * The entire dock sits inside a QScrollArea so every section is
 * reachable regardless of panel height — matches Python behaviour.
 * ========================================================= */
#include "right_panel.h"
#include "main_panel.h"
#include "cyberdeck.h"

#include <QPainter>
#include <QFont>
#include <QFontMetrics>
#include <QPen>
#include <QFile>
#include <QDir>
#include <QDirIterator>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QProcess>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollBar>
#include <QCoreApplication>
#include <QRegularExpression>
#include <QTextStream>
#include <QStandardPaths>
#include <sys/statvfs.h>
#include <unistd.h>
#include <csignal>

#include <X11/Xlib.h>
#include <X11/Xatom.h>

#undef KeyPress
#undef FocusOut

/* ── colours (One Dark) ──────────────────────────────────── */
static const QColor CYAN (97,  175, 239);
static const QColor GREEN(152, 195, 121);
static const QColor AMBER(229, 192, 123);
static const QColor RED  (224, 108, 117);
static const QColor DIM  (62,  68,  81);
static const QColor WHITE(171, 178, 191);

static QString cfgDir() {
    return QDir::homePath() + "/.config/animated-wallpaper";
}

/* ── .desktop field-code stripping ──────────────────────── */
static const QSet<QString> FIELD_CODES = {
    "%f","%F","%u","%U","%d","%D","%n","%N",
    "%i","%c","%k","%v","%m"
};

static QString cleanExec(const QString &execLine) {
    /* Very small shell-like split: honour single/double quotes */
    QStringList parts;
    QString cur;
    bool inSingle = false, inDouble = false;
    for (int i = 0; i < execLine.length(); ++i) {
        QChar c = execLine[i];
        if (c == '\'' && !inDouble) { inSingle = !inSingle; continue; }
        if (c == '"'  && !inSingle) { inDouble = !inDouble; continue; }
        if (c == ' ' && !inSingle && !inDouble) {
            if (!cur.isEmpty()) { parts << cur; cur.clear(); }
        } else {
            cur += c;
        }
    }
    if (!cur.isEmpty()) parts << cur;

    QStringList out;
    for (const QString &p : parts)
        if (!FIELD_CODES.contains(p.toLower()))
            out << p;
    return out.join(' ');
}

/* ── scan all .desktop files for visible GUI apps ───────── */
struct AppEntry { QString name, cmd; };

static QList<AppEntry> scanDesktopApps() {
    static const QStringList DIRS = {
        "/usr/share/applications",
        "/usr/local/share/applications",
        "/var/lib/flatpak/exports/share/applications",
        QDir::homePath() + "/.local/share/flatpak/exports/share/applications",
        QDir::homePath() + "/.local/share/applications",
    };

    QMap<QString, QString> seen; /* name → cmd */

    for (const QString &dir : DIRS) {
        QDirIterator it(dir, {"*.desktop"}, QDir::Files, QDirIterator::NoIteratorFlags);
        QStringList files;
        while (it.hasNext()) files << it.next();
        files.sort();

        for (const QString &path : files) {
            QFile f(path);
            if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;

            QString name, exec;
            bool inMain = false, noDisplay = false;

            QTextStream ts(&f);
            while (!ts.atEnd()) {
                QString line = ts.readLine().trimmed();

                if (line == "[Desktop Entry]") { inMain = true; continue; }
                if (line.startsWith('[') && line != "[Desktop Entry]") break;
                if (!inMain) continue;

                if (line.startsWith("Name=") && name.isEmpty())
                    name = line.mid(5).trimmed();
                else if (line.startsWith("Exec="))
                    exec = line.mid(5).trimmed();
                else if (line == "NoDisplay=true" || line == "Hidden=true")
                    noDisplay = true;
                else if (line.startsWith("Type=") &&
                         line.mid(5).trimmed() != "Application")
                    noDisplay = true;
            }

            if (noDisplay || name.isEmpty() || exec.isEmpty()) continue;
            QString cmd = cleanExec(exec);
            if (!cmd.isEmpty() && !seen.contains(name))
                seen[name] = cmd;
        }
    }

    /* Sort by name, case-insensitive */
    QList<AppEntry> result;
    QList<QString> names = seen.keys();
    std::sort(names.begin(), names.end(),
              [](const QString &a, const QString &b){
                  return a.toLower() < b.toLower(); });
    for (const QString &n : names)
        result.append({n, seen[n]});
    return result;
}

/* ── pinned apps from apps.json ──────────────────────────── */
static const QList<AppEntry> DEFAULT_APPS = {
    {"Terminal", "ghostty"},
    {"Browser",  "zen-browser"},
    {"Files",    "nautilus"},
};

static QList<AppEntry> loadPinned() {
    QString path = cfgDir() + "/apps.json";
    QFile f(path);
    if (f.open(QIODevice::ReadOnly)) {
        auto doc = QJsonDocument::fromJson(f.readAll());
        QList<AppEntry> out;
        for (const auto &v : doc.array()) {
            QJsonObject o = v.toObject();
            QString n = o["name"].toString();
            QString c = o["cmd"].toString();
            if (!n.isEmpty() && !c.isEmpty())
                out.append({n, c});
        }
        if (!out.isEmpty()) return out;
    }
    /* Write defaults */
    QDir().mkpath(cfgDir());
    if (f.open(QIODevice::WriteOnly)) {
        QJsonArray arr;
        for (const auto &a : DEFAULT_APPS) {
            QJsonObject o;
            o["name"] = a.name;
            o["cmd"]  = a.cmd;
            arr.append(o);
        }
        f.write(QJsonDocument(arr).toJson());
    }
    return DEFAULT_APPS;
}

/* Pinned first, then every other installed GUI app */
static QList<AppEntry> loadApps() {
    QList<AppEntry> pinned = loadPinned();
    QSet<QString> pinnedNames;
    for (const auto &a : pinned) pinnedNames.insert(a.name);

    QList<AppEntry> all = pinned;
    for (const auto &a : scanDesktopApps())
        if (!pinnedNames.contains(a.name))
            all.append(a);
    return all;
}

/* ── system stats helpers ────────────────────────────────── */
static double cpuPercent() {
    static long long prevTotal = 0, prevIdle = 0;
    QFile f("/proc/stat");
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return 0;
    QString line = QString::fromLocal8Bit(f.readLine());
    QStringList p = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    if (p.size() < 8) return 0;
    long long total = 0;
    for (int i = 1; i < 8; ++i) total += p[i].toLongLong();
    long long idle = p[4].toLongLong();
    long long dt = total - prevTotal, di = idle - prevIdle;
    prevTotal = total; prevIdle = idle;
    return dt ? 100.0 * (1.0 - (double)di / dt) : 0;
}

static void memInfo(double &usedGB, double &totalGB) {
    usedGB = 0; totalGB = 1;
    QFile f("/proc/meminfo");
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QMap<QString, long> info;
    while (!f.atEnd()) {
        QString l = f.readLine();
        auto kv = l.split(':');
        if (kv.size() == 2)
            info[kv[0].trimmed()] = kv[1].trimmed().split(' ')[0].toLong();
    }
    /* MemAvailable is what the kernel considers truly free (incl. reclaimable
     * cache/buffers). Used = Total - Available gives the real working-set. */
    totalGB = info["MemTotal"]                            / 1024.0 / 1024.0;
    usedGB  = (info["MemTotal"] - info["MemAvailable"]) / 1024.0 / 1024.0;
}

static void diskInfo(int &usedGB, int &totalGB, int &pct) {
    usedGB = totalGB = 0; pct = 0;
    struct statvfs st;
    if (statvfs("/", &st) != 0) return;
    totalGB = (int)(((unsigned long long)st.f_blocks * st.f_frsize) / (1024ULL*1024*1024));
    usedGB  = (int)((((unsigned long long)(st.f_blocks - st.f_bavail)) * st.f_frsize)
                    / (1024ULL*1024*1024));
    pct = (st.f_blocks > 0)
        ? (int)(100.0 * (1.0 - (double)st.f_bavail / st.f_blocks))
        : 0;
}

static int cpuTemp() {
    for (int hw = 0; hw < 8; ++hw)
        for (int t = 1; t < 5; ++t) {
            QFile f(QString("/sys/class/hwmon/hwmon%1/temp%2_input").arg(hw).arg(t));
            if (f.open(QIODevice::ReadOnly)) {
                int val = f.readAll().trimmed().toInt() / 1000;
                if (val > 20 && val < 120) return val;
            }
        }
    return 0;
}

static QList<QPair<QString, QPair<double,double>>> topProcs(int n = 10) {
    QList<QPair<QString, QPair<double,double>>> result;
    QProcess proc;
    proc.start("ps", {"aux", "--sort=-%cpu"});
    proc.waitForFinished(2000);
    QStringList lines = QString::fromLocal8Bit(proc.readAllStandardOutput()).split('\n');
    for (int i = 1; i < qMin(lines.size(), n + 1); ++i) {
        QStringList p = lines[i].split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (p.size() >= 11)
            result.append({p[10].left(18), {p[2].toDouble(), p[3].toDouble()}});
    }
    return result;
}

/* ── stylesheet helpers ──────────────────────────────────── */
static QString btnQss(const QString &color = "#c8dcff") {
    return QString(
        "QPushButton { color: %1; background: rgba(62,68,81,150); "
        "text-align: left; border: 1px solid #3e4451; border-radius: 3px; "
        "font: bold 9pt 'JetBrains Mono'; padding: 4px 10px; } "
        "QPushButton:hover { background: rgba(97,175,239,210); "
        "border-color: #61afef; }"
    ).arg(color);
}

static const char *LBL_QSS =
    "color: #98c379; font: bold 9pt 'JetBrains Mono'; padding-top: 4px;";

/* =========================================================
 * Constructor
 * ========================================================= */
RightPanel::RightPanel(QWidget *parent)
    : QWidget(parent), m_pipes(this)
{
    setAttribute(Qt::WA_TranslucentBackground);

    connect(&m_statsTimer, &QTimer::timeout, this, &RightPanel::updateStats);
    m_statsTimer.start(2500);
    updateStats();

    buildDock();

    QFileInfo fi(cfgDir() + "/apps.json");
    m_appsMtime = fi.lastModified().toSecsSinceEpoch();

    connect(&m_appsTimer, &QTimer::timeout, this, &RightPanel::checkAppsChanged);
    m_appsTimer.start(3000);

    /* placeDock needs the real painted stats height, which is only known
     * after the first paintEvent. Defer it so the dock doesn't overlap RAM/DISK. */
    QTimer::singleShot(50, this, [this]() { update(); });
    QTimer::singleShot(150, this, [this]() { placeDock(); });
}

/* =========================================================
 * buildDock — builds the entire scrollable dock widget
 *
 * Layout inside the QScrollArea:
 *   ── APPS ──     (header + search bar)
 *   [app list]     (inner QScrollArea, 8 rows visible)
 *   ── PANELS ──
 *   [Browser] [FM] [Terminal]
 *   ── SETTINGS ──
 *   ... buttons ...
 *   Color theme:
 *   [Theme A] [Theme B]  (pairs)
 * ========================================================= */
void RightPanel::buildDock() {
    /* Tear down any previous dock */
    if (m_dockScroll) {
        m_dockScroll->deleteLater();
        m_dockScroll = nullptr;
        m_dock       = nullptr;
        m_dockLayout = nullptr;
        m_appSearch  = nullptr;
        m_appScroll  = nullptr;
        m_redshiftBtn = nullptr;
        m_glavaBtn    = nullptr;
        m_appButtons.clear();
        m_themeBtns.clear();
    }

    /* ── Outer scroll area ─────────────────────────────── */
    m_dockScroll = new QScrollArea(this);
    m_dockScroll->setFrameShape(QScrollArea::NoFrame);
    m_dockScroll->setWidgetResizable(true);
    m_dockScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_dockScroll->setStyleSheet(
        "QScrollArea { background: transparent; border: none; }"
        "QScrollBar:vertical { background: rgba(62,68,81,80); width: 4px; border-radius: 2px; }"
        "QScrollBar::handle:vertical { background: rgba(97,175,239,160); border-radius: 2px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }");
    m_dockScroll->viewport()->setStyleSheet("background: transparent;");

    /* ── Inner content widget ──────────────────────────── */
    m_dock = new QWidget();
    m_dock->setAttribute(Qt::WA_TranslucentBackground);
    m_dock->setStyleSheet("background: transparent;");
    m_dockScroll->setWidget(m_dock);

    m_dockLayout = new QVBoxLayout(m_dock);
    m_dockLayout->setContentsMargins(0, 4, 4, 8);
    m_dockLayout->setSpacing(4);

    /* ── helpers ───────────────────────────────────────── */
    auto addLabel = [&](const QString &text) {
        auto *l = new QLabel(text, m_dock);
        l->setStyleSheet(LBL_QSS);
        m_dockLayout->addWidget(l);
    };

    auto makeBtn = [&](const QString &text, QWidget *parent,
                       const QString &color = "#c8dcff") -> QPushButton * {
        auto *b = new QPushButton(text, parent ? parent : m_dock);
        b->setCursor(Qt::PointingHandCursor);
        b->setStyleSheet(btnQss(color));
        return b;
    };

    /* ── APPS header + search ──────────────────────────── */
    auto *head = new QWidget(m_dock);
    head->setAttribute(Qt::WA_TranslucentBackground);
    auto *hl = new QHBoxLayout(head);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(6);

    auto *appsLbl = new QLabel("── APPS ──", head);
    appsLbl->setStyleSheet(LBL_QSS);
    hl->addWidget(appsLbl);

    m_appSearch = new QLineEdit(head);
    m_appSearch->setPlaceholderText("🔍 search apps…");
    m_appSearch->setStyleSheet(
        "QLineEdit { color: #abb2bf; background: rgba(62,68,81,150); "
        "border: 1px solid #3e4451; border-radius: 3px; "
        "font: 9pt 'JetBrains Mono'; padding: 3px 8px; }"
        "QLineEdit:focus { border-color: #61afef; }");
    connect(m_appSearch, &QLineEdit::textChanged, this, &RightPanel::filterApps);
    m_appSearch->installEventFilter(this);
    hl->addWidget(m_appSearch, 1);
    m_dockLayout->addWidget(head);

    /* ── App list (inner scrollable) ───────────────────── */
    auto *appsHolder = new QWidget();
    appsHolder->setAutoFillBackground(false);
    appsHolder->setStyleSheet("background: transparent;");
    auto *appsLay = new QVBoxLayout(appsHolder);
    appsLay->setContentsMargins(0, 0, 4, 0);
    appsLay->setSpacing(4);

    for (const AppEntry &app : loadApps()) {
        auto *b = new QPushButton(QString("▸ %1").arg(app.name), appsHolder);
        b->setCursor(Qt::PointingHandCursor);
        b->setStyleSheet(btnQss());
        QString cmd = app.cmd;
        connect(b, &QPushButton::clicked, [this, cmd]() { launch(cmd); });
        appsLay->addWidget(b);
        m_appButtons.append({b, app.name.toLower()});
    }
    appsLay->addStretch(1);

    /* Height: 8 visible rows */
    const int VISIBLE = 8;
    int btnH = m_appButtons.isEmpty()
               ? 26
               : m_appButtons.first().first->sizeHint().height();
    int listH = VISIBLE * btnH + (VISIBLE - 1) * appsLay->spacing();

    m_appScroll = new QScrollArea(m_dock);
    m_appScroll->setWidget(appsHolder);
    m_appScroll->setWidgetResizable(true);
    m_appScroll->setFixedHeight(listH);
    m_appScroll->setFrameShape(QScrollArea::NoFrame);
    m_appScroll->setStyleSheet("QScrollArea { background: transparent; border: none; }");
    m_appScroll->viewport()->setStyleSheet("background: transparent;");
    m_appScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_dockLayout->addWidget(m_appScroll);

    /* ── PANELS ────────────────────────────────────────── */
    addLabel("── PANELS ──");

    struct PanelDef { const char *id; const char *label; const char *color; };
    static const PanelDef PANELS[] = {
        { "browser",  "🌐 Browser", "#61afef" },
        { "fm",       "📁 FM",      "#98c379" },
        { "terminal", "⬛ Term",    "#abb2bf" },
    };

    auto *panelRow = new QWidget(m_dock);
    panelRow->setAttribute(Qt::WA_TranslucentBackground);
    auto *pr = new QHBoxLayout(panelRow);
    pr->setContentsMargins(0, 0, 0, 0);
    pr->setSpacing(4);

    for (const auto &pd : PANELS) {
        auto *b = new QPushButton(QString::fromUtf8(pd.label), panelRow);
        b->setCursor(Qt::PointingHandCursor);
        QString c = QString::fromUtf8(pd.color);
        b->setStyleSheet(QString(
            "QPushButton { color:%1; background:rgba(62,68,81,150); "
            "border:1px solid #3e4451; border-radius:3px; "
            "font:bold 8pt 'JetBrains Mono'; padding:3px 6px; } "
            "QPushButton:hover { background:rgba(97,175,239,200); "
            "color:#1e2228; border-color:#61afef; }").arg(c));
        QString tid = QString::fromUtf8(pd.id);
        connect(b, &QPushButton::clicked, [this, tid]() { openPanel(tid); });
        pr->addWidget(b);
    }
    m_dockLayout->addWidget(panelRow);

    /* ── SETTINGS ──────────────────────────────────────── */
    addLabel("── SETTINGS ──");

    struct BtnDef { const char *label; const char *color; };
    auto *editGraphBtn = makeBtn("  Edit graph", m_dock);
    connect(editGraphBtn, &QPushButton::clicked, [this]() {
        launch(QDir(QCoreApplication::applicationDirPath())
               .absoluteFilePath("../../graph-edit.sh"));
    });
    m_dockLayout->addWidget(editGraphBtn);

    auto *audioBtn = makeBtn("  Audio mixer", m_dock);
    connect(audioBtn, &QPushButton::clicked, [this]() { launch("pavucontrol"); });
    m_dockLayout->addWidget(audioBtn);

    auto *netBtn = makeBtn("  Choose network…", m_dock);
    connect(netBtn, &QPushButton::clicked, [this]() { chooseNetwork(); });
    m_dockLayout->addWidget(netBtn);

    auto *wifiBtn = makeBtn("  Wifi on/off", m_dock);
    connect(wifiBtn, &QPushButton::clicked, [this]() { toggleWifi(); });
    m_dockLayout->addWidget(wifiBtn);

    auto *muteBtn = makeBtn("  Mute on/off", m_dock);
    connect(muteBtn, &QPushButton::clicked, [this]() {
        launch("pactl set-sink-mute @DEFAULT_SINK@ toggle");
    });
    m_dockLayout->addWidget(muteBtn);

    m_redshiftBtn = makeBtn("", m_dock, "#e5c07b");
    connect(m_redshiftBtn, &QPushButton::clicked, [this]() { toggleRedshift(); });
    m_dockLayout->addWidget(m_redshiftBtn);
    updateRedshiftLabel();

    m_glavaBtn = makeBtn("", m_dock, "#c678dd");
    connect(m_glavaBtn, &QPushButton::clicked, [this]() { toggleGlava(); });
    m_dockLayout->addWidget(m_glavaBtn);
    updateGlavaLabel();

    auto *restartBtn = makeBtn("  Restart deck", m_dock, "#e5c07b");
    connect(restartBtn, &QPushButton::clicked, [this]() { restartDeck(); });
    m_dockLayout->addWidget(restartBtn);

    /* ── COLOR THEME ───────────────────────────────────── */
    auto *themeLbl = new QLabel("  Color theme:", m_dock);
    themeLbl->setStyleSheet(
        "color: #abb2bf; font: 9pt 'JetBrains Mono'; padding-top: 4px;");
    m_dockLayout->addWidget(themeLbl);

    /*
     * Built-in One Dark–derived presets.
     * Each entry: { display name, accent QColor }
     * Switching a theme emits switchTheme(name) which can be expanded
     * to recolour the whole deck once a theme system is wired in.
     */
    struct ThemePreset { const char *name; QColor accent; };
    static const ThemePreset PRESETS[] = {
        { "Dark (default)", QColor(97,  175, 239) },   /* blue   */
        { "Dracula",        QColor(189, 147, 249) },   /* purple */
        { "Gruvbox",        QColor(250, 189, 47)  },   /* yellow */
        { "Nord",           QColor(136, 192, 208) },   /* frost  */
        { "Solarized",      QColor(38,  139, 210) },   /* blue   */
        { "Monokai",        QColor(166, 226, 46)  },   /* green  */
    };

    int nPresets = (int)(sizeof(PRESETS) / sizeof(PRESETS[0]));
    for (int i = 0; i < nPresets; i += 2) {
        auto *rowW = new QWidget(m_dock);
        rowW->setAttribute(Qt::WA_TranslucentBackground);
        auto *rowL = new QHBoxLayout(rowW);
        rowL->setContentsMargins(0, 0, 0, 0);
        rowL->setSpacing(4);

        for (int j = i; j < qMin(i + 2, nPresets); ++j) {
            const ThemePreset &tp = PRESETS[j];
            int cr = tp.accent.red(), cg = tp.accent.green(), cb = tp.accent.blue();
            auto *b = new QPushButton(QString::fromUtf8(tp.name), rowW);
            b->setCursor(Qt::PointingHandCursor);
            b->setCheckable(true);
            b->setChecked(QString::fromUtf8(tp.name) == m_currentTheme);
            b->setStyleSheet(QString(
                "QPushButton { color:rgb(%1,%2,%3); "
                "background:rgba(62,68,81,150); "
                "border:1px solid rgba(%1,%2,%3,100); "
                "border-radius:3px; font:8pt 'JetBrains Mono'; "
                "padding:3px 6px; text-align:left; } "
                "QPushButton:hover, QPushButton:checked { "
                "background:rgba(%1,%2,%3,180); "
                "color:#1e2228; border-color:rgb(%1,%2,%3); }")
                .arg(cr).arg(cg).arg(cb));
            QString tname = QString::fromUtf8(tp.name);
            connect(b, &QPushButton::clicked, [this, tname]() {
                switchTheme(tname);
            });
            rowL->addWidget(b);
            m_themeBtns[tname] = b;
        }
        m_dockLayout->addWidget(rowW);
    }

    m_dockLayout->addStretch(1);
    /* dock is shown/placed by placeDock() called from resizeEvent */
}

/* =========================================================
 * placeDock — position the outer QScrollArea below painted stats
 * ========================================================= */
void RightPanel::placeDock() {
    if (!m_dockScroll) return;
    int dw  = width() - 12;
    int top = m_statsBottom + 10;
    int h   = qMax(100, height() - top - 4);
    m_dockScroll->setGeometry(12, top, dw, h);
}

/* =========================================================
 * resizeEvent
 * ========================================================= */
void RightPanel::resizeEvent(QResizeEvent *e) {
    QWidget::resizeEvent(e);
    placeDock();
}

/* =========================================================
 * eventFilter — keyboard/focus for the app search box
 *
 * The deck is an override-redirect window: the WM never assigns
 * keyboard focus to it automatically, so we grab it via Xlib when
 * the search box is clicked and release it on Escape / FocusOut.
 * ========================================================= */
bool RightPanel::eventFilter(QObject *obj, QEvent *e) {
    if (obj == m_appSearch) {
        if (e->type() == QEvent::MouseButtonPress) {
            grabXFocus();
            m_appSearch->setFocus(Qt::MouseFocusReason);
        } else if (e->type() == QEvent::KeyPress) {
            auto *ke = static_cast<QKeyEvent *>(e);
            if (ke->key() == Qt::Key_Escape) {
                m_appSearch->clearFocus();
                releaseXFocus();
                return true;
            }
        } else if (e->type() == QEvent::FocusOut) {
            releaseXFocus();
        }
    }
    return QWidget::eventFilter(obj, e);
}

void RightPanel::grabXFocus() {
    Display *d = XOpenDisplay(nullptr);
    if (!d) return;
    XSetInputFocus(d, (::Window)winId(), RevertToPointerRoot, CurrentTime);
    XFlush(d);
    XCloseDisplay(d);
}

void RightPanel::releaseXFocus() {
    Display *d = XOpenDisplay(nullptr);
    if (!d) return;
    XSetInputFocus(d, PointerRoot, RevertToPointerRoot, CurrentTime);
    XFlush(d);
    XCloseDisplay(d);
}

/* =========================================================
 * filterApps — live search through the app list
 * ========================================================= */
void RightPanel::filterApps(const QString &text) {
    QString needle = text.trimmed().toLower();
    for (auto &[btn, nameLower] : m_appButtons)
        btn->setVisible(needle.isEmpty() || nameLower.contains(needle));
    if (m_appScroll)
        m_appScroll->verticalScrollBar()->setValue(0);
}

/* =========================================================
 * launch — fire-and-forget command via /bin/sh
 * ========================================================= */
void RightPanel::launch(const QString &cmd) {
    if (!cmd.isEmpty())
        QProcess::startDetached("/bin/sh", {"-c", cmd});
}

/* =========================================================
 * openPanel — ask the main panel to switch to a tab
 * ========================================================= */
void RightPanel::openPanel(const QString &tid) {
    /* Ask MainPanel to switch to the embedded slot (and auto-launch if needed) */
    QWidget *top = window();
    if (!top) return;
    MainPanel *mp = top->findChild<MainPanel *>();
    if (mp) { mp->showPanel(tid); return; }

    /* Fallback: MainPanel not found — launch directly */
    if (tid == "browser") {
        for (const char *exe : {"SwordFish", "zen-browser", "firefox",
                                "chromium", "google-chrome-stable"}) {
            QString p = QStandardPaths::findExecutable(QString::fromUtf8(exe));
            if (!p.isEmpty()) { QProcess::startDetached(p, {}); return; }
        }
    } else if (tid == "fm") {
        for (const char *exe : {"swordfm", "nautilus", "thunar", "pcmanfm"}) {
            QString p = QStandardPaths::findExecutable(QString::fromUtf8(exe));
            if (!p.isEmpty()) { QProcess::startDetached(p, {}); return; }
        }
    } else if (tid == "terminal") {
        for (const char *exe : {"ghostty", "alacritty", "kitty", "xterm"}) {
            QString p = QStandardPaths::findExecutable(QString::fromUtf8(exe));
            if (!p.isEmpty()) { QProcess::startDetached(p, {}); return; }
        }
    }
}

/* =========================================================
 * switchTheme — toggle checked state; theme wiring placeholder
 * ========================================================= */
void RightPanel::switchTheme(const QString &name) {
    m_currentTheme = name;
    for (auto it = m_themeBtns.begin(); it != m_themeBtns.end(); ++it)
        it.value()->setChecked(it.key() == name);
    update();
    /* Future: walk up to CyberDeck and broadcast palette change */
}

/* =========================================================
 * Wifi / network
 * ========================================================= */
void RightPanel::toggleWifi() {
    launch("state=$(nmcli radio wifi); "
           "if [ \"$state\" = \"enabled\" ]; then nmcli radio wifi off; "
           "else nmcli radio wifi on; fi");
}

void RightPanel::chooseNetwork() {
    launch(R"(
sel=$( { nmcli -t -f NAME,TYPE connection show 2>/dev/null \
           | awk -F: '{printf "%s  [%s]\n", $1, $2}';
         nmcli -t -f SSID,SIGNAL dev wifi list 2>/dev/null \
           | awk -F: '$1!="" {printf "%s  (wifi %s%%)\n", $1, $2}'; } \
       | sort -u \
       | rofi -dmenu -p "Connect to:" -i )
[ -z "$sel" ] && exit 0
name=$(echo "$sel" | sed -E 's/  \[[^]]*\]$//; s/  \(wifi[^)]*\)$//')
if nmcli -t -f NAME connection show | grep -Fxq "$name"; then
    nmcli connection up id "$name"
else
    pass=$(rofi -dmenu -p "Password (blank = open network):" -password </dev/null)
    if [ -z "$pass" ]; then
        nmcli device wifi connect "$name"
    else
        nmcli device wifi connect "$name" password "$pass"
    fi
fi
)");
}

/* =========================================================
 * Redshift toggle (reading mode / normal)
 * ========================================================= */
void RightPanel::updateRedshiftLabel() {
    if (!m_redshiftBtn) return;
    bool on = QFile::exists(cfgDir() + "/redshift.on");
    m_redshiftBtn->setText(on ? "☀ Normal colors" : "  Reading mode (5000K)");
}

void RightPanel::toggleRedshift() {
    QString flag = cfgDir() + "/redshift.on";
    if (QFile::exists(flag)) {
        launch("redshift -x");
        QFile::remove(flag);
    } else {
        launch("redshift -O 5000");
        QFile f(flag);
        (void)f.open(QIODevice::WriteOnly);
    }
    updateRedshiftLabel();
}

/* =========================================================
 * Glava toggle (audio visualizer)
 * ========================================================= */
void RightPanel::updateGlavaLabel() {
    if (!m_glavaBtn) return;
    QString pidFile = cfgDir() + "/glava.pid";
    if (QFile::exists(pidFile)) {
        QFile f(pidFile);
        if (f.open(QIODevice::ReadOnly)) {
            int pid = f.readAll().trimmed().toInt();
            if (pid > 0 && kill(pid, 0) == 0) {
                m_glavaBtn->setText("  Audio Visualizer: ON");
                return;
            }
        }
    }
    m_glavaBtn->setText("  Audio Visualizer: OFF");
}

void RightPanel::toggleGlava() {
    /* Binary lives at SwordWM/cpp-sworddeck/build/sworddeck,
     * script lives at SwordWM/cyberdesk.sh — two levels up. */
    QString script = QDir(QCoreApplication::applicationDirPath())
                         .absoluteFilePath("../../cyberdesk.sh");
    QProcess::startDetached("/bin/sh", {"-c", script + " glava-toggle"});
    QTimer::singleShot(500, this, &RightPanel::updateGlavaLabel);
}

void RightPanel::restartDeck() {
    QString script = QDir(QCoreApplication::applicationDirPath())
                         .absoluteFilePath("../../cyberdesk.sh");
    QProcess::startDetached("/bin/sh", {"-c", script + " restart"});
}

/* =========================================================
 * checkAppsChanged — reload dock if apps.json was modified
 * ========================================================= */
void RightPanel::checkAppsChanged() {
    QFileInfo fi(cfgDir() + "/apps.json");
    qint64 mt = fi.lastModified().toSecsSinceEpoch();
    if (mt != m_appsMtime) {
        m_appsMtime = mt;
        buildDock();
        m_dockScroll->show();
        placeDock();
    }
}

/* =========================================================
 * updateStats — called every 2500 ms
 * ========================================================= */
void RightPanel::updateStats() {
    m_cpu = cpuPercent();
    memInfo(m_memUsed, m_memTotal);
    m_temp    = cpuTemp();
    m_hasTemp = (m_temp > 0);
    ++m_statsCounter;
    if (m_statsCounter % 2 == 0)
        m_procs = topProcs(10);
    update();
}

/* =========================================================
 * Paint helpers
 * ========================================================= */
void RightPanel::drawBar(QPainter &p, int x, int y, int bw, int bh,
                          double pct, const QColor &color)
{
    p.setPen(QPen(DIM, 1));
    p.drawRect(x, y, bw, bh);
    int fill = (int)(bw * qMin(pct, 100.0) / 100.0);
    int segW = 4, segGap = 1, segX = x + 1;
    while (segX < x + 1 + fill - segGap) {
        int w = qMin(segW, x + 1 + fill - segGap - segX);
        p.fillRect(segX, y + 1, w, bh - 1, color);
        segX += segW + segGap;
    }
}

/* Returns y after the label (caller advances further) */
int RightPanel::drawSection(QPainter &p, int x, int y, int w,
                              const QString &label)
{
    p.setPen(QPen(DIM, 1));
    p.drawLine(x, y, x + w - 8, y);
    QFont f("JetBrains Mono", 9, QFont::Bold);
    p.setFont(f);
    p.setPen(GREEN);
    p.drawText(x, y - 2, "── " + label + " ──");
    return y + 14;
}

QColor RightPanel::valColor(double pct) const {
    if (pct >= 80) return RED;
    if (pct >= 50) return AMBER;
    return GREEN;
}

/* =========================================================
 * paintEvent
 * ========================================================= */
void RightPanel::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    int W = width();

    /* Animated pipes texture (shared with left panel) */
    m_pipes.paint(p);

    int  x  = 12;
    int  bw = W - 24;
    int  y  = 10;
    QFont sm ("JetBrains Mono", 9);
    QFont smb("JetBrains Mono", 9, QFont::Bold);

    /* ── Header ──────────────────────────────────────────── */
    QFont hf("JetBrains Mono", 16, QFont::Bold);
    p.setFont(hf);
    p.setPen(CYAN);
    QFontMetrics hfm(hf);
    const QString title = "▸ SWORDDECK";
    p.drawText((W - hfm.horizontalAdvance(title)) / 2, y + 20, title);
    y += 34;
    p.setPen(QPen(DIM, 1));
    p.drawLine(x, y, x + bw, y);
    y += 10;

    /* ── SYSTEM ──────────────────────────────────────────── */
    y = drawSection(p, x, y, W, "SYSTEM");

    /* CPU */
    QColor cpuC = valColor(m_cpu);
    p.setFont(smb); p.setPen(WHITE);
    p.drawText(x, y + 12, "CPU");
    p.setFont(sm); p.setPen(cpuC);
    p.drawText(x + 35, y + 12,
               QString("%1%").arg(m_cpu, 0, 'f', 0));
    if (m_hasTemp) {
        p.setPen(AMBER);
        p.drawText(x + 80, y + 12, QString("  %1°C").arg(m_temp));
    }
    drawBar(p, x, y + 15, bw, 7, m_cpu, cpuC);
    y += 28;

    /* RAM */
    double memPct = 100.0 * m_memUsed / qMax(0.001, m_memTotal);
    QColor memC   = valColor(memPct);
    p.setFont(smb); p.setPen(WHITE);
    p.drawText(x, y + 12, "RAM");
    p.setFont(sm); p.setPen(memC);
    p.drawText(x + 35, y + 12,
               QString("%1G / %2G  %3%")
               .arg(m_memUsed, 0, 'f', 1)
               .arg(m_memTotal, 0, 'f', 1)
               .arg(memPct, 0, 'f', 0));
    drawBar(p, x, y + 15, bw, 7, memPct, memC);
    y += 30;

    /* DISK */
    int diskUsed = 0, diskTotal = 0, diskPct = 0;
    diskInfo(diskUsed, diskTotal, diskPct);
    if (diskPct > 0) {
        QColor diskC = valColor(diskPct);
        p.setFont(smb); p.setPen(WHITE);
        p.drawText(x, y + 12, "DISK");
        p.setFont(sm); p.setPen(diskC);
        p.drawText(x + 40, y + 12,
                   QString("%1G / %2G  %3%")
                   .arg(diskUsed).arg(diskTotal)
                   .arg(diskPct));
        drawBar(p, x, y + 15, bw, 7, diskPct, diskC);
        y += 30;
    }
    y += 6;
    p.setPen(QPen(DIM, 1));
    p.drawLine(x, y, x + bw, y);
    y += 10;

    /* ── TOP PROCESSES ───────────────────────────────────── */
    y = drawSection(p, x, y, W, "TOP PROCESSES");

    /* Column positions proportional to bar width */
    int colCpu = x + bw * 60 / 100;
    int colMem = x + bw * 80 / 100;

    p.setFont(QFont("JetBrains Mono", 8));
    p.setPen(DIM);
    p.drawText(x,      y + 10, "NAME");
    p.drawText(colCpu, y + 10, "CPU%");
    p.drawText(colMem, y + 10, "MEM%");
    y += 13;
    p.setPen(QPen(DIM, 1));
    p.drawLine(x, y, x + bw, y);
    y += 4;

    for (const auto &[name, cpu_mem] : m_procs) {
        QFont pf("JetBrains Mono", 8);
        QFontMetrics pfm(pf);
        QString dispName = pfm.elidedText(name, Qt::ElideRight, colCpu - x - 4);
        QColor cc = valColor(cpu_mem.first);
        p.setFont(pf);
        p.setPen(WHITE);  p.drawText(x,      y + 10, dispName);
        p.setPen(cc);     p.drawText(colCpu, y + 10,
                                     QString("%1%").arg(cpu_mem.first,  0, 'f', 1));
        p.setPen(GREEN);  p.drawText(colMem, y + 10,
                                     QString("%1%").arg(cpu_mem.second, 0, 'f', 1));
        y += 13;
    }
    y += 6;
    p.setPen(QPen(DIM, 1));
    p.drawLine(x, y, x + bw, y);
    y += 10;

    /* ── QUICK KEYS ──────────────────────────────────────── */
    y = drawSection(p, x, y, W, "QUICK KEYS");

    int colAct = x + bw * 55 / 100;
    struct { const char *key, *action; } keys[] = {
        { "Mod+Return",  "terminal"     },
        { "Mod+Q",       "close window" },
        { "Mod+J/K",     "focus ↓/↑"    },
        { "Mod+Space",   "rotate layout"},
        { "Mod+Shift+R", "reload config"},
        { "Mod+1..9",    "workspace"    },
    };
    for (const auto &k : keys) {
        p.setFont(QFont("JetBrains Mono", 8));
        p.setPen(CYAN); p.drawText(x,      y + 11, k.key);
        p.setPen(DIM);  p.drawText(colAct, y + 11,
                                   QString("» %1").arg(k.action));
        y += 13;
    }

    /* Reposition dock if painted area height changed */
    if (qAbs(y - m_statsBottom) > 1) {
        m_statsBottom = y;
        placeDock();
    }

    p.end();
}
