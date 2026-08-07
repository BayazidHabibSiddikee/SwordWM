#pragma once
#include <QWidget>
#include <QVector>
#include <QPointF>
#include <QTimer>

struct Workspace {
    QString name;
    bool focused;
    bool urgent;

    bool operator==(const Workspace &o) const {
        return name == o.name && focused == o.focused && urgent == o.urgent;
    }
};

/* Per-button physics state */
struct WsPhysics {
    /* Spring bounce (y displacement, velocity) */
    double yOff  = 0.0;
    double yVel  = 0.0;

    /* Magnetic scale (1.0 = normal, up to 1.18 when hovered) */
    double scale    = 1.0;
    double scaleVel = 0.0;
};

class BottomBar : public QWidget {
    Q_OBJECT
public:
    explicit BottomBar(QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void leaveEvent(QEvent *e) override;
    bool event(QEvent *e) override;

private slots:
    void refresh();
    void refreshWorkspaces();
    void physicsStep();   /* 60 fps animation tick */

private:
    struct Stats {
        int cpu, mem, disk;
        QString net, music, uptime, wifi, bat, vol;
        int batPct;
        QString activeTitle;
        double memUsedGB  = 0;
        double memTotalGB = 1;
    };

    Stats m_stats;
    QVector<Workspace>  m_ws;
    QVector<WsPhysics>  m_phys;          /* parallel to m_ws */
    QVector<QPair<int,int>> m_wsRects;   /* painted hit areas */

    int  m_hoveredWs   = -1;
    bool m_physDirty   = false;          /* true while any button is animating */

    /* three-finger swipe */
    int     m_touchCount = 0;
    QPointF m_swipeStart;
    bool    m_swiping    = false;

    QTimer *m_physTimer = nullptr;
};
