/* =========================================================
 * main_panel.cpp
 *
 * Browser-like main panel with:
 *   Clock at top
 *   Tab bar: [Graph] (always present) + dynamic app tabs
 *   Content: stacked graph/embedded apps
 * ========================================================= */
#include "main_panel.h"

#include <QPainter>
#include <QPainterPath>
#include <QFile>
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
#include <QApplication>
#include <QMouseEvent>
#include <QVBoxLayout>
#include <sys/utsname.h>
#include <cmath>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#undef Bool
#undef None
#undef Status

static const QColor CYAN   (97,  175, 239);
static const QColor GREEN  (152, 195, 121);
static const QColor AMBER  (229, 192, 123);
static const QColor RED    (224, 108, 117);
static const QColor DIM    (62,  68,  81 );
static const QColor WHITE  (171, 178, 191);
static const QColor TAB_BG (48,  52,  62 );
static const QColor TAB_ACT(55,  60,  72 );
static const QColor TAB_HVR(52,  56,  66 );

static const int CLOCK_H = 100;
static const int TAB_H   = 34;
static const int TAB_MIN = 80;
static const int TAB_ACT_W = 180;

static QString cfgDir() { return QDir::homePath() + "/.config/animated-wallpaper"; }

static QString findExeList(const QStringList &names) {
    for (const QString &n : names) {
        QString p = QStandardPaths::findExecutable(n);
        if (!p.isEmpty()) return p;
    }
    return {};
}

static QString btnQss(const QString &accent = "#61afef") {
    return QString(
        "QPushButton { color:%1; background:rgba(40,44,52,200);"
        "border:1px solid %1; border-radius:4px;"
        "font:bold 9pt 'JetBrains Mono'; padding:4px 14px; }"
        "QPushButton:hover { background:rgba(97,175,239,180); color:#1e2228; }"
    ).arg(accent);
}

/* =========================================================
 * ClockWidget
 * ========================================================= */
ClockWidget::ClockWidget(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedHeight(CLOCK_H);
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &ClockWidget::tick);
    m_timer->start(1000);
}

void ClockWidget::tick() { update(); }

void ClockWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    int w = width(), h = height();

    p.fillRect(0, 0, w, h, QColor(30, 34, 40));

    QFont tf("JetBrains Mono", 38, QFont::Bold);
    p.setFont(tf); p.setPen(CYAN);
    QString ts = QDateTime::currentDateTime().toString("HH:mm:ss");
    QFontMetrics tfm(tf);
    p.drawText((w - tfm.horizontalAdvance(ts)) / 2, 52, ts);

    QFont df("JetBrains Mono", 12);
    p.setFont(df); p.setPen(GREEN);
    QString ds = QDateTime::currentDateTime().toString("ddd  dd MMMM yyyy");
    QFontMetrics dfm(df);
    p.drawText((w - dfm.horizontalAdvance(ds)) / 2, 74, ds);

    /* Close button — top right */
    QFont xf("JetBrains Mono", 12, QFont::Bold);
    p.setFont(xf);
    p.setPen(QColor(150, 155, 165));
    p.drawText(w - 36, 0, 30, 30, Qt::AlignCenter, "✕");

    p.setPen(QPen(DIM, 1));
    p.drawLine(0, h - 1, w, h - 1);
}

void ClockWidget::mousePressEvent(QMouseEvent *e) {
    if (e->position().x() > width() - 40 && e->position().y() < 32)
        emit closeRequested();
}

/* =========================================================
 * GraphWidget — shows graph image + status
 * ========================================================= */
GraphWidget::GraphWidget(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground);
    auto *w = new QFileSystemWatcher({cfgDir()+"/graph.png", cfgDir()+"/graph.json"}, this);
    connect(w, &QFileSystemWatcher::fileChanged, this, &GraphWidget::loadGraph);
    loadGraph();
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

    /* Graph image */
    if (!m_graphPixmap.isNull()) {
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        QSize sc = m_graphPixmap.size().scaled(w, h, Qt::KeepAspectRatio);
        QRect dst((w - sc.width()) / 2, (h - sc.height()) / 2,
                  sc.width(), sc.height());
        p.drawPixmap(dst, m_graphPixmap, m_graphPixmap.rect());
    } else {
        p.setPen(DIM); p.setFont(QFont("JetBrains Mono", 11));
        p.drawText(QRect(0, 0, w, h), Qt::AlignCenter,
                   "[ no graph ]\n\nRun: graph-edit.sh");
    }

    /* Status bar at bottom */
    int STATUS_H = 36;
    int sy = h - STATUS_H;
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
}

/* =========================================================
 * AppTab
 * ========================================================= */
AppTab::AppTab(const QString &name, const QString &icon,
               int index, QWidget *parent)
    : QWidget(parent), m_name(name), m_icon(icon), m_index(index)
{
    setFixedHeight(TAB_H);
    setCursor(Qt::PointingHandCursor);
    setMouseTracking(true);
}

void AppTab::setActive(bool active) {
    if (m_active != active) { m_active = active; update(); }
}

void AppTab::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    int w = width(), h = height();

    QColor bg = m_active ? TAB_ACT : m_hovered ? TAB_HVR : TAB_BG;
    p.fillRect(0, 0, w, h, bg);
    if (m_active) p.fillRect(0, 0, w, 3, CYAN);

    /* Close button */
    QFont xf("JetBrains Mono", 10);
    p.setFont(xf);
    p.setPen(QColor(150, 155, 165));
    p.drawText(w - 24, 0, 20, h, Qt::AlignCenter, "✕");

    /* Icon + name */
    QFont nf("JetBrains Mono", 10, QFont::Bold);
    p.setFont(nf);
    p.setPen(m_active ? CYAN : WHITE);

    QString label = m_icon + " " + m_name;
    QFontMetrics nfm(nf);
    int textW = nfm.horizontalAdvance(label);
    int maxTextW = w - 36;
    if (textW > maxTextW) {
        while (textW > maxTextW - nfm.horizontalAdvance("…") && !label.isEmpty()) {
            label.chop(1);
            textW = nfm.horizontalAdvance(label);
        }
        label += "…";
    }
    int textY = (h + nfm.ascent() - nfm.descent()) / 2;
    p.drawText(12, textY, label);
}

void AppTab::mousePressEvent(QMouseEvent *e) {
    if (e->position().x() > width() - 24)
        emit closeRequested(m_index);
    else
        emit clicked(m_index);
}

void AppTab::enterEvent(QEnterEvent *) { m_hovered = true; update(); }
void AppTab::leaveEvent(QEvent *)      { m_hovered = false; update(); }

/* =========================================================
 * AppTabBar
 * ========================================================= */
AppTabBar::AppTabBar(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedHeight(TAB_H);
    setMouseTracking(true);
}

void AppTabBar::addTab(const QString &name, const QString &icon, int index) {
    auto *tab = new AppTab(name, icon, index, this);
    connect(tab, &AppTab::clicked, this, &AppTabBar::tabClicked);
    connect(tab, &AppTab::closeRequested, this, &AppTabBar::tabCloseRequested);
    m_tabs.append(tab);
    relayout();
}

void AppTabBar::removeTab(int index) {
    for (int i = 0; i < m_tabs.size(); ++i) {
        if (m_tabs[i]->appIndex() == index) {
            m_tabs[i]->deleteLater();
            m_tabs.remove(i);
            if (m_activeIndex == index) m_activeIndex = -1;
            relayout();
            return;
        }
    }
}

void AppTabBar::setActiveTab(int index) {
    m_activeIndex = index;
    for (auto *t : m_tabs) t->setActive(t->appIndex() == index);
    relayout();
}

void AppTabBar::resizeEvent(QResizeEvent *) { relayout(); }

void AppTabBar::relayout() {
    int w = width();
    int n = m_tabs.size();
    if (n == 0) return;

    int totalMin = n * TAB_MIN;
    int extra = qMax(0, w - totalMin);
    int activeExtra = qMin(extra, TAB_ACT_W - TAB_MIN);
    int remaining = extra - activeExtra;
    int perInactive = (n > 1) ? remaining / (n - 1) : 0;

    int x = 0;
    for (int i = 0; i < n; ++i) {
        int tw = (m_tabs[i]->appIndex() == m_activeIndex)
                 ? TAB_MIN + activeExtra
                 : TAB_MIN + perInactive;
        m_tabs[i]->setGeometry(x, 0, tw, TAB_H);
        x += tw;
    }
}

void AppTabBar::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.fillRect(0, 0, width(), height(), QColor(30, 34, 40));
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

QString EmbedSlot::findExe() const { return findExeList(m_exeFallbacks); }
bool EmbedSlot::isRunning() const { return m_proc && m_proc->state() == QProcess::Running; }

void EmbedSlot::rebuildPlaceholder() {
    if (m_placeholder) { m_placeholder->deleteLater(); m_placeholder = nullptr; }
    m_placeholder = new QWidget(this);
    m_placeholder->setAttribute(Qt::WA_TranslucentBackground);
    auto *vl = new QVBoxLayout(m_placeholder);
    vl->setAlignment(Qt::AlignCenter);
    auto *lbl = new QLabel(m_name, m_placeholder);
    lbl->setAlignment(Qt::AlignCenter);
    lbl->setStyleSheet("color:#61afef; font:bold 24pt 'JetBrains Mono'; background:transparent;");
    vl->addWidget(lbl);
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
    rebuildPlaceholder();
    emit stateChanged();
}

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
            Atom pidAtom = XInternAtom(dpy, "_NET_WM_PID", False);
            Atom a2; int f2; unsigned long n2, af2;
            unsigned char *pd = nullptr;
            if (XGetWindowProperty(dpy, wins[i], pidAtom, 0, 1, False, XA_CARDINAL,
                                   &a2, &f2, &n2, &af2, &pd) == Success && pd) {
                if ((qint64)*(unsigned long *)pd == pid) found = (WId)wins[i];
                XFree(pd);
            }
        }
        XFree(data);
    }
    XCloseDisplay(dpy);
    ++m_embedTries;
    if (found) { m_embedTimer->stop(); doEmbed(found); }
    else if (m_embedTries > 50) m_embedTimer->stop();
}

void EmbedSlot::doEmbed(WId wid) {
    m_embeddedWid = wid;
    if (m_placeholder) {
        m_layout->removeWidget(m_placeholder);
        m_placeholder->hide();
        m_placeholder->deleteLater();
        m_placeholder = nullptr;
    }

    /* Set the window to be frameless before embedding */
    Display *dpy = XOpenDisplay(nullptr);
    if (dpy) {
        /* Remove decorations */
        Atom atom = XInternAtom(dpy, "_MOTIF_WM_HINTS", False);
        long hints[5] = {2, 0, 0, 0, 0};  /* no decorations */
        XChangeProperty(dpy, wid, atom, atom, 32, PropModeReplace,
                        (unsigned char *)hints, 5);
        XFlush(dpy);
        XCloseDisplay(dpy);
    }

    QWindow *foreign = QWindow::fromWinId(wid);
    foreign->setFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    m_container = QWidget::createWindowContainer(foreign, this);
    m_container->setMinimumSize(200, 150);
    m_container->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_container->setFocusPolicy(Qt::StrongFocus);

    /* Top bar: app name + pop-out + close */
    auto *bar = new QWidget(this);
    bar->setFixedHeight(28);
    bar->setStyleSheet("background:rgba(30,34,40,220);");
    auto *hl = new QHBoxLayout(bar);
    hl->setContentsMargins(8, 0, 8, 0);
    hl->setSpacing(8);
    auto *nameLbl = new QLabel("  " + m_name, bar);
    nameLbl->setStyleSheet("color:#61afef; font:bold 9pt 'JetBrains Mono'; background:transparent;");
    hl->addWidget(nameLbl);
    hl->addStretch(1);
    m_popOutBtn = new QPushButton("⛶  Pop Out", bar);
    m_popOutBtn->setCursor(Qt::PointingHandCursor);
    m_popOutBtn->setFixedHeight(20);
    m_popOutBtn->setStyleSheet(btnQss("#e5c07b"));
    connect(m_popOutBtn, &QPushButton::clicked, this, &EmbedSlot::popOut);
    hl->addWidget(m_popOutBtn);
    auto *closeBtn = new QPushButton("✕", bar);
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setFixedSize(20, 20);
    closeBtn->setStyleSheet(btnQss("#e06c75"));
    connect(closeBtn, &QPushButton::clicked, this, &EmbedSlot::closeApp);
    hl->addWidget(closeBtn);

    auto *wrapper = new QWidget(this);
    wrapper->setStyleSheet("background:transparent;");
    auto *wl = new QVBoxLayout(wrapper);
    wl->setContentsMargins(0, 0, 0, 0);
    wl->setSpacing(0);
    wl->addWidget(bar);
    wl->addWidget(m_container, 1);
    m_layout->addWidget(wrapper);

    /* Force resize after layout settles — multiple retries */
    QTimer::singleShot(0, this, &EmbedSlot::forceResizeEmbedded);
    QTimer::singleShot(200, this, &EmbedSlot::forceResizeEmbedded);
    QTimer::singleShot(500, this, &EmbedSlot::forceResizeEmbedded);
    emit stateChanged();
}

void EmbedSlot::closeApp() {
    if (m_proc && m_proc->state() == QProcess::Running) {
        m_proc->terminate();
        /* Force kill after 500ms if process doesn't die */
        QTimer::singleShot(500, this, [this]() {
            if (m_proc && m_proc->state() == QProcess::Running) {
                m_proc->kill();
            }
        });
    }
}

void EmbedSlot::popOut() {
    if (!m_embeddedWid) return;

    /* Reparent the X11 window back to root so WM takes over */
    Display *dpy = XOpenDisplay(nullptr);
    if (dpy) {
        Window root = DefaultRootWindow(dpy);
        /* Get current position before reparenting */
        XWindowAttributes attr;
        if (XGetWindowAttributes(dpy, (Window)m_embeddedWid, &attr)) {
            /* Map to root coordinates */
            Window child;
            int rx, ry;
            XTranslateCoordinates(dpy, (Window)m_embeddedWid, root,
                                  0, 0, &rx, &ry, &child);
            XReparentWindow(dpy, (Window)m_embeddedWid, root, rx, ry);
        } else {
            XReparentWindow(dpy, (Window)m_embeddedWid, root, 100, 100);
        }
        /* Remove our size hints so WM can manage it */
        XDeleteProperty(dpy, (Window)m_embeddedWid,
                        XInternAtom(dpy, "WM_NORMAL_HINTS", False));
        XMapRaised(dpy, (Window)m_embeddedWid);
        XFlush(dpy);
        XCloseDisplay(dpy);
    }

    /* Disconnect signals before deleting to prevent crashes */
    if (m_popOutBtn) {
        disconnect(m_popOutBtn, nullptr, this, nullptr);
        m_popOutBtn = nullptr;
    }

    /* Remove the wrapper (contains bar + container) from layout */
    if (m_container) {
        QWidget *wrapper = m_container->parentWidget();
        if (wrapper) {
            m_layout->removeWidget(wrapper);
            wrapper->hide();
            wrapper->deleteLater();
        }
        m_container = nullptr;
    }
    m_embeddedWid = 0;

    rebuildPlaceholder();
    emit stateChanged();
}

void EmbedSlot::resizeEvent(QResizeEvent *e) {
    QWidget::resizeEvent(e);
    forceResizeEmbedded();
}

void EmbedSlot::forceResizeEmbedded() {
    if (!m_embeddedWid || !m_container) return;

    /* Wait for container to have valid geometry */
    if (m_container->width() < 10 || m_container->height() < 10) {
        QTimer::singleShot(100, this, &EmbedSlot::forceResizeEmbedded);
        return;
    }

    /* Get container's screen position and size */
    QPoint screenPos = m_container->mapToGlobal(QPoint(0, 0));
    int w = m_container->width();
    int h = m_container->height();

    Display *dpy = XOpenDisplay(nullptr);
    if (!dpy) return;

    /* Remove any size hints that force the window larger */
    XRemoveFromSaveSet(dpy, (Window)m_embeddedWid);

    /* Position and size the embedded window to match container exactly */
    XMoveResizeWindow(dpy, (Window)m_embeddedWid,
                      screenPos.x(), screenPos.y(), (unsigned)w, (unsigned)h);

    /* Set WM_NORMAL_HINTS to prevent the app from resizing itself */
    XSizeHints hints;
    hints.flags = PMinSize | PMaxSize | PPosition | PSize;
    hints.min_width = w;  hints.min_height = h;
    hints.max_width = w;  hints.max_height = h;
    hints.x = screenPos.x(); hints.y = screenPos.y();
    hints.width = w; hints.height = h;
    XSetWMNormalHints(dpy, (Window)m_embeddedWid, &hints);

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

    /* Clock */
    m_clock = new ClockWidget(this);
    connect(m_clock, &ClockWidget::closeRequested, qApp, &QApplication::quit);
    root->addWidget(m_clock);

    /* Tab bar */
    m_tabBar = new AppTabBar(this);
    connect(m_tabBar, &AppTabBar::tabClicked, this, &MainPanel::onTabClicked);
    connect(m_tabBar, &AppTabBar::tabCloseRequested, this, &MainPanel::onTabCloseRequested);
    root->addWidget(m_tabBar);

    /* Content stack */
    m_stack = new QStackedWidget(this);
    m_stack->setAttribute(Qt::WA_TranslucentBackground);

    /* Graph — always present, default tab */
    m_graph = new GraphWidget(m_stack);
    m_stack->addWidget(m_graph);     /* index 0 */

    /* App slots */
    m_browser = new EmbedSlot("Browser",
                    {"SwordFish","zen-browser","firefox",
                     "chromium","google-chrome-stable"}, m_stack);
    m_fm      = new EmbedSlot("Files",
                    {"swordfm","nautilus","thunar","pcmanfm"}, m_stack);
    m_terminal = new EmbedSlot("Terminal",
                    {"ghostty","alacritty","kitty","xterm"}, m_stack);

    m_stack->addWidget(m_browser);   /* index 1 */
    m_stack->addWidget(m_fm);        /* index 2 */
    m_stack->addWidget(m_terminal);  /* index 3 */
    m_stack->setCurrentIndex(0);

    root->addWidget(m_stack, 1);

    /* Graph tab is always present */
    m_tabs.append({"Graph", "◈", 0});
    m_tabBar->addTab("Graph", "◈", 0);
    m_tabBar->setActiveTab(0);
}

int MainPanel::findTabSlot(const QString &name) const {
    for (int i = 0; i < m_tabs.size(); ++i)
        if (m_tabs[i].name.toLower() == name.toLower()) return i;
    return -1;
}

void MainPanel::addPanelTab(const QString &name, const QString &icon, int stackIndex) {
    int existing = findTabSlot(name);
    if (existing >= 0) {
        m_tabBar->setActiveTab(m_tabs[existing].stackIndex);
        m_stack->setCurrentIndex(m_tabs[existing].stackIndex);
        return;
    }

    m_tabs.append({name, icon, stackIndex});
    m_tabBar->addTab(name, icon, stackIndex);
    m_tabBar->setActiveTab(stackIndex);
    m_stack->setCurrentIndex(stackIndex);

    if (stackIndex == 1 && !m_browser->isRunning()) m_browser->launch();
    if (stackIndex == 2 && !m_fm->isRunning())      m_fm->launch();
    if (stackIndex == 3 && !m_terminal->isRunning()) m_terminal->launch();
}

void MainPanel::showPanel(const QString &id) {
    if (id == "browser")       addPanelTab("Browser",  "🌐", 1);
    else if (id == "fm")       addPanelTab("Files",    "📁", 2);
    else if (id == "terminal") addPanelTab("Terminal", "⌨",  3);
    else if (id == "graph") {
        m_tabBar->setActiveTab(0);
        m_stack->setCurrentIndex(0);
    }
}

void MainPanel::onTabClicked(int index) {
    m_tabBar->setActiveTab(index);
    m_stack->setCurrentIndex(index);
}

void MainPanel::onTabCloseRequested(int stackIndex) {
    /* Never close the graph tab */
    if (stackIndex == 0) return;

    for (int i = 0; i < m_tabs.size(); ++i) {
        if (m_tabs[i].stackIndex == stackIndex) {
            /* Kill the app process */
            if (stackIndex == 1 && m_browser->isRunning()) m_browser->closeApp();
            if (stackIndex == 2 && m_fm->isRunning())      m_fm->closeApp();
            if (stackIndex == 3 && m_terminal->isRunning()) m_terminal->closeApp();

            m_tabs.remove(i);
            m_tabBar->removeTab(stackIndex);
            break;
        }
    }

    /* Always switch back to graph after closing an app */
    m_tabBar->setActiveTab(0);
    m_stack->setCurrentIndex(0);
}
