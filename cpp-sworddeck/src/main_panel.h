#pragma once
#include <QWidget>
#include <QTimer>
#include <QPixmap>
#include <QPushButton>
#include <QProcess>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include "pipes_layer.h"

/* ── App slot IDs ────────────────────────────────────────── */
enum class PanelSlot { Graph = 0, Browser, FM, Terminal };

/* =========================================================
 * GraphWidget — draws the animated graph, clock, status bar
 * ========================================================= */
class GraphWidget : public QWidget {
    Q_OBJECT
public:
    explicit GraphWidget(QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;

private slots:
    void loadGraph();
    void editGraph();

private:
    PipesLayer  m_pipes;
    QPixmap     m_graphPixmap;
    int         m_nodes = 0, m_edges = 0;
    QPushButton *m_editBtn = nullptr;
    QPushButton *m_fmBtn   = nullptr;
};

/* =========================================================
 * EmbedSlot — hosts one embedded X11 application.
 *
 * Life cycle:
 *   launch()   — start the process, wait for its window, embed it
 *   popOut()   — un-embed: move the window to WM control, keep process
 *   close()    — terminate process
 *
 * The embed uses QWindow::fromWinId() + createWindowContainer(),
 * which is the official Qt way to host a foreign X11 window.
 * ========================================================= */
class EmbedSlot : public QWidget {
    Q_OBJECT
public:
    explicit EmbedSlot(const QString &name,
                       std::initializer_list<const char *> exeFallbacks,
                       QWidget *parent = nullptr);
    ~EmbedSlot() override;

    void launch();    /* launch + embed                          */
    void popOut();    /* detach window from embed, keep running  */
    bool isRunning() const;

signals:
    void stateChanged();

protected:
    void resizeEvent(QResizeEvent *e) override;

private slots:
    void tryEmbed();  /* called by timer until window is found   */

private:
    void doEmbed(WId wid);
    void rebuildPlaceholder();
    QString findExe() const;
    void forceResizeEmbedded();

    QString              m_name;
    QStringList          m_exeFallbacks;
    QProcess            *m_proc       = nullptr;
    QWidget             *m_container  = nullptr;  /* createWindowContainer result */
    QWidget             *m_placeholder= nullptr;  /* shown while not embedded     */
    QVBoxLayout         *m_layout     = nullptr;
    QTimer              *m_embedTimer = nullptr;  /* polls for window after launch */
    WId                  m_embeddedWid = 0;
    int                  m_embedTries  = 0;
    QPushButton         *m_popOutBtn  = nullptr;
    QPushButton         *m_launchBtn  = nullptr;
};

/* =========================================================
 * MainPanel — stacked: Graph | Browser | FM | Terminal
 * ========================================================= */
class MainPanel : public QWidget {
    Q_OBJECT
public:
    explicit MainPanel(QWidget *parent = nullptr);

public slots:
    /* Called by RightPanel PANELS buttons */
    void showPanel(const QString &id);   /* "graph","browser","fm","terminal" */

private:
    void switchTo(PanelSlot slot);

    QStackedWidget *m_stack    = nullptr;
    GraphWidget    *m_graph    = nullptr;
    EmbedSlot      *m_browser  = nullptr;
    EmbedSlot      *m_fm       = nullptr;
    EmbedSlot      *m_terminal = nullptr;
};
