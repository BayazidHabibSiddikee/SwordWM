#include "bottom_bar.h"

#include <QPainter>
#include <QPainterPath>
#include <QFont>
#include <QFontMetrics>
#include <QFile>
#include <QProcess>
#include <QTimer>
#include <QMouseEvent>
#include <QTouchEvent>
#include <QDateTime>
#include <QRegularExpression>
#include <sys/statvfs.h>
#include <unistd.h>
#include <cmath>

/* ── Xlib for EWMH workspace queries ─────────────────────── */
#include <X11/Xlib.h>
#include <X11/Xatom.h>

static const QColor CYAN(97, 175, 239);
static const QColor GREEN(152, 195, 121);
static const QColor AMBER(229, 192, 123);
static const QColor RED(224, 108, 117);
static const QColor DIM(62, 68, 81);
static const QColor WHITE(171, 178, 191);
static const QColor BG(40, 44, 52, 255);
static const QColor WS_BG(62, 68, 81, 255);

/* ── System stats (unchanged — all WM-agnostic) ──────────── */

static int cpu() {
    QFile f("/proc/stat");
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return 0;
    QString line = QString::fromLocal8Bit(f.readLine());
    QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    if (parts.size() < 8) return 0;
    long long total = 0;
    for (int i = 1; i < 8; i++) total += parts[i].toLongLong();
    long long idle = parts[4].toLongLong();
    static long long pt = 0, pi = 0;
    long long dt = total - pt, di = idle - pi;
    pt = total; pi = idle;
    return dt ? (int)(100 * (1.0 - (double)di / dt)) : 0;
}

static int memPct() {
    QFile f("/proc/meminfo");
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return 0;
    QMap<QString, long> info;
    QByteArray line;
    while (f.readLineInto(&line)) {
        int colon = line.indexOf(':');
        if (colon < 0) continue;
        QString key = QString::fromLocal8Bit(line.constData(), colon).trimmed();
        QString valStr = QString::fromLocal8Bit(line.constData() + colon + 1).trimmed().split(' ')[0];
        info[key] = valStr.toLong();
    }
    if (info["MemTotal"] == 0) return 0;
    return (int)(100.0 * (info["MemTotal"] - info["MemAvailable"]) / info["MemTotal"]);
}

static void memDetails(double &usedGB, double &totalGB) {
    usedGB = 0; totalGB = 1;
    QFile f("/proc/meminfo");
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QMap<QString, long> info;
    QByteArray line;
    while (f.readLineInto(&line)) {
        int colon = line.indexOf(':');
        if (colon < 0) continue;
        QString key = QString::fromLocal8Bit(line.constData(), colon).trimmed();
        QString valStr = QString::fromLocal8Bit(line.constData() + colon + 1).trimmed().split(' ')[0];
        info[key] = valStr.toLong();
    }
    totalGB = info["MemTotal"]                            / 1024.0 / 1024.0;
    usedGB  = (info["MemTotal"] - info["MemAvailable"]) / 1024.0 / 1024.0;
}

static int diskPct() {
    struct statvfs st;
    if (statvfs("/", &st) != 0) return 0;
    return (int)(100.0 * (1.0 - (double)st.f_bavail / st.f_blocks));
}

static QString netSpeed() {
    static qint64 prevRx = 0, prevTx = 0;
    static double prevTime = 0;
    qint64 rx = 0, tx = 0;
    QFile f("/proc/net/dev");
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        f.readLine(); f.readLine();
        while (!f.atEnd()) {
            QString line = QString::fromLocal8Bit(f.readLine());
            auto parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
            if (parts.size() > 9 && parts[0] != "lo:") {
                rx += parts[1].toLongLong();
                tx += parts[9].toLongLong();
            }
        }
    }
    double now = QDateTime::currentMSecsSinceEpoch() / 1000.0;
    double dt = qMax(0.1, now - prevTime);
    int down = qMax(0, (int)((rx - prevRx) / dt / 1024));
    int up = qMax(0, (int)((tx - prevTx) / dt / 1024));
    prevRx = rx; prevTx = tx; prevTime = now;
    return QString("↓%1 ↑%2 KB/s").arg(down).arg(up);
}

static QString music() {
    QProcess proc;
    proc.start("playerctl", {"metadata", "--format", "♫ {{artist}} – {{title}}"});
    if (proc.waitForFinished(500)) {
        QString out = QString::fromLocal8Bit(proc.readAllStandardOutput()).trimmed();
        return out.length() > 45 ? out.left(45) + "…" : (out.isEmpty() ? "♫ —" : out);
    }
    return "♫ —";
}

static QString uptimeStr() {
    QFile f("/proc/uptime");
    if (f.open(QIODevice::ReadOnly)) {
        double secs = f.readAll().split(' ')[0].toDouble();
        return QString("↑ %1h %2m").arg((int)(secs / 3600)).arg((int)(fmod(secs, 3600) / 60));
    }
    return "↑ ?";
}

static QString network() {
    QString name = "—", icon = "🌐";
    QProcess proc;
    proc.start("nmcli", {"-t", "-f", "TYPE,STATE,CONNECTION", "device"});
    if (proc.waitForFinished(500)) {
        QStringList lines = QString::fromLocal8Bit(proc.readAllStandardOutput()).split('\n');
        for (const auto &line : lines) {
            QStringList parts = line.split(':');
            if (parts.size() >= 3 && parts[1] == "connected" &&
                !parts[2].isEmpty() && parts[2] != "--") {
                if (parts[0] == "wifi") { name = parts[2]; icon = "📶"; break; }
                else { name = parts[2]; }
            }
        }
    }
    QString ip = "?";
    QProcess ipProc;
    ipProc.start("ip", {"-4", "-o", "addr", "show", "scope", "global"});
    if (ipProc.waitForFinished(500)) {
        QStringList lines = QString::fromLocal8Bit(ipProc.readAllStandardOutput()).split('\n');
        if (!lines.isEmpty()) {
            QStringList parts = lines[0].split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
            if (parts.size() >= 4) ip = parts[3].split('/')[0];
        }
    }
    return QString("%1 %2  %3").arg(icon).arg(name).arg(ip);
}

static QPair<int, QString> battery() {
    QFile cap("/sys/class/power_supply/BAT0/capacity");
    QFile stat("/sys/class/power_supply/BAT0/status");
    if (cap.open(QIODevice::ReadOnly) && stat.open(QIODevice::ReadOnly)) {
        int pct = QString::fromLocal8Bit(cap.readAll()).trimmed().toInt();
        QString s = QString::fromLocal8Bit(stat.readAll()).trimmed();
        QString icon = (s == "Charging") ? "⚡" : "🔋";
        return {pct, QString("%1 %2%").arg(icon).arg(pct)};
    }
    return {100, ""};
}

static QString volume() {
    QProcess proc;
    proc.start("pactl", {"get-sink-volume", "@DEFAULT_SINK@"});
    if (proc.waitForFinished(500)) {
        QString out = QString::fromLocal8Bit(proc.readAllStandardOutput());
        QString vol = out.split('%')[0].split('/').last().trimmed();
        QProcess mute;
        mute.start("pactl", {"get-sink-mute", "@DEFAULT_SINK@"});
        bool isMuted = mute.waitForFinished(500) &&
                       QString::fromLocal8Bit(mute.readAllStandardOutput()).contains("yes");
        return isMuted ? QString("🔇 %1%").arg(vol) : QString("🔊 %1%").arg(vol);
    }
    return "";
}

/*
 * Read the title of the currently focused window via EWMH:
 *   _NET_ACTIVE_WINDOW  → Window XID of focused window (on root)
 *   _NET_WM_NAME        → UTF-8 title of that window
 * Falls back to WM_NAME (Latin-1) if _NET_WM_NAME is absent.
 * Returns an empty string if no window is focused or on any error.
 */
static QString activeWindowTitle() {
    Display *dpy = XOpenDisplay(nullptr);
    if (!dpy) return {};

    Window root = DefaultRootWindow(dpy);

    /* get _NET_ACTIVE_WINDOW */
    Atom activeAtom = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
    Atom actual_type;
    int  actual_format;
    unsigned long n_items, bytes_after;
    unsigned char *prop = nullptr;

    Window focused = None;
    if (XGetWindowProperty(dpy, root, activeAtom, 0, 1, False, XA_WINDOW,
                           &actual_type, &actual_format,
                           &n_items, &bytes_after, &prop) == Success && prop) {
        focused = *(Window *)prop;
        XFree(prop);
    }

    if (focused == None || focused == root) {
        XCloseDisplay(dpy);
        return {};
    }

    /* try _NET_WM_NAME (UTF-8) first */
    QString title;
    Atom utf8Atom    = XInternAtom(dpy, "UTF8_STRING",   False);
    Atom nameAtom    = XInternAtom(dpy, "_NET_WM_NAME",  False);
    prop = nullptr;

    if (XGetWindowProperty(dpy, focused, nameAtom, 0, 1024, False, utf8Atom,
                           &actual_type, &actual_format,
                           &n_items, &bytes_after, &prop) == Success && prop) {
        title = QString::fromUtf8((const char *)prop);
        XFree(prop);
    }

    /* fall back to WM_NAME (Latin-1 XA_STRING) */
    if (title.isEmpty()) {
        prop = nullptr;
        if (XGetWindowProperty(dpy, focused, XA_WM_NAME, 0, 1024, False, XA_STRING,
                               &actual_type, &actual_format,
                               &n_items, &bytes_after, &prop) == Success && prop) {
            title = QString::fromLatin1((const char *)prop);
            XFree(prop);
        }
    }

    XCloseDisplay(dpy);

    /* truncate long titles */
    const int MAX = 50;
    if (title.length() > MAX) title = title.left(MAX) + "…";
    return title;
}

/* ── EWMH workspace helpers (replaces i3-msg entirely) ───── */

/*
 * Read a cardinal property from the root window.
 * Returns -1 on failure.
 */
static long ewmhGetCardinal(Display *dpy, Window root, const char *atom_name) {
    Atom atom = XInternAtom(dpy, atom_name, False);
    Atom actual_type;
    int  actual_format;
    unsigned long n_items, bytes_after;
    unsigned char *prop = nullptr;

    if (XGetWindowProperty(dpy, root, atom, 0, 1, False, XA_CARDINAL,
                           &actual_type, &actual_format,
                           &n_items, &bytes_after, &prop) == Success && prop) {
        long val = *(long *)prop;
        XFree(prop);
        return val;
    }
    return -1;
}

/*
 * Query workspaces purely via EWMH root properties:
 *   _NET_NUMBER_OF_DESKTOPS  — how many workspaces exist
 *   _NET_CURRENT_DESKTOP     — which one is active (0-based)
 *   _NET_DESKTOP_NAMES       — UTF-8 names (optional; we fall back to "1"…"9")
 *
 * Works with swordwm, i3, openbox, bspwm — any EWMH WM.
 */
static QVector<Workspace> workspacesEWMH() {
    QVector<Workspace> result;

    Display *dpy = XOpenDisplay(nullptr);
    if (!dpy) return result;

    Window root = DefaultRootWindow(dpy);

    long num     = ewmhGetCardinal(dpy, root, "_NET_NUMBER_OF_DESKTOPS");
    long current = ewmhGetCardinal(dpy, root, "_NET_CURRENT_DESKTOP");

    if (num <= 0) { XCloseDisplay(dpy); return result; }

    /* Try to read desktop names (_NET_DESKTOP_NAMES — UTF-8 string list) */
    QStringList names;
    Atom namesAtom = XInternAtom(dpy, "_NET_DESKTOP_NAMES", False);
    Atom utf8Atom  = XInternAtom(dpy, "UTF8_STRING", False);
    Atom actual_type;
    int  actual_format;
    unsigned long n_items, bytes_after;
    unsigned char *prop = nullptr;

    if (XGetWindowProperty(dpy, root, namesAtom, 0, 4096, False, utf8Atom,
                           &actual_type, &actual_format,
                           &n_items, &bytes_after, &prop) == Success && prop) {
        /* Names are null-separated UTF-8 strings */
        const char *p = (const char *)prop;
        const char *end = p + n_items;
        while (p < end) {
            names << QString::fromUtf8(p);
            p += strlen(p) + 1;
        }
        XFree(prop);
    }

    XCloseDisplay(dpy);

    /* Build workspace list */
    for (long i = 0; i < num; i++) {
        Workspace ws;
        ws.name    = (i < names.size()) ? names[(int)i] : QString::number(i + 1);
        ws.focused = (i == current);
        ws.urgent  = false;   /* _NET_WM_STATE_DEMANDS_ATTENTION per-window; skip for now */
        result.append(ws);
    }
    return result;
}

/*
 * Switch workspace by sending _NET_CURRENT_DESKTOP ClientMessage to root.
 * This is the standard EWMH mechanism — works with swordwm and any EWMH WM.
 */
static void switchWorkspaceEWMH(int index) {
    Display *dpy = XOpenDisplay(nullptr);
    if (!dpy) return;

    Window root  = DefaultRootWindow(dpy);
    Atom   atom  = XInternAtom(dpy, "_NET_CURRENT_DESKTOP", False);

    XEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.type                 = ClientMessage;
    ev.xclient.display      = dpy;
    ev.xclient.window       = root;
    ev.xclient.message_type = atom;
    ev.xclient.format       = 32;
    ev.xclient.data.l[0]    = index;
    ev.xclient.data.l[1]    = CurrentTime;

    XSendEvent(dpy, root, False,
               SubstructureRedirectMask | SubstructureNotifyMask, &ev);
    XFlush(dpy);
    XCloseDisplay(dpy);
}

/* ── BottomBar ────────────────────────────────────────────── */

BottomBar::BottomBar(QWidget *parent)
    : QWidget(parent), m_stats{0,0,0,"","","","","","",100,"",0,1}
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_AcceptTouchEvents);
    setMouseTracking(true);

    auto *t = new QTimer(this);
    connect(t, &QTimer::timeout, this, &BottomBar::refresh);
    t->start(2000);

    auto *tw = new QTimer(this);
    connect(tw, &QTimer::timeout, this, &BottomBar::refreshWorkspaces);
    tw->start(500);

    /* active window title — fast poll */
    auto *tt = new QTimer(this);
    connect(tt, &QTimer::timeout, this, [this]() {
        QString t = activeWindowTitle();
        if (t != m_stats.activeTitle) { m_stats.activeTitle = t; update(); }
    });
    tt->start(500);

    /* physics animation — 60 fps, only runs while something is moving */
    m_physTimer = new QTimer(this);
    m_physTimer->setInterval(16);   /* ~62.5 fps */
    connect(m_physTimer, &QTimer::timeout, this, &BottomBar::physicsStep);

    QTimer::singleShot(100, this, &BottomBar::refresh);
}

void BottomBar::refresh() {
    auto [batPct, batTxt] = battery();
    double usedGB = 0, totalGB = 1;
    memDetails(usedGB, totalGB);
    m_stats = {cpu(), memPct(), diskPct(),
               netSpeed(), music(), uptimeStr(),
               network(), batTxt, volume(), batPct,
               activeWindowTitle(), usedGB, totalGB};
    update();
}

void BottomBar::refreshWorkspaces() {
    auto ws = workspacesEWMH();
    if (ws.size() != m_ws.size() || ws != m_ws) {
        m_ws = ws;
        /* grow/shrink physics array to match */
        while (m_phys.size() < m_ws.size()) m_phys.append(WsPhysics{});
        while (m_phys.size() > m_ws.size()) m_phys.removeLast();
        update();
    }
}

/* =========================================================
 * physicsStep — spring + magnetic simulation at ~60 fps
 *
 * Spring bounce (y):
 *   F = -k*x - damping*v   (damped harmonic oscillator)
 *   k=0.35  damping=0.55
 *
 * Magnetic scale:
 *   Target scale = 1.18 when hovered, 1.0 otherwise
 *   Spring toward target: k=0.22  damping=0.60
 * ========================================================= */
void BottomBar::physicsStep() {
    const double kSpring  = 0.35,  dampSpring  = 0.55;
    const double kMag     = 0.22,  dampMag     = 0.60;
    const double REST_EPS = 0.002; /* threshold to declare "at rest" */

    bool anyMoving = false;

    for (int i = 0; i < m_phys.size(); i++) {
        WsPhysics &ph = m_phys[i];

        /* ── y spring ── */
        double ay = -kSpring * ph.yOff - dampSpring * ph.yVel;
        ph.yVel += ay;
        ph.yOff += ph.yVel;
        if (qAbs(ph.yOff) < REST_EPS && qAbs(ph.yVel) < REST_EPS) {
            ph.yOff = 0; ph.yVel = 0;
        } else {
            anyMoving = true;
        }

        /* ── magnetic scale ── */
        double targetScale = (i == m_hoveredWs) ? 1.18 : 1.0;
        double as = kMag * (targetScale - ph.scale) - dampMag * ph.scaleVel;
        ph.scaleVel += as;
        ph.scale    += ph.scaleVel;
        if (qAbs(ph.scale - targetScale) < REST_EPS &&
            qAbs(ph.scaleVel) < REST_EPS) {
            ph.scale = targetScale; ph.scaleVel = 0;
        } else {
            anyMoving = true;
        }
    }

    update();

    /* Stop timer when everything is at rest to save CPU */
    if (!anyMoving) {
        m_physTimer->stop();
        m_physDirty = false;
    }
}

void BottomBar::mousePressEvent(QMouseEvent *e) {
    int x = (int)e->position().x();
    for (int i = 0; i < m_wsRects.size(); i++) {
        if (m_wsRects[i].first <= x && x <= m_wsRects[i].second) {
            switchWorkspaceEWMH(i);
            /* kick spring downward (positive y = down) */
            if (i < m_phys.size()) {
                m_phys[i].yOff = 4.0;
                m_phys[i].yVel = 2.0;
            }
            if (!m_physTimer->isActive()) m_physTimer->start();
            m_physDirty = true;
            return;
        }
    }
}

void BottomBar::mouseMoveEvent(QMouseEvent *e) {
    int x = (int)e->position().x();
    int hovered = -1;
    for (int i = 0; i < m_wsRects.size(); i++) {
        if (m_wsRects[i].first <= x && x <= m_wsRects[i].second) {
            hovered = i; break;
        }
    }
    if (hovered != m_hoveredWs) {
        m_hoveredWs = hovered;
        if (!m_physTimer->isActive()) m_physTimer->start();
        m_physDirty = true;
        update();
    }
}

void BottomBar::leaveEvent(QEvent *) {
    if (m_hoveredWs != -1) {
        m_hoveredWs = -1;
        if (!m_physTimer->isActive()) m_physTimer->start();
        m_physDirty = true;
        update();
    }
}

bool BottomBar::event(QEvent *e) {
    if (e->type() == QEvent::TouchBegin ||
        e->type() == QEvent::TouchUpdate ||
        e->type() == QEvent::TouchEnd)
    {
        auto *te = static_cast<QTouchEvent *>(e);
        const auto &points = te->points();

        if (e->type() == QEvent::TouchBegin) {
            m_touchCount = points.size();
            if (m_touchCount == 3) {
                m_swipeStart = points[0].position();
                m_swiping    = true;
            }
        } else if (e->type() == QEvent::TouchUpdate && m_swiping) {
            /* keep updating touch count in case fingers are lifted mid-gesture */
            m_touchCount = qMax(m_touchCount, (int)points.size());
        } else if (e->type() == QEvent::TouchEnd && m_swiping) {
            m_swiping = false;
            if (m_touchCount >= 3 && !points.isEmpty()) {
                qreal dx = points[0].position().x() - m_swipeStart.x();
                /* require at least 40 px horizontal movement */
                if (qAbs(dx) > 40) {
                    /* find current workspace index */
                    int cur = -1;
                    for (int i = 0; i < m_ws.size(); i++) {
                        if (m_ws[i].focused) { cur = i; break; }
                    }
                    if (cur >= 0) {
                        int next = (dx < 0)
                            ? qMin(cur + 1, m_ws.size() - 1)   /* swipe left  → next */
                            : qMax(cur - 1, 0);                 /* swipe right → prev */
                        if (next != cur) switchWorkspaceEWMH(next);
                    }
                }
            }
            m_touchCount = 0;
        }
        e->accept();
        return true;
    }
    return QWidget::event(e);
}

void BottomBar::paintEvent(QPaintEvent *) {
    QPainter p(this);
    int W = width(), H = height();
    p.fillRect(rect(), BG);

    QFont f("JetBrains Mono", 10, QFont::Bold);
    p.setFont(f);
    QFontMetrics fm(f);
    int y = H - (H - fm.ascent()) / 2 - 2;

    /* ── Workspaces ──────────────────────────────────────── */
    m_wsRects.clear();
    int x = 6;
    for (int i = 0; i < m_ws.size(); i++) {
        const auto &ws = m_ws[i];
        int tw = fm.horizontalAdvance(ws.name);
        int bw = tw + 16;
        int bh = H - 6;
        int by = 3;

        /* physics */
        double sc  = (i < m_phys.size()) ? m_phys[i].scale : 1.0;
        double yof = (i < m_phys.size()) ? m_phys[i].yOff  : 0.0;

        /* Scaled width/height and centered position */
        int sbw = (int)(bw * sc);
        int sbh = (int)(bh * sc);
        int sx  = x + (bw - sbw) / 2;
        int sy  = by + (bh - sbh) / 2 + (int)yof;

        QPainterPath path;
        path.addRoundedRect(sx, sy, sbw, sbh, 4 * sc, 4 * sc);

        bool hovered = (i == m_hoveredWs);

        if (ws.focused) {
            p.fillPath(path, hovered ? CYAN.lighter(115) : CYAN);
            p.setPen(QPen(CYAN.darker(130), 1));
            p.drawPath(path);
            p.setPen(QColor(30, 34, 40));
        } else if (ws.urgent) {
            p.fillPath(path, hovered ? QColor(224,108,117,220) : QColor(224,108,117,180));
            p.setPen(QPen(RED, 1));
            p.drawPath(path);
            p.setPen(QColor(255, 255, 255));
        } else if (hovered) {
            p.fillPath(path, QColor(97, 175, 239, 40));
            p.setPen(QPen(CYAN, 1));
            p.drawPath(path);
            p.setPen(CYAN);
        } else {
            p.fillPath(path, QColor(52, 58, 70, 200));
            p.setPen(QPen(QColor(80, 88, 104), 1));
            p.drawPath(path);
            p.setPen(QColor(100, 110, 130));
        }

        /* Draw text centered in the scaled button */
        p.save();
        if (sc != 1.0) {
            /* scale font size with button */
            QFont sf = f;
            sf.setPointSizeF(f.pointSizeF() * sc);
            p.setFont(sf);
            QFontMetrics sfm(sf);
            int tx = sx + (sbw - sfm.horizontalAdvance(ws.name)) / 2;
            int ty = sy + (sbh + sfm.ascent() - sfm.descent()) / 2;
            p.drawText(tx, ty, ws.name);
        } else {
            p.drawText(x + 8, y + (int)yof, ws.name);
        }
        p.restore();
        p.setFont(f);   /* restore font for next iteration */

        m_wsRects.append({x, x + bw});
        x += bw + 3;
    }
    x += 8;

    /* ── Left / centre stats ─────────────────────────────── */
    QString user = qgetenv("USER");
    QDateTime now = QDateTime::currentDateTime();
    QString sep = "  │  ";

    auto cpuCol = [](int v) { return v >= 80 ? RED : v >= 50 ? AMBER : GREEN; };
    auto memCol = [](int v) { return v >= 80 ? RED : v >= 60 ? AMBER : CYAN; };
    auto batCol = [](int v) { return v <= 20 ? RED : v <= 40 ? AMBER : GREEN; };

    /* Format RAM as "X.XG/Y.YG" — more informative than a bare percent */
    QString ramStr = QString("%1G/%2G")
                     .arg(m_stats.memUsedGB,  0, 'f', 1)
                     .arg(m_stats.memTotalGB, 0, 'f', 1);

    struct Part { QColor c; QString t; };
    QVector<Part> parts = {
        {CYAN,                QString("[%1]  %2").arg(user.toUpper())
                              .arg(now.toString("HH:mm:ss  ddd dd MMM"))},
        {DIM,                 sep},
        {GREEN,               "⚡ CPU: "},
        {cpuCol(m_stats.cpu), QString("%1%").arg(m_stats.cpu)},
        {DIM,                 sep},
        {GREEN,               "🧠 RAM: "},
        {memCol(m_stats.mem), ramStr},
        {DIM,                 sep},
        {GREEN,               "💾 /: "},
        {CYAN,                QString("%1%").arg(m_stats.disk)},
        {DIM,                 sep},
        {CYAN,                m_stats.net},
        {DIM,                 sep},
    };

    /* active window title takes priority; fall back to music */
    if (!m_stats.activeTitle.isEmpty()) {
        parts.append({AMBER,  "▸ " + m_stats.activeTitle});
    } else {
        parts.append({GREEN,  m_stats.music});
    }

    for (const auto &pt : parts) {
        p.setPen(pt.c);
        p.drawText(x, y, pt.t);
        x += fm.horizontalAdvance(pt.t);
    }

    /* ── Right-aligned: wifi · volume · battery · uptime ─── */
    QVector<QPair<QColor, QString>> right;
    if (!m_stats.wifi.isEmpty())   right.append({WHITE,              m_stats.wifi});
    if (!m_stats.vol.isEmpty())    right.append({CYAN,               m_stats.vol});
    if (!m_stats.bat.isEmpty())    right.append({batCol(m_stats.batPct), m_stats.bat});
    right.append({GREEN, m_stats.uptime});

    int rx = W - 10;
    for (int i = right.size() - 1; i >= 0; i--) {
        rx -= fm.horizontalAdvance(right[i].second);
        p.setPen(right[i].first);
        p.drawText(rx, y, right[i].second);
        if (i > 0) {
            rx -= fm.horizontalAdvance(sep);
            p.setPen(DIM);
            p.drawText(rx, y, sep);
        }
    }
    p.end();
}
