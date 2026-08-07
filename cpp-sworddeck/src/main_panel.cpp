/* =========================================================
 * main_panel.cpp — left+center panel for sworddeck
 *
 * Tab bar below the clock:
 *   [📊 Graph]  [🌐 Browser]  [>_ Terminal]  [📁 Files]
 *
 * Graph tab  — animated graph PNG + clock (original behaviour)
 * Browser    — launch SwordFish browser; show status / PID
 * Terminal   — launch ghostty; show status / PID
 * Files      — launch swordfm / nautilus; show status / PID
 * ========================================================= */
#include "main_panel.h"

#include <QPainter>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QCoreApplication>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMouseEvent>
#include <QFileSystemWatcher>
#include <QDateTime>
#include <QFontMetrics>
#include <QStandardPaths>
#include <sys/utsname.h>
#include <cmath>

static const QColor CYAN (97,  175, 239);
static const QColor GREEN(152, 195, 121);
static const QColor DIM  (62,  68,  81 );
static const QColor WHITE(171, 178, 191);
static const QColor BG   (40,  44,  52 );
static const QColor AMBER(229, 192, 123);
static const QColor RED  (224, 108, 117);

static const int TAB_H    = 28;   /* tab bar height in px      */
static const int CLOCK_H  = 96;   /* clock + date + divider    */

static QString configDir() { return QDir::homePath() + "/.config/animated-wallpaper"; }

/* ── helpers ─────────────────────────────────────────────── */
static QString findExe(std::initializer_list<const char*> names) {
    for (const char *n : names) {
        QString p = QStandardPaths::findExecutable(QString::fromUtf8(n));
        if (!p.isEmpty()) return p;
    }
    return {};
}

/* ── constructor ─────────────────────────────────────────── */
MainPanel::MainPanel(QWidget *parent)
    : QWidget(parent), m_pipes(this)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setMouseTracking(true);
    loadGraph();

    auto *clockTimer = new QTimer(this);
    connect(clockTimer, &QTimer::timeout, this, QOverload<>::of(&QWidget::update));
    clockTimer->start(1000);

    auto *watcher = new QFileSystemWatcher(
        {configDir() + "/graph.png", configDir() + "/graph.json"}, this);
    connect(watcher, &QFileSystemWatcher::fileChanged, this, &MainPanel::loadGraph);

    /* Legacy bottom-right buttons — kept but hidden in launcher tabs */
    m_editBtn = new QPushButton("✚ EDIT GRAPH", this);
    m_editBtn->setCursor(Qt::PointingHandCursor);
    m_editBtn->setStyleSheet(
        "QPushButton { color: #98c379; background: rgba(62,68,81,160);"
        "border: 1px solid #3e4451; border-radius: 3px;"
        "font: bold 9pt 'JetBrains Mono'; padding: 3px 10px; }"
        "QPushButton:hover { background: rgba(97,175,239,200); border-color: #61afef; }"
    );
    connect(m_editBtn, &QPushButton::clicked, this, &MainPanel::editGraph);

    m_fmBtn = new QPushButton("📁 SWORDFM", this);
    m_fmBtn->setCursor(Qt::PointingHandCursor);
    m_fmBtn->setStyleSheet(
        "QPushButton { color: #98c379; background: rgba(62,68,81,160);"
        "border: 1px solid #3e4451; border-radius: 3px;"
        "font: bold 9pt 'JetBrains Mono'; padding: 3px 10px; }"
        "QPushButton:hover { background: rgba(97,175,239,200); border-color: #61afef; }"
    );
    connect(m_fmBtn, &QPushButton::clicked, this, &MainPanel::openFM);
}

/* ── slot: edit graph ────────────────────────────────────── */
void MainPanel::editGraph() {
    QString script = QDir(QCoreApplication::applicationDirPath())
                         .absoluteFilePath("../graph-edit.sh");
    QProcess::startDetached(script);
}

/* ── slot: open browser ──────────────────────────────────── */
void MainPanel::openBrowser() {
    /* Open DuckDuckGo in the system default browser.
     * SwordWM's default — lets xdg-open pick whatever the user
     * has set as their default browser. */
    QProcess::startDetached("xdg-open", {"https://duckduckgo.com"});
}

/* ── slot: open terminal ─────────────────────────────────── */
void MainPanel::openTerminal() {
    /* ghostty is the SwordWM default; fall back gracefully */
    QString exe = findExe({"ghostty", "alacritty", "kitty", "xterm"});
    if (exe.isEmpty()) return;
    if (!m_terminalProc || m_terminalProc->state() == QProcess::NotRunning) {
        delete m_terminalProc;
        m_terminalProc = new QProcess(this);
        connect(m_terminalProc, &QProcess::stateChanged,
                this, [this](QProcess::ProcessState){ update(); });
        m_terminalProc->start(exe, {});
    }
}

/* ── slot: open FM ───────────────────────────────────────── */
void MainPanel::openFM() {
    /* swordfm is the SwordWM default; fall back gracefully */
    QString exe = findExe({"swordfm", "nautilus", "thunar", "pcmanfm"});
    if (!exe.isEmpty()) QProcess::startDetached(exe, {});
}

/* ── launch for a tab ────────────────────────────────────── */
void MainPanel::launchForTab(Tab tab) {
    switch (tab) {
    case Tab::Browser:  openBrowser();  break;
    case Tab::Terminal: openTerminal(); break;
    case Tab::FM:       openFM();       break;
    default: break;
    }
}

/* ── mouse: tab clicks ───────────────────────────────────── */
void MainPanel::mousePressEvent(QMouseEvent *e) {
    for (int i = 0; i < static_cast<int>(Tab::Count); i++) {
        if (m_tabRects[i].contains(e->pos())) {
            Tab t = static_cast<Tab>(i);
            if (t != m_activeTab) {
                m_activeTab = t;
                update();
            }
            launchForTab(t);
            return;
        }
    }
    QWidget::mousePressEvent(e);
}

/* ── resize ──────────────────────────────────────────────── */
void MainPanel::resizeEvent(QResizeEvent *e) {
    QWidget::resizeEvent(e);
    /* Keep legacy buttons in bottom-right of graph tab */
    if (m_editBtn && m_fmBtn) {
        m_editBtn->adjustSize();
        m_fmBtn->adjustSize();
        int gap = 8;
        int total = m_editBtn->width() + gap + m_fmBtn->width();
        int bx = width() - total - 16;
        int by = height() - 56;
        m_editBtn->move(bx, by);
        m_fmBtn->move(bx + m_editBtn->width() + gap, by);
        bool showGraph = (m_activeTab == Tab::Graph);
        m_editBtn->setVisible(showGraph);
        m_fmBtn->setVisible(showGraph);
    }
}

/* ── load graph PNG ──────────────────────────────────────── */
void MainPanel::loadGraph() {
    QString png  = configDir() + "/graph.png";
    QString json = configDir() + "/graph.json";

    m_graphPixmap = QFile::exists(png) ? QPixmap(png) : QPixmap();
    if (!m_graphPixmap.isNull())
        m_mtime = QFileInfo(png).lastModified().toSecsSinceEpoch();

    QFile f(json);
    if (f.open(QIODevice::ReadOnly)) {
        auto obj = QJsonDocument::fromJson(f.readAll()).object();
        m_nodes = obj["nodes"].toArray().size();
        m_edges = obj["edges"].toArray().size();
    }
}

/* ── draw tab bar ────────────────────────────────────────── */
void MainPanel::drawTabBar(QPainter &p, int w, int tabBarY, int tabH) {
    struct TabDef { Tab id; const char *label; } tabs[] = {
        { Tab::Graph,    "📊 Graph"    },
        { Tab::Browser,  "🌐 Browser"  },
        { Tab::Terminal, ">_ Terminal" },
        { Tab::FM,       "📁 Files"    },
    };
    int n = static_cast<int>(Tab::Count);
    int tabW = w / n;

    QFont tf("JetBrains Mono", 9, QFont::Bold);
    p.setFont(tf);
    QFontMetrics tfm(tf);

    for (int i = 0; i < n; i++) {
        Tab t = tabs[i].id;
        bool active = (t == m_activeTab);
        int tx = i * tabW;
        QRect r(tx, tabBarY, (i == n-1) ? w - tx : tabW, tabH);
        m_tabRects[i] = r;

        /* Tab background */
        p.fillRect(r, active ? QColor(40,44,52,230) : QColor(26,28,36,180));

        /* Bottom indicator on active tab */
        if (active) {
            p.fillRect(tx, tabBarY + tabH - 2, r.width(), 2, CYAN);
        }

        /* Tab label */
        p.setPen(active ? CYAN : DIM);
        QString label = QString::fromUtf8(tabs[i].label);
        int lx = tx + (r.width() - tfm.horizontalAdvance(label)) / 2;
        p.drawText(lx, tabBarY + tfm.ascent() + (tabH - tfm.height()) / 2, label);
    }
}

/* ── draw graph tab content ──────────────────────────────── */
void MainPanel::drawGraphTab(QPainter &p, int w, int h, int contentY) {
    int graphH = h - contentY - 60;

    if (!m_graphPixmap.isNull()) {
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        const QSize scaled = m_graphPixmap.size().scaled(w, graphH, Qt::KeepAspectRatio);
        const QRect dst((w - scaled.width()) / 2,
                        contentY + (graphH - scaled.height()) / 2,
                        scaled.width(), scaled.height());
        p.drawPixmap(dst, m_graphPixmap, m_graphPixmap.rect());
    } else {
        p.setPen(DIM);
        p.setFont(QFont("JetBrains Mono", 11));
        p.drawText(QRect(0, contentY, w, graphH), Qt::AlignCenter, "[ no graph ]\n\nRun: graph-edit.sh");
    }

    /* Bottom status bar */
    int sy = h - 34;
    p.setPen(QPen(DIM, 1));
    p.drawLine(16, sy, w - 16, sy);
    p.setFont(QFont("JetBrains Mono", 9));

    QString upt = "?";
    QFile uptimeFile("/proc/uptime");
    if (uptimeFile.open(QIODevice::ReadOnly)) {
        double secs = uptimeFile.readAll().split(' ')[0].toDouble();
        upt = QString("%1h %2m").arg(int(secs/3600)).arg(int(fmod(secs,3600)/60));
    }
    QString kern = "?";
    struct utsname uts;
    if (uname(&uts) == 0) kern = QString(uts.release).split('-')[0];

    QFontMetrics sm(QFont("JetBrains Mono", 9));
    int tx = 16;
    auto draw = [&](const QColor &col, const QString &s) {
        p.setPen(col); p.drawText(tx, sy + 16, s);
        tx += sm.horizontalAdvance(s) + 20;
    };
    draw(GREEN, QString("◈ Nodes: %1  Links: %2").arg(m_nodes).arg(m_edges));
    draw(WHITE, QString("◈ Up: %1").arg(upt));
    draw(CYAN,  QString("◈ Kernel: %1").arg(kern));
}

/* ── draw launcher tab (Browser / Terminal / Files) ──────── */
void MainPanel::drawLauncherTab(QPainter &p, int w, int h, int contentY, Tab tab) {
    struct AppInfo {
        QString name;
        QString exe;
        QString description;
        QString launchHint;
        QColor  accentColor;
    };

    AppInfo info;
    QProcess *proc = nullptr;

    switch (tab) {
    case Tab::Browser:
        info.name        = "Browser";
        info.exe         = "xdg-open";   /* always available */
        info.description = "Opens DuckDuckGo in your default browser";
        info.launchHint  = "Click to open DuckDuckGo";
        info.accentColor = CYAN;
        proc = nullptr;   /* browser is not a tracked child process */
        break;
    case Tab::Terminal:
        info.name        = "ghostty";
        info.exe         = findExe({"ghostty", "alacritty", "kitty", "xterm"});
        info.description = "SwordWM default terminal";
        info.launchHint  = "Click to launch  |  Mod+Return";
        info.accentColor = GREEN;
        proc = m_terminalProc;
        break;
    case Tab::FM:
        info.name        = "swordfm";
        info.exe         = findExe({"swordfm", "nautilus", "thunar", "pcmanfm"});
        info.description = "SwordWM default file manager";
        info.launchHint  = "Click to launch";
        info.accentColor = AMBER;
        proc = m_fmProc;
        break;
    default: return;
    }

    bool running = proc && proc->state() == QProcess::Running;
    bool found   = !info.exe.isEmpty();

    int cx = w / 2;
    int y  = contentY + 30;

    /* App name */
    QFont titleFont("JetBrains Mono", 18, QFont::Bold);
    p.setFont(titleFont);
    p.setPen(info.accentColor);
    QFontMetrics tfm(titleFont);
    p.drawText(cx - tfm.horizontalAdvance(info.name)/2, y + tfm.ascent(), info.name);
    y += tfm.height() + 10;

    /* Divider */
    p.setPen(QPen(DIM, 1));
    p.drawLine(w/4, y, 3*w/4, y);
    y += 16;

    /* Description */
    QFont descFont("JetBrains Mono", 10);
    p.setFont(descFont);
    p.setPen(WHITE);
    QFontMetrics dfm(descFont);
    for (const QString &line : info.description.split('\n')) {
        p.drawText(cx - dfm.horizontalAdvance(line)/2, y + dfm.ascent(), line);
        y += dfm.height() + 4;
    }
    y += 20;

    /* Status pill */
    QString status = running ? "● RUNNING" : (found ? "○ NOT RUNNING" : "✗ NOT INSTALLED");
    QColor  statusColor = running ? GREEN : (found ? AMBER : RED);
    p.setFont(QFont("JetBrains Mono", 10, QFont::Bold));
    p.setPen(statusColor);
    QFontMetrics sfm(QFont("JetBrains Mono", 10, QFont::Bold));
    p.drawText(cx - sfm.horizontalAdvance(status)/2, y + sfm.ascent(), status);
    y += sfm.height() + 6;

    /* PID if running */
    if (running) {
        QString pid = QString("PID: %1").arg(proc->processId());
        p.setFont(QFont("JetBrains Mono", 9));
        p.setPen(DIM);
        QFontMetrics pfm(QFont("JetBrains Mono", 9));
        p.drawText(cx - pfm.horizontalAdvance(pid)/2, y + pfm.ascent(), pid);
        y += pfm.height() + 4;
    }
    y += 24;

    /* Launch / Kill button */
    if (found) {
        QString btnLabel = running ? "  Kill" : "  Launch";
        QColor btnBg  = running ? QColor(60,20,20,200) : QColor(20,40,60,200);
        QColor btnBdr = running ? RED : info.accentColor;

        int btnW = 140, btnH = 32;
        int bx = cx - btnW/2;
        QRect btnRect(bx, y, btnW, btnH);

        p.setPen(QPen(btnBdr, 1));
        p.setBrush(btnBg);
        p.drawRoundedRect(btnRect, 4, 4);

        p.setFont(QFont("JetBrains Mono", 10, QFont::Bold));
        p.setPen(running ? RED : info.accentColor);
        QFontMetrics bfm(QFont("JetBrains Mono", 10, QFont::Bold));
        p.drawText(cx - bfm.horizontalAdvance(btnLabel)/2,
                   y + (btnH + bfm.ascent() - bfm.descent())/2, btnLabel);
        y += btnH + 16;
    }

    /* Hint */
    p.setFont(QFont("JetBrains Mono", 8));
    p.setPen(DIM);
    QFontMetrics hfm(QFont("JetBrains Mono", 8));
    p.drawText(cx - hfm.horizontalAdvance(info.launchHint)/2,
               y + hfm.ascent(), info.launchHint);

    /* Exe path at bottom */
    if (found) {
        int by = h - 20;
        QString exeStr = "exe: " + info.exe;
        p.setFont(QFont("JetBrains Mono", 7));
        p.setPen(QColor(62,68,81,160));
        QFontMetrics efm(QFont("JetBrains Mono", 7));
        p.drawText(cx - efm.horizontalAdvance(exeStr)/2, by, exeStr);
    }
}

/* ── paint ───────────────────────────────────────────────── */
void MainPanel::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    int w = width(), h = height();

    m_pipes.paint(p);

    /* ── Clock strip (always visible) ──────────────────── */
    QDateTime now = QDateTime::currentDateTime();
    QFont clockFont("JetBrains Mono", 34, QFont::Bold);
    p.setFont(clockFont);
    p.setPen(CYAN);
    QString ts = now.toString("HH:mm:ss");
    QFontMetrics cfm(clockFont);
    p.drawText((w - cfm.horizontalAdvance(ts)) / 2, 52, ts);

    QFont dateFont("JetBrains Mono", 11);
    p.setFont(dateFont);
    p.setPen(GREEN);
    QString ds = now.toString("ddd  dd MMM yyyy");
    QFontMetrics dfm(dateFont);
    p.drawText((w - dfm.horizontalAdvance(ds)) / 2, 74, ds);

    /* ── Tab bar ────────────────────────────────────────── */
    int tabBarY = 82;
    p.setPen(QPen(DIM, 1));
    p.drawLine(8, tabBarY - 2, w - 8, tabBarY - 2);
    drawTabBar(p, w, tabBarY, TAB_H);

    int contentY = tabBarY + TAB_H + 4;

    /* ── Tab content ────────────────────────────────────── */
    switch (m_activeTab) {
    case Tab::Graph:
        if (m_editBtn) m_editBtn->setVisible(true);
        if (m_fmBtn)   m_fmBtn->setVisible(true);
        drawGraphTab(p, w, h, contentY);
        break;
    case Tab::Browser:
    case Tab::Terminal:
    case Tab::FM:
        if (m_editBtn) m_editBtn->setVisible(false);
        if (m_fmBtn)   m_fmBtn->setVisible(false);
        drawLauncherTab(p, w, h, contentY, m_activeTab);
        break;
    default: break;
    }

    p.end();
}
