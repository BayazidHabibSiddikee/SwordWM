#pragma once
#include <QWidget>
#include <QTimer>
#include <QEvent>
#include <QKeyEvent>
#include <QPushButton>
#include <QLineEdit>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QMap>
#include "pipes_layer.h"

class RightPanel : public QWidget {
    Q_OBJECT
public:
    explicit RightPanel(QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    bool eventFilter(QObject *obj, QEvent *e) override;

private slots:
    void updateStats();
    void checkAppsChanged();
    void filterApps(const QString &text);

private:
    void buildDock();
    void placeDock();
    void grabXFocus();
    void releaseXFocus();
    void launch(const QString &cmd);
    void openPanel(const QString &tid);
    void switchTheme(const QString &name);
    void toggleWifi();
    void chooseNetwork();
    void toggleRedshift();
    void toggleGlava();
    void restartDeck();
    void updateRedshiftLabel();
    void updateGlavaLabel();

    /* Returns y after the section header line */
    int  drawSection(QPainter &p, int x, int y, int w, const QString &label);
    void drawBar(QPainter &p, int x, int y, int bw, int bh,
                 double pct, const QColor &color);
    QColor valColor(double pct) const;

    PipesLayer m_pipes;
    double m_cpu    = 0;
    double m_memUsed  = 0;
    double m_memTotal = 1;
    int m_temp      = 0;
    bool m_hasTemp  = false;
    QList<QPair<QString, QPair<double,double>>> m_procs;
    int m_statsBottom  = 200;
    int m_statsCounter = 0;

    /* Outer scroll area wrapping the entire dock */
    QScrollArea *m_dockScroll = nullptr;
    QWidget     *m_dock       = nullptr;
    QVBoxLayout *m_dockLayout = nullptr;

    /* App list widgets */
    QLineEdit   *m_appSearch  = nullptr;
    QScrollArea *m_appScroll  = nullptr;
    QList<QPair<QPushButton *, QString>> m_appButtons;  /* btn, name_lower */

    /* Settings buttons whose label changes at runtime */
    QPushButton *m_redshiftBtn = nullptr;
    QPushButton *m_glavaBtn    = nullptr;

    /* Color theme switcher */
    QString m_currentTheme = "Dark (default)";
    QMap<QString, QPushButton *> m_themeBtns;

    qint64 m_appsMtime = 0;
    QTimer m_statsTimer;
    QTimer m_appsTimer;
};
