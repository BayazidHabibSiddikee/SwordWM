/* =========================================================
 * main_panel.cpp
 *
 * MainPanel uses a QStackedWidget with 4 slots:
 *   0 – Graph     (GraphWidget: clock + graph PNG + status bar)
 *   1 – Browser   (EmbedSlot: SwordFish / firefox …)
 *   2 – FM        (EmbedSlot: swordfm / nautilus …)
 *   3 – Terminal  (EmbedSlot: ghostty / alacritty …)
 *
 * EmbedSlot launches the app with QProcess, then polls for its
 * X11 window ID (via _NET_CLIENT_LIST on the root window),
 * reparents it into a QWindow container.  A "⛶ Pop Out" button
 * moves the window back to WM control without killing the process.
 * ========================================================= */
#include "main_panel.h"

#include <QPainter>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFileSystemWatcher>
#include <QDateTime>
#include <QFontMetrics>
#include <QStandardPaths>
#include <QWindow>
#include <QScreen>
#include <QApplication>
#include <QFrame>
#include <sys/utsname.h>
#include <cmath>

/* Xlib for window discovery + reparent */
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#undef Bool
#undef None
#undef Status

static const QColor CYAN (97,  175, 239);
static const QColor GREEN(152, 195, 121);
static const QColor DIM  (62,  68,  81 );
static const QColor WHITE(171, 178, 191);
static const int    CLOCK_H = 80;

static QString cfgDir() { return QDir::homePath() + "/.config/animated-wallpaper"; }

static QString findExeList(const QStringList &names) {
    for (const QString &n : names) {
        QString p = QStandardPaths::findExecutable(n);
        if (!p.isEmpty()) return p;
    }
    return {};
}

/* ── button stylesheet ──────────────────────────────────── */
static QString btnQss(const QString &accent = "#61afef") {
    return QString(
        "QPushButton { color:%1; background:rgba(40,44,52,200);"
        "border:1px solid %1; border-radius:4px;"
        "font:bold 9pt 'JetBrains Mono'; padding:4px 14px; }"
        "QPushButton:hover { background:rgba(97,175,239,180); color:#1e2228; }"
    ).arg(accent);
}

/* =========================================================
 * GraphWidget
 * ========================================================= */
GraphWidget::GraphWidget(QWidget *parent)
    : QWidget(parent), m_pipes(this)
{
    setAttribute(Qt::WA_TranslucentBackground);

    auto *t = new QTimer(this);
    connect(t, &QTimer::timeout, this, QOverload<>::of(&QWidget::update));
    t->start(1000);

    auto *w = new QFileSystemWatcher({cfgDir()+"/graph.png", cfgDir()+"/graph.json"}, this);
    connect(w, &QFileSystemWatcher::fileChanged, this, &GraphWidget::loadGraph);
    loadGraph();

    m_editBtn = new QPushButton("✚ EDIT GRAPH", this);
    m_editBtn->setCursor(Qt::PointingHandCursor);
    m_editBtn->setStyleSheet(btnQss("#98c379"));
    connect(m_editBtn, &QPushButton::clicked, this, &GraphWidget::editGraph);

    m_fmBtn = new QPushButton("📁 SWORDFM", this);
    m_fmBtn->setCursor(Qt::PointingHandCursor);
    m_fmBtn->setStyleSheet(btnQss("#98c379"));
    connect(m_fmBtn, &QPushButton::clicked, this, []() {
        QString e = findExeList({"swordfm","nautilus","thunar","pcmanfm"});
        if (!e.isEmpty()) QProcess::startDetached(e, {});
    });
}

void GraphWidget::editGraph() {
    /* Binary lives at SwordWM/cpp-sworddeck/build/sworddeck,
     * script lives at SwordWM/graph-edit.sh — two levels up. */
    QProcess::startDetached(
        QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../../graph-edit.sh"));
}

void GraphWidget::resizeEvent(QResizeEvent *e) {
    QWidget::resizeEvent(e);
    if (m_editBtn && m_fmBtn) {
        m_editBtn->adjustSize(); m_fmBtn->adjustSize();
        int gap = 8;
        int bx  = width() - m_editBtn->width() - gap - m_fmBtn->width() - 16;
        int by  = height() - 44;
        m_editBtn->move(bx, by);
        m_fmBtn->move(bx + m_editBtn->width() + gap, by);
    }
}

void GraphWidget::loadGraph() {
    QString png  = cfgDir() + "/graph.png";
    QString json = cfgDir() + "/graph.json";
    m_graphPixmap = QFile::exists(png) ? QPixmap(png) : QPixmap();
    QFile f(json);
    if (f.open(QIODevice::ReadOnly)) {
        auto o = QJsonDocument::fromJson(f.readAll()).object();
        m_nodes = o["nodes"].toArray().size();
        m_edges = o["edges"].toArray().size();
    }
    update();
}

void GraphWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    int w = width(), h = height();
    m_pipes.paint(p);

    /* Clock */
    QFont cf("JetBrains Mono", 34, QFont::Bold);
    p.setFont(cf); p.setPen(CYAN);
    QString ts = QDateTime::currentDateTime().toString("HH:mm:ss");
    QFontMetrics cfm(cf);
    p.drawText((w - cfm.horizontalAdvance(ts)) / 2, 52, ts);

    /* Date */
    QFont df("JetBrains Mono", 11);
    p.setFont(df); p.setPen(GREEN);
    QString ds = QDateTime::currentDateTime().toString("ddd  dd MMM yyyy");
    QFontMetrics dfm(df);
    p.drawText((w - dfm.horizontalAdvance(ds)) / 2, 74, ds);

    p.setPen(QPen(DIM, 1));
    p.drawLine(8, CLOCK_H, w - 8, CLOCK_H);

    /* Graph image */
    const int STATUS_H = 40;
    int graphH = h - CLOCK_H - 6 - STATUS_H;
    int gy     = CLOCK_H + 6;
    if (!m_graphPixmap.isNull()) {
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        QSize sc = m_graphPixmap.size().scaled(w, graphH, Qt::KeepAspectRatio);
        QRect dst((w - sc.width()) / 2, gy + (graphH - sc.height()) / 2,
                  sc.width(), sc.height());
        p.drawPixmap(dst, m_graphPixmap, m_graphPixmap.rect());
    } else {
        p.setPen(DIM); p.setFont(QFont("JetBrains Mono", 11));
        p.drawText(QRect(0, gy, w, graphH), Qt::AlignCenter,
                   "[ no graph ]\n\nRun: graph-edit.sh");
    }

    /* Status bar */
    int sy = h - STATUS_H + 4;
    p.setPen(QPen(DIM, 1)); p.drawLine(16, sy, w - 16, sy);
    p.setFont(QFont("JetBrains Mono", 9));
    QFontMetrics sm(QFont("JetBrains Mono", 9));
    int tx = 16, ty = sy + sm.ascent() + 8;

    QString upt = "?";
    QFile uf("/proc/uptime");
    if (uf.open(QIODevice::ReadOnly)) {
        double s = uf.readAll().split(' ')[0].toDouble();
        upt = QString("%1h %2m").arg(int(s/3600)).arg(int(fmod(s,3600)/60));
    }
    QString kern = "?";
    struct utsname uts;
    if (uname(&uts) == 0) kern = QString(uts.release).split('-')[0];

    auto draw = [&](const QColor &c, const QString &s) {
        p.setPen(c); p.drawText(tx, ty, s);
        tx += sm.horizontalAdvance(s) + 20;
    };
    draw(GREEN, QString("◈ Nodes: %1  Links: %2").arg(m_nodes).arg(m_edges));
    draw(WHITE, QString("◈ Up: %1").arg(upt));
    draw(CYAN,  QString("◈ Kernel: %1").arg(kern));
    p.end();
}

/* =========================================================
 * EmbedSlot
 * ========================================================= */
EmbedSlot::EmbedSlot(const QString &name,
                     std::initializer_list<const char *> exeFallbacks,
                     QWidget *parent)
    : QWidget(parent), m_name(name)
{
    for (const char *e : exeFallbacks) m_exeFallbacks << QString::fromUtf8(e);

    setAttribute(Qt::WA_TranslucentBackground);
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);

    m_embedTimer = new QTimer(this);
    m_embedTimer->setInterval(200);
    connect(m_embedTimer, &QTimer::timeout, this, &EmbedSlot::tryEmbed);

    rebuildPlaceholder();
}

EmbedSlot::~EmbedSlot() {
    if (m_proc && m_proc->state() == QProcess::Running)
        m_proc->terminate();
}

QString EmbedSlot::findExe() const {
    return findExeList(m_exeFallbacks);
}

bool EmbedSlot::isRunning() const {
    return m_proc && m_proc->state() == QProcess::Running;
}

void EmbedSlot::rebuildPlaceholder() {
    /* Remove old placeholder */
    if (m_placeholder) { m_placeholder->deleteLater(); m_placeholder = nullptr; }

    m_placeholder = new QWidget(this);
    m_placeholder->setAttribute(Qt::WA_TranslucentBackground);
    auto *vl = new QVBoxLayout(m_placeholder);
    vl->setAlignment(Qt::AlignCenter);
    vl->setSpacing(12);

    /* App name label */
    auto *lbl = new QLabel(m_name, m_placeholder);
    lbl->setAlignment(Qt::AlignCenter);
    lbl->setStyleSheet("color:#61afef; font:bold 22pt 'JetBrains Mono'; background:transparent;");
    vl->addWidget(lbl);

    QString exe = findExe();

    /* Status */
    auto *status = new QLabel(isRunning() ? "● Running — waiting for window…"
                              : exe.isEmpty() ? "✗ Not installed"
                              : "○ Not running", m_placeholder);
    status->setAlignment(Qt::AlignCenter);
    status->setStyleSheet(
        QString("color:%1; font:10pt 'JetBrains Mono'; background:transparent;")
        .arg(isRunning() ? "#98c379" : exe.isEmpty() ? "#e06c75" : "#e5c07b"));
    vl->addWidget(status);

    /* Launch button */
    if (!exe.isEmpty() && !isRunning()) {
        m_launchBtn = new QPushButton("  Launch", m_placeholder);
        m_launchBtn->setCursor(Qt::PointingHandCursor);
        m_launchBtn->setFixedSize(140, 34);
        m_launchBtn->setStyleSheet(btnQss("#61afef"));
        connect(m_launchBtn, &QPushButton::clicked, this, &EmbedSlot::launch);
        vl->addWidget(m_launchBtn, 0, Qt::AlignCenter);
    }

    m_layout->addWidget(m_placeholder);
}

void EmbedSlot::launch() {
    if (isRunning()) return;

    QString exe = findExe();
    if (exe.isEmpty()) return;

    delete m_proc;
    m_proc = new QProcess(this);
    connect(m_proc, &QProcess::stateChanged, this, [this](QProcess::ProcessState st) {
        if (st == QProcess::NotRunning) {
            /* Process died — remove container, show placeholder */
            if (m_container) {
                m_layout->removeWidget(m_container);
                m_container->deleteLater();
                m_container = nullptr;
                m_embeddedWid = 0;
            }
            rebuildPlaceholder();
            emit stateChanged();
        }
    });

    m_proc->start(exe, {});
    m_embedTries = 0;
    m_embedTimer->start();

    /* Update placeholder to "waiting…" state */
    rebuildPlaceholder();
    emit stateChanged();
}

/* ── Poll for the app's X11 window ─────────────────────── */
void EmbedSlot::tryEmbed() {
    if (!m_proc || m_proc->state() != QProcess::Running) {
        m_embedTimer->stop();
        return;
    }

    qint64 pid = m_proc->processId();

    Display *dpy = XOpenDisplay(nullptr);
    if (!dpy) return;

    Window root = DefaultRootWindow(dpy);
    Atom listAtom = XInternAtom(dpy, "_NET_CLIENT_LIST", False);
    Atom actual; int fmt; unsigned long n, after;
    unsigned char *data = nullptr;

    WId found = 0;
    if (XGetWindowProperty(dpy, root, listAtom, 0, 1024, False, XA_WINDOW,
                           &actual, &fmt, &n, &after, &data) == Success && data) {
        Window *wins = (Window *)data;
        for (unsigned long i = 0; i < n && !found; ++i) {
            /* Check _NET_WM_PID */
            Atom pidAtom = XInternAtom(dpy, "_NET_WM_PID", False);
            Atom a2; int f2; unsigned long n2, af2;
            unsigned char *pd = nullptr;
            if (XGetWindowProperty(dpy, wins[i], pidAtom, 0, 1, False, XA_CARDINAL,
                                   &a2, &f2, &n2, &af2, &pd) == Success && pd) {
                unsigned long wpid = *(unsigned long *)pd;
                XFree(pd);
                if ((qint64)wpid == pid) { found = (WId)wins[i]; }
            }
        }
        XFree(data);
    }
    XCloseDisplay(dpy);

    ++m_embedTries;
    if (found) {
        m_embedTimer->stop();
        doEmbed(found);
    } else if (m_embedTries > 50) {
        /* 10 seconds — give up polling, just leave placeholder */
        m_embedTimer->stop();
    }
}

void EmbedSlot::doEmbed(WId wid) {
    m_embeddedWid = wid;

    /* Remove placeholder */
    if (m_placeholder) {
        m_layout->removeWidget(m_placeholder);
        m_placeholder->hide();
        m_placeholder->deleteLater();
        m_placeholder = nullptr;
    }

    /* Wrap foreign window */
    QWindow *foreign = QWindow::fromWinId(wid);
    foreign->setFlags(Qt::FramelessWindowHint);
    m_container = QWidget::createWindowContainer(foreign, this);
    m_container->setMinimumSize(100, 100);
    m_container->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_container->setFocusPolicy(Qt::StrongFocus);

    /* Top bar: app name + pop-out button */
    auto *bar    = new QWidget(this);
    bar->setFixedHeight(30);
    bar->setStyleSheet("background:rgba(30,34,40,220);");
    auto *hl     = new QHBoxLayout(bar);
    hl->setContentsMargins(8, 0, 8, 0);
    hl->setSpacing(8);

    auto *nameLbl = new QLabel("  " + m_name, bar);
    nameLbl->setStyleSheet("color:#61afef; font:bold 9pt 'JetBrains Mono'; background:transparent;");
    hl->addWidget(nameLbl);
    hl->addStretch(1);

    m_popOutBtn = new QPushButton("⛶  Pop Out", bar);
    m_popOutBtn->setCursor(Qt::PointingHandCursor);
    m_popOutBtn->setFixedHeight(22);
    m_popOutBtn->setStyleSheet(btnQss("#e5c07b"));
    connect(m_popOutBtn, &QPushButton::clicked, this, &EmbedSlot::popOut);
    hl->addWidget(m_popOutBtn);

    auto *closeBtn = new QPushButton("✕", bar);
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setFixedSize(22, 22);
    closeBtn->setStyleSheet(btnQss("#e06c75"));
    connect(closeBtn, &QPushButton::clicked, this, [this]() {
        if (m_proc) m_proc->terminate();
    });
    hl->addWidget(closeBtn);

    /* Stack: bar on top, container fills rest */
    auto *wrapper = new QWidget(this);
    wrapper->setStyleSheet("background:transparent;");
    auto *wl = new QVBoxLayout(wrapper);
    wl->setContentsMargins(0, 0, 0, 0);
    wl->setSpacing(0);
    wl->addWidget(bar);
    wl->addWidget(m_container, 1);

    m_layout->addWidget(wrapper);

    /* Force the embedded window to match our size immediately.
     * The container may not have its final geometry yet, so defer
     * one frame — but also do a synchronous resize right now for
     * the common case where the container already has a size. */
    QTimer::singleShot(0, this, &EmbedSlot::forceResizeEmbedded);
    QTimer::singleShot(200, this, &EmbedSlot::forceResizeEmbedded);

    emit stateChanged();
}

void EmbedSlot::popOut() {
    if (!m_embeddedWid) return;

    /* Re-map window under root — WM will manage it again */
    Display *dpy = XOpenDisplay(nullptr);
    if (dpy) {
        Window root = DefaultRootWindow(dpy);
        XReparentWindow(dpy, (Window)m_embeddedWid, root, 100, 100);
        XMapRaised(dpy, (Window)m_embeddedWid);
        XFlush(dpy);
        XCloseDisplay(dpy);
    }

    /* Remove container from our layout */
    if (m_container) {
        m_layout->removeWidget(m_container->parentWidget()
                                   ? m_container->parentWidget()
                                   : m_container);
        m_container->deleteLater();
        m_container = nullptr;
    }
    m_embeddedWid = 0;

    /* Show placeholder again (app is still running) */
    rebuildPlaceholder();
    emit stateChanged();
}

void EmbedSlot::resizeEvent(QResizeEvent *e) {
    QWidget::resizeEvent(e);
    forceResizeEmbedded();
}

void EmbedSlot::forceResizeEmbedded() {
    if (!m_embeddedWid || !m_container) return;
    /* Force the embedded X11 window to match our container size.
     * Without this, the window stays at its initial (often smaller) size
     * because QWindow::fromWinId doesn't always propagate Qt resize events
     * to foreign X11 windows.  The Python version does the same via xdotool. */
    Display *dpy = XOpenDisplay(nullptr);
    if (!dpy) return;
    int w = qMax(1, m_container->width());
    int h = qMax(1, m_container->height());
    XResizeWindow(dpy, (Window)m_embeddedWid, (unsigned)w, (unsigned)h);
    XMoveResizeWindow(dpy, (Window)m_embeddedWid, 0, 0, (unsigned)w, (unsigned)h);
    XFlush(dpy);
    XCloseDisplay(dpy);
}

/* =========================================================
 * MainPanel
 * ========================================================= */
MainPanel::MainPanel(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_stack = new QStackedWidget(this);
    m_stack->setAttribute(Qt::WA_TranslucentBackground);

    m_graph    = new GraphWidget(m_stack);
    m_browser  = new EmbedSlot("Browser",
                    {"SwordFish","zen-browser","firefox",
                     "chromium","google-chrome-stable"}, m_stack);
    m_fm       = new EmbedSlot("Files",
                    {"swordfm","nautilus","thunar","pcmanfm"}, m_stack);
    m_terminal = new EmbedSlot("Terminal",
                    {"ghostty","alacritty","kitty","xterm"}, m_stack);

    m_stack->addWidget(m_graph);      /* index 0 */
    m_stack->addWidget(m_browser);    /* index 1 */
    m_stack->addWidget(m_fm);         /* index 2 */
    m_stack->addWidget(m_terminal);   /* index 3 */
    m_stack->setCurrentIndex(0);

    root->addWidget(m_stack);
}

void MainPanel::showPanel(const QString &id) {
    if      (id == "graph")    switchTo(PanelSlot::Graph);
    else if (id == "browser")  switchTo(PanelSlot::Browser);
    else if (id == "fm")       switchTo(PanelSlot::FM);
    else if (id == "terminal") switchTo(PanelSlot::Terminal);
}

void MainPanel::switchTo(PanelSlot slot) {
    m_stack->setCurrentIndex(static_cast<int>(slot));

    /* Auto-launch the app if it isn't running yet */
    switch (slot) {
    case PanelSlot::Browser:
        if (!m_browser->isRunning())  m_browser->launch();  break;
    case PanelSlot::FM:
        if (!m_fm->isRunning())       m_fm->launch();       break;
    case PanelSlot::Terminal:
        if (!m_terminal->isRunning()) m_terminal->launch(); break;
    default: break;
    }
}
