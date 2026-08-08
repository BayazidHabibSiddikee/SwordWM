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
#include <QVector>

/* =========================================================
 * AppTab — a single browser-like tab in the tab bar
 * ========================================================= */
class AppTab : public QWidget {
    Q_OBJECT
public:
    explicit AppTab(const QString &name, const QString &icon,
                    int index, QWidget *parent = nullptr);

    void setActive(bool active);
    bool isActive() const { return m_active; }
    QString appName() const { return m_name; }
    int appIndex() const { return m_index; }

signals:
    void clicked(int index);
    void closeRequested(int index);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void enterEvent(QEnterEvent *) override;
    void leaveEvent(QEvent *) override;

private:
    QString m_name;
    QString m_icon;
    int     m_index;
    bool    m_active = false;
    bool    m_hovered = false;
};

/* =========================================================
 * AppTabBar — horizontal row of browser-like tabs
 * ========================================================= */
class AppTabBar : public QWidget {
    Q_OBJECT
public:
    explicit AppTabBar(QWidget *parent = nullptr);

    void addTab(const QString &name, const QString &icon, int index);
    void removeTab(int index);
    void setActiveTab(int index);
    int  count() const { return m_tabs.size(); }

signals:
    void tabClicked(int index);
    void tabCloseRequested(int index);

protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;

private:
    void relayout();

    QVector<AppTab *> m_tabs;
    int m_activeIndex = -1;
};

/* =========================================================
 * ClockWidget — large digital clock with date
 * ========================================================= */
class ClockWidget : public QWidget {
    Q_OBJECT
public:
    explicit ClockWidget(QWidget *parent = nullptr);

signals:
    void closeRequested();

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;

private slots:
    void tick();

private:
    QTimer *m_timer;
};

/* =========================================================
 * GraphWidget — shows the graph image + status
 * ========================================================= */
class GraphWidget : public QWidget {
    Q_OBJECT
public:
    explicit GraphWidget(QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *) override;

private slots:
    void loadGraph();

private:
    QPixmap m_graphPixmap;
    int     m_nodes = 0, m_edges = 0;
};

/* =========================================================
 * EmbedSlot — hosts one embedded X11 application.
 * ========================================================= */
class EmbedSlot : public QWidget {
    Q_OBJECT
public:
    explicit EmbedSlot(const QString &name,
                       std::initializer_list<const char *> exeFallbacks,
                       QWidget *parent = nullptr);
    ~EmbedSlot() override;

    void launch();
    void popOut();
    void closeApp();
    bool isRunning() const;
    QString appName() const { return m_name; }

signals:
    void stateChanged();

protected:
    void resizeEvent(QResizeEvent *e) override;

private slots:
    void tryEmbed();

private:
    void doEmbed(WId wid);
    void rebuildPlaceholder();
    QString findExe() const;
    void forceResizeEmbedded();

    QString              m_name;
    QStringList          m_exeFallbacks;
    QProcess            *m_proc       = nullptr;
    QWidget             *m_container  = nullptr;
    QWidget             *m_placeholder= nullptr;
    QVBoxLayout         *m_layout     = nullptr;
    QTimer              *m_embedTimer = nullptr;
    WId                  m_embeddedWid = 0;
    int                  m_embedTries  = 0;
    QPushButton         *m_popOutBtn  = nullptr;
    QPushButton         *m_launchBtn  = nullptr;
};

/* =========================================================
 * MainPanel — browser-like layout:
 *   ┌──────────────────────────────────────┐
 *   │          ClockWidget (time)          │
 *   ├──────────────────────────────────────┤
 *   │   [Graph] [Tab1] [Tab2] ...         │  ← AppTabBar
 *   ├──────────────────────────────────────┤
 *   │                                      │
 *   │          Content (QStackedWidget)    │
 *   │                                      │
 *   └──────────────────────────────────────┘
 * ========================================================= */
class MainPanel : public QWidget {
    Q_OBJECT
public:
    explicit MainPanel(QWidget *parent = nullptr);

public slots:
    void showPanel(const QString &id);

private slots:
    void onTabClicked(int index);
    void onTabCloseRequested(int index);

private:
    int  findTabSlot(const QString &name) const;
    void addPanelTab(const QString &name, const QString &icon, int stackIndex);

    ClockWidget     *m_clock   = nullptr;
    AppTabBar       *m_tabBar  = nullptr;
    QStackedWidget  *m_stack   = nullptr;

    struct TabInfo {
        QString name;
        QString icon;
        int     stackIndex;
    };
    QVector<TabInfo> m_tabs;

    GraphWidget  *m_graph    = nullptr;
    EmbedSlot    *m_browser  = nullptr;
    EmbedSlot    *m_fm       = nullptr;
    EmbedSlot    *m_terminal = nullptr;
};
