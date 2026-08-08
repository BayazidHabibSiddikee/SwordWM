#pragma once
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QString>

/* =========================================================
 * GlavaManager — launches glava via xwinwrap with proper
 * click-through and layering, all from C++ (no shell script).
 *
 * Key flags:
 *   -ni  = ignore input (empty X Shape — real click-through)
 *   -b   = below all windows (real stacking, not EWMH hint)
 *   -ov  = override redirect (WM never touches it)
 *   -fdt = desktop type window
 *
 * Without xwinwrap: bare glava with X Shape click-through.
 * ========================================================= */
class GlavaManager : public QObject {
    Q_OBJECT
public:
    explicit GlavaManager(QObject *parent = nullptr);
    ~GlavaManager();

    bool isRunning() const;
    void toggle();

signals:
    void stateChanged(bool running);

private slots:
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    void start();
    void stop();

    QProcess *m_proc = nullptr;
};
