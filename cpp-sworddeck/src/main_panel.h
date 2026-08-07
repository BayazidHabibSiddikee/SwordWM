#pragma once
#include <QWidget>
#include <QTimer>
#include <QPixmap>
#include <QPushButton>
#include <QProcess>
#include "pipes_layer.h"

/* ── Tab IDs ─────────────────────────────────────────────── */
enum class Tab { Graph = 0, Browser, Terminal, FM, Count };

class MainPanel : public QWidget {
    Q_OBJECT
public:
    explicit MainPanel(QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;

public slots:
    void openFM();
    void openBrowser();
    void openTerminal();
    void switchTab(Tab tab);   /* switch the active tab without launching */

private slots:
    void loadGraph();
    void editGraph();

private:
    void drawTabBar(QPainter &p, int w, int tabBarY, int tabH);
    void drawGraphTab(QPainter &p, int w, int h, int contentY);
    void drawLauncherTab(QPainter &p, int w, int h, int contentY,
                         Tab tab);
    void launchForTab(Tab tab);

    PipesLayer  m_pipes;
    QPixmap     m_graphPixmap;
    qint64      m_mtime   = 0;
    int         m_nodes   = 0;
    int         m_edges   = 0;
    Tab         m_activeTab = Tab::Graph;

    QPushButton *m_editBtn = nullptr;
    QPushButton *m_fmBtn   = nullptr;

    /* Track running processes for Browser / Terminal / FM tabs */
    QProcess *m_browserProc  = nullptr;
    QProcess *m_terminalProc = nullptr;
    QProcess *m_fmProc       = nullptr;

    /* Cached tab button rects for hit-testing in mousePressEvent */
    QRect m_tabRects[static_cast<int>(Tab::Count)];

    /* Cached Launch/Kill button rect (content area) for hit-testing */
    QRect m_launchBtnRect;
    Tab   m_launchBtnTab = Tab::Graph;  /* which tab the rect belongs to */
};
