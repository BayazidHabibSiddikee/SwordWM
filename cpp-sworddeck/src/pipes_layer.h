#ifndef PIPES_LAYER_H
#define PIPES_LAYER_H

#include <QObject>
#include <QPainter>
#include <QTimer>
#include <QColor>
#include <QHash>
#include <QPoint>
#include <QVector>

struct Pipe {
    int col, row;
    char dir;   // 'h' or 'v'
    QColor color;
    int len;
};

class PipesLayer : public QObject {
    Q_OBJECT
public:
    explicit PipesLayer(QWidget *parent, int cell = 16, int interval = 80, int maxAlpha = 140);

    void paint(QPainter &p);
    int cols() const;
    int rows() const;

private slots:
    void tick();

private:
    QWidget *m_widget;
    int m_cellW, m_cellH;
    int m_maxAlpha;
    int m_frame;

    struct GridCell { QChar ch; QColor color; int age; };
    QHash<QPoint, GridCell> m_grid;
    QVector<Pipe> m_pipes;

    static const QVector<QColor> PIPE_COLORS;
};

#endif // PIPES_LAYER_H
