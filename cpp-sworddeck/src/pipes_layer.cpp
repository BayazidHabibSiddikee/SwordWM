#include "pipes_layer.h"
#include <QWidget>
#include <QFont>
#include <QRandomGenerator>
#include <algorithm>

static const QChar PIPE_H      = QChar(0x2501);
static const QChar PIPE_V      = QChar(0x2503);
static const QChar PIPE_TL     = QChar(0x250F);
static const QChar PIPE_TR     = QChar(0x2513);
static const QChar PIPE_BL     = QChar(0x2517);
static const QChar PIPE_BR     = QChar(0x251B);
static const QChar PIPE_CROSS  = QChar(0x2533);

const QVector<QColor> PipesLayer::PIPE_COLORS = {
    QColor(97, 175, 239),
    QColor(152, 195, 121),
    QColor(229, 192, 123),
    QColor(198, 120, 221),
    QColor(86, 182, 194),
    QColor(224, 108, 117),
};

static inline int randInt(int lo, int hi) {
    return lo + QRandomGenerator::global()->bounded(hi - lo + 1);
}

static inline bool chance(double p) {
    return QRandomGenerator::global()->generateDouble() < p;
}

PipesLayer::PipesLayer(QWidget *parent, int cell, int interval, int maxAlpha)
    : QObject(parent), m_widget(parent), m_cellW(cell), m_cellH(cell), m_maxAlpha(maxAlpha), m_frame(0)
{
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &PipesLayer::tick);
    timer->start(interval);
}

int PipesLayer::cols() const { return std::max(1, m_widget->width() / m_cellW); }
int PipesLayer::rows() const { return std::max(1, m_widget->height() / m_cellH); }

void PipesLayer::tick() {
    m_frame++;
    int c = cols(), r = rows();

    // Spawn new pipe
    if (m_pipes.size() < 8 && chance(0.15)) {
        Pipe p;
        p.col = randInt(0, c - 1);
        p.row = randInt(0, r - 1);
        p.dir = chance(0.5) ? 'h' : 'v';
        p.color = PIPE_COLORS[randInt(0, PIPE_COLORS.size() - 1)];
        p.len = 0;
        m_pipes.append(p);
    }

    // Age grid cells, remove expired
    QList<QPoint> dead;
    for (auto it = m_grid.begin(); it != m_grid.end(); ++it) {
        if (it->age <= 0) {
            dead.append(it.key());
        } else {
            it->age--;
        }
    }
    for (const auto &k : dead)
        m_grid.remove(k);

    // Advance pipes
    QList<int> deadPipes;
    for (int i = 0; i < m_pipes.size(); i++) {
        Pipe &pipe = m_pipes[i];
        QChar ch = (pipe.dir == 'h') ? PIPE_H : PIPE_V;
        m_grid[{pipe.col, pipe.row}] = {ch, pipe.color, 60};
        pipe.len++;

        // Random direction change
        if (chance(0.12)) {
            char oldDir = pipe.dir;
            pipe.dir = (pipe.dir == 'h') ? 'v' : 'h';
            QChar corner;
            if (oldDir == 'h' && pipe.dir == 'v')
                corner = PIPE_TL;
            else if (oldDir == 'v' && pipe.dir == 'h')
                corner = PIPE_BR;
            else
                corner = PIPE_CROSS;
            m_grid[{pipe.col, pipe.row}] = {corner, pipe.color, 60};
        }

        // Move
        if (pipe.dir == 'h')
            pipe.col = (pipe.col + (chance(0.5) ? -1 : 1) + c) % c;
        else
            pipe.row = (pipe.row + (chance(0.5) ? -1 : 1) + r) % r;

        // Kill pipes that exceeded random length
        if (pipe.len > randInt(20, 60))
            deadPipes.append(i);
    }

    // Remove dead pipes in reverse order to preserve indices
    for (int i = deadPipes.size() - 1; i >= 0; i--)
        m_pipes.removeAt(deadPipes[i]);

    // Periodic full clear
    if (m_frame % 400 == 0)
        m_grid.clear();

    m_widget->update();
}

void PipesLayer::paint(QPainter &p) {
    QFont font("monospace", 11);
    p.setFont(font);
    for (auto it = m_grid.begin(); it != m_grid.end(); ++it) {
        int alpha = std::min(m_maxAlpha, it->age * 2);
        QColor col(it->color.red(), it->color.green(), it->color.blue(), alpha);
        p.setPen(col);
        p.drawText(it.key().x() * m_cellW, it.key().y() * m_cellH + m_cellH - 2, QString(it->ch));
    }
}
