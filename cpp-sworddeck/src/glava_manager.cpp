#include "glava_manager.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QDebug>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/extensions/shape.h>
#include <unistd.h>
#include <csignal>

static QString glavaPidFile() {
    return QDir::homePath() + "/.config/animated-wallpaper/glava.pid";
}

static bool isProcessAlive(qint64 pid) {
    return pid > 0 && kill(static_cast<pid_t>(pid), 0) == 0;
}

static qint64 readPidFile(const QString &path) {
    QFile f(path);
    if (f.open(QIODevice::ReadOnly)) {
        qint64 pid = f.readAll().trimmed().toLongLong();
        return (pid > 0 && isProcessAlive(pid)) ? pid : 0;
    }
    return 0;
}

static void writePidFile(const QString &path, qint64 pid) {
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QByteArray::number(pid));
}

static void removePidFile(const QString &path) {
    QFile::remove(path);
}

/* ----------------------------------------------------------
 * getScreenSize — returns "WxH" string from xrandr
 * ---------------------------------------------------------- */
static QString getScreenSize() {
    QProcess p;
    p.start("xrandr", {"--current"});
    if (p.waitForFinished(2000)) {
        QByteArray out = p.readAllStandardOutput();
        for (const QByteArray &line : out.split('\n')) {
            if (line.contains('*')) {
                QByteArray res = line.split(' ')[0].trimmed();
                if (res.contains('x')) return QString::fromLocal8Bit(res);
            }
        }
    }
    return "1920x1080";
}

/* ----------------------------------------------------------
 * setEmptyInputShape — the ONLY real click-through mechanism
 * on X11.  Uses the Shape extension to set an empty input
 * region so the window never intercepts mouse events.
 * ---------------------------------------------------------- */
static void setEmptyInputShape(Window wid) {
    Display *d = XOpenDisplay(nullptr);
    if (!d) return;
    XRectangle empty;
    empty.x = empty.y = empty.width = empty.height = 0;
    XShapeCombineRectangles(d, wid, ShapeInput, 0, 0, &empty, 1, ShapeSet, 0);
    XFlush(d);
    XCloseDisplay(d);
}

/* ----------------------------------------------------------
 * findGlavaWindow — find the xwinwrap window (or fallback
 * to the GLava class window).
 * ---------------------------------------------------------- */
static Window findGlavaWindow() {
    Display *d = XOpenDisplay(nullptr);
    if (!d) return 0;

    /* First try: find xwinwrap's window by enumerating children */
    Window root_ret, parent_ret;
    Window *children = nullptr;
    unsigned int nchildren = 0;
    if (XQueryTree(d, DefaultRootWindow(d), &root_ret, &parent_ret,
                   &children, &nchildren)) {
        for (unsigned int i = 0; i < nchildren; i++) {
            XClassHint ch;
            if (XGetClassHint(d, children[i], &ch)) {
                bool match = (ch.res_name && strcmp(ch.res_name, "xwinwrap") == 0) ||
                             (ch.res_class && strcmp(ch.res_class, "xwinwrap") == 0);
                if (ch.res_name) XFree(ch.res_name);
                if (ch.res_class) XFree(ch.res_class);
                if (match) {
                    Window w = children[i];
                    XFree(children);
                    XCloseDisplay(d);
                    return w;
                }
            }
        }
        if (children) XFree(children);
    }
    XCloseDisplay(d);

    /* Fallback: xdotool search by GLava class */
    QProcess p;
    p.start("xdotool", {"search", "--class", "GLava"});
    if (p.waitForFinished(2000)) {
        QByteArray out = p.readAllStandardOutput().trimmed();
        if (!out.isEmpty()) {
            Window wid = out.split('\n').first().toULongLong(nullptr, 10);
            return wid;
        }
    }
    return 0;
}

/* ----------------------------------------------------------
 * hasXwinwrap — check if xwinwrap is installed
 * ---------------------------------------------------------- */
static bool hasXwinwrap() {
    QProcess p;
    p.start("which", {"xwinwrap"});
    return p.waitForFinished(1000) && p.exitCode() == 0;
}

/* ----------------------------------------------------------
 * GlavaManager implementation
 * ---------------------------------------------------------- */
GlavaManager::GlavaManager(QObject *parent)
    : QObject(parent), m_proc(nullptr)
{
}

GlavaManager::~GlavaManager() {
    stop();
}

bool GlavaManager::isRunning() const {
    return readPidFile(glavaPidFile()) > 0;
}

void GlavaManager::toggle() {
    if (isRunning())
        stop();
    else
        start();
}

void GlavaManager::start() {
    if (m_proc && m_proc->state() != QProcess::NotRunning)
        return;

    /* Kill any stale instances */
    qint64 oldPid = readPidFile(glavaPidFile());
    if (oldPid > 0) kill(static_cast<pid_t>(oldPid), SIGTERM);
    QProcess::execute("pkill", {"-x", "glava"});
    QProcess::execute("pkill", {"xwinwrap"});
    usleep(200000);

    /* Get screen geometry */
    QString res = getScreenSize();
    int sw = res.split('x')[0].toInt();
    int sh = res.split('x')[1].toInt();
    int bottomH = sh * 32 / 1000;
    int gw = sw;
    int gh = sh * 22 / 100;
    int gy = sh - bottomH - gh;

    m_proc = new QProcess(this);
    connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &GlavaManager::onProcessFinished);

    if (hasXwinwrap()) {
        /* xwinwrap handles everything:
         *   -ni  = ignore input (click-through via empty input shape)
         *   -b   = below all windows
         *   -ov  = override redirect (WM never touches it)
         *   -fdt = desktop type window
         *   -st  = skip taskbar
         *   -sp  = skip pager
         */
        QStringList args;
        args << "-g" << QString("%1x%2+0+%3").arg(gw).arg(gh).arg(gy)
             << "-ni"    /* click-through */
             << "-b"     /* below all */
             << "-ov"    /* override redirect */
             << "-fdt"   /* desktop type */
             << "-st"    /* skip taskbar */
             << "-sp"    /* skip pager */
             << "--"
             << "glava" << "-m" << "graph"
             << "-r" << QString("setgeometry 0 0 %1 %2").arg(gw).arg(gh);

        m_proc->start("xwinwrap", args);
        qInfo("[glava] launching via xwinwrap: %s", qPrintable(args.join(' ')));
    } else {
        /* No xwinwrap — bare glava, we apply click-through via X Shape */
        QStringList args;
        args << "-m" << "graph"
             << "-r" << QString("setgeometry 0 %1 %2 %3").arg(gy).arg(gw).arg(gh);
        m_proc->start("glava", args);
        qInfo("[glava] launching bare glava (no xwinwrap)");
    }

    if (m_proc->waitForStarted(3000)) {
        writePidFile(glavaPidFile(), m_proc->processId());
        qInfo("[glava] started (PID %lld)", m_proc->processId());

        /* Apply layer/click-through after window appears */
        auto applyHints = [this]() {
            Window wid = findGlavaWindow();
            if (!wid) {
                /* Retry once */
                QTimer::singleShot(500, this, [this]() {
                    Window wid2 = findGlavaWindow();
                    if (wid2) {
                        setEmptyInputShape(wid2);
                        qInfo("[glava] layer hints applied (retry) to window %lu", wid2);
                    }
                });
                return;
            }
            setEmptyInputShape(wid);
            qInfo("[glava] layer hints applied to window %lu", wid);
        };
        QTimer::singleShot(500, this, applyHints);

        emit stateChanged(true);
    } else {
        qWarning("[glava] failed to start");
        m_proc->deleteLater();
        m_proc = nullptr;
    }
}

void GlavaManager::stop() {
    /* Kill tracked process */
    qint64 pid = readPidFile(glavaPidFile());
    if (pid > 0) {
        kill(static_cast<pid_t>(pid), SIGTERM);
        usleep(100000);
        if (isProcessAlive(pid))
            kill(static_cast<pid_t>(pid), SIGKILL);
    }
    removePidFile(glavaPidFile());

    /* Kill any xwinwrap + glava */
    QProcess::execute("pkill", {"-x", "glava"});
    QProcess::execute("pkill", {"xwinwrap"});

    if (m_proc) {
        m_proc->disconnect();
        if (m_proc->state() != QProcess::NotRunning)
            m_proc->kill();
        m_proc->deleteLater();
        m_proc = nullptr;
    }

    qInfo("[glava] stopped");
    emit stateChanged(false);
}

void GlavaManager::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    qInfo("[glava] process exited (code=%d, status=%d)", exitCode, exitStatus);
    removePidFile(glavaPidFile());
    emit stateChanged(false);
}
