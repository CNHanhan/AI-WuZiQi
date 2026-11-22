#include "GomokuBoardWidget.h"

#include <QPainter>
#include <QPen>
#include <QBrush>
#include <QColor>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QPalette>
#include <QtMath>
#include <QTimer>
#include <QtGlobal>

GomokuBoardWidget::GomokuBoardWidget(QWidget *parent)
    : QWidget(parent)
    , lastMove_(-1, -1)
    , boardSkin_("classic")
    , stoneSkin_("classic")
    , interactive_(true)
    , effectTimer_(new QTimer(this))
    , effectProgress_(1.0)
    , effectPos_(-1, -1)
{
    clearBoard();
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor("#c99a5b"));
    setPalette(pal);

    effectTimer_->setInterval(40);           // ~25fps
    connect(effectTimer_, &QTimer::timeout, this, &GomokuBoardWidget::onEffectTick);

    // 一个合理的默认尺寸；用 sizePolicy 让它可伸缩同时保正方形倾向
    setMinimumSize(360, 360);
}

// ---------------- 棋盘数据 ----------------
void GomokuBoardWidget::setCell(int x, int y, int color)
{
    if (x < 0 || x >= AIWZQ::BOARD_SIZE || y < 0 || y >= AIWZQ::BOARD_SIZE)
        return;
    grid_[x][y] = color;
    update();
}

int GomokuBoardWidget::cellAt(int x, int y) const
{
    if (x < 0 || x >= AIWZQ::BOARD_SIZE || y < 0 || y >= AIWZQ::BOARD_SIZE)
        return AIWZQ::Color::EMPTY;
    return grid_[x][y];
}

void GomokuBoardWidget::setBoard(const int cells[AIWZQ::BOARD_SIZE][AIWZQ::BOARD_SIZE])
{
    for (int i = 0; i < AIWZQ::BOARD_SIZE; ++i)
        for (int j = 0; j < AIWZQ::BOARD_SIZE; ++j)
            grid_[i][j] = cells[i][j];
    update();
}

void GomokuBoardWidget::clearBoard()
{
    for (int i = 0; i < AIWZQ::BOARD_SIZE; ++i)
        for (int j = 0; j < AIWZQ::BOARD_SIZE; ++j)
            grid_[i][j] = AIWZQ::Color::EMPTY;
    lastMove_ = QPoint(-1, -1);
    effectProgress_ = 1.0;
    effectPos_ = QPoint(-1, -1);
    update();
}

// ---------------- 显示状态 ----------------
void GomokuBoardWidget::setLastMove(int x, int y)
{
    lastMove_ = QPoint(x, y);
    update();
}

// ---------------- 皮肤 ----------------
void GomokuBoardWidget::setBoardSkin(const QString &skin)
{
    boardSkin_ = skin;
    update();
}

void GomokuBoardWidget::setStoneSkin(const QString &skin)
{
    stoneSkin_ = skin;
    update();
}

// ---------------- 特效 ----------------
void GomokuBoardWidget::playPlaceEffect(int x, int y)
{
    effectPos_ = QPoint(x, y);
    effectProgress_ = 0.0;
    if (!effectTimer_->isActive())
        effectTimer_->start();
}

void GomokuBoardWidget::onEffectTick()
{
    effectProgress_ += 0.12;
    if (effectProgress_ >= 1.0) {
        effectProgress_ = 1.0;
        effectTimer_->stop();
    }
    update();
}

// ---------------- 几何计算 ----------------
qreal GomokuBoardWidget::margin() const
{
    // 四周留 6% 的边距给网格
    return qMin(width(), height()) * 0.06;
}

qreal GomokuBoardWidget::cellSize() const
{
    // 15 个格子之间有 14 段间距
    return (qMin(width(), height()) - 2 * margin()) / (AIWZQ::BOARD_SIZE - 1);
}

QPointF GomokuBoardWidget::cellCenter(int x, int y) const
{
    qreal m = margin();
    return QPointF(m + x * cellSize(), m + y * cellSize());
}

QPoint GomokuBoardWidget::pointToCell(const QPointF &pos) const
{
    qreal m = margin();
    qreal cs = cellSize();
    if (cs <= 0)
        return QPoint(-1, -1);
    int x = qRound((pos.x() - m) / cs);
    int y = qRound((pos.y() - m) / cs);
    if (x < 0 || x >= AIWZQ::BOARD_SIZE || y < 0 || y >= AIWZQ::BOARD_SIZE)
        return QPoint(-1, -1);
    return QPoint(x, y);
}

// ---------------- 事件 ----------------
void GomokuBoardWidget::mousePressEvent(QMouseEvent *event)
{
    if (!interactive_ || !event)
        return;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QPointF pos = event->position();
#else
    QPointF pos = event->pos();
#endif
    QPoint cell = pointToCell(pos);
    if (cell.x() >= 0 && cell.y() >= 0)
        emit clickAt(cell.x(), cell.y());
}

void GomokuBoardWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    update();
}

void GomokuBoardWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    drawBoardBackground(painter);
    drawGridLines(painter);
    drawStarPoints(painter);

    for (int i = 0; i < AIWZQ::BOARD_SIZE; ++i) {
        for (int j = 0; j < AIWZQ::BOARD_SIZE; ++j) {
            if (grid_[i][j] != AIWZQ::Color::EMPTY)
                drawStone(painter, i, j, grid_[i][j]);
        }
    }

    drawLastMoveMarker(painter);
    drawEffect(painter);
}

// ---------------- 绘制流水线 ----------------
void GomokuBoardWidget::drawBoardBackground(QPainter &p)
{
    QColor c;
    if (boardSkin_ == "dark")       c = QColor("#3a3f4a");
    else if (boardSkin_ == "green") c = QColor("#8fbf7f");
    else if (boardSkin_ == "jade")  c = QColor("#7fb3a3");
    else                            c = QColor("#dcb174"); // classic 木色
    p.fillRect(rect(), c);

    // 轻微渐变让木质感更自然
    QLinearGradient g(0, 0, 0, height());
    g.setColorAt(0.0, QColor(255, 255, 255, 28));
    g.setColorAt(1.0, QColor(0, 0, 0, 28));
    p.fillRect(rect(), g);
}

void GomokuBoardWidget::drawGridLines(QPainter &p)
{
    qreal m = margin();
    qreal cs = cellSize();
    QPen pen(QColor("#4a3018"));
    pen.setWidthF(qMax<qreal>(1.0, cs * 0.025));
    p.setPen(pen);
    for (int i = 0; i < AIWZQ::BOARD_SIZE; ++i) {
        qreal off = m + i * cs;
        p.drawLine(QPointF(m, off), QPointF(m + (AIWZQ::BOARD_SIZE - 1) * cs, off));
        p.drawLine(QPointF(off, m), QPointF(off, m + (AIWZQ::BOARD_SIZE - 1) * cs));
    }
}

void GomokuBoardWidget::drawStarPoints(QPainter &p)
{
    // 标准 15 路棋盘的 5 个星位：(3,3)(3,11)(7,7)(11,3)(11,11)
    static const int stars[5][2] = {{3,3},{3,11},{7,7},{11,3},{11,11}};
    qreal r = qMax<qreal>(2.5, cellSize() * 0.07);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#2b1c0e"));
    for (auto &s : stars) {
        QPointF c = cellCenter(s[0], s[1]);
        p.drawEllipse(c, r, r);
    }
}

void GomokuBoardWidget::drawStone(QPainter &p, int x, int y, int color)
{
    QPointF c = cellCenter(x, y);
    qreal r = cellSize() * 0.44;
    QRectF pr(c.x() - r, c.y() - r, 2 * r, 2 * r);

    bool black = (color == AIWZQ::Color::BLACK);
    QColor base;
    if (black) base = (stoneSkin_ == "glass") ? QColor("#2b3a55") : QColor("#1c1c1c");
    else       base = (stoneSkin_ == "glass") ? QColor("#eaf4ff") : QColor("#f7f7f7");

    // 阴影
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 70));
    p.drawEllipse(pr.adjusted(r * 0.08, r * 0.12, r * 0.08, r * 0.12));

    // 主体：径向渐变模拟玻璃/塑料质感
    QRadialGradient grad(c.x() - r * 0.35, c.y() - r * 0.35, r * 1.4);
    grad.setColorAt(0.0, base.lighter(black ? 130 : 108));
    grad.setColorAt(0.6, base);
    grad.setColorAt(1.0, base.darker(black ? 160 : 115));
    p.setBrush(grad);
    p.drawEllipse(pr);

    // 高光
    p.setBrush(QColor(255, 255, 255, black ? 90 : 150));
    p.drawEllipse(QRectF(c.x() - r * 0.55, c.y() - r * 0.6, r * 0.7, r * 0.45));
}

void GomokuBoardWidget::drawLastMoveMarker(QPainter &p)
{
    if (lastMove_.x() < 0 || lastMove_.y() < 0)
        return;
    QPointF c = cellCenter(lastMove_.x(), lastMove_.y());
    qreal r = cellSize() * 0.16;
    QPen pen(QColor("#e53935"));
    pen.setWidthF(qMax<qreal>(1.5, cellSize() * 0.05));
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(c, r, r);
}

void GomokuBoardWidget::drawEffect(QPainter &p)
{
    if (effectProgress_ >= 1.0 || effectPos_.x() < 0 || effectPos_.y() < 0)
        return;
    QPointF c = cellCenter(effectPos_.x(), effectPos_.y());
    // 扩散圆环：半径随进度增大，透明度递减
    qreal rMax = cellSize() * 1.3;
    qreal r = rMax * effectProgress_;
    int alpha = qMax(0, int(160 * (1.0 - effectProgress_)));
    QPen pen(QColor(255, 236, 120, alpha));
    pen.setWidthF(qMax<qreal>(1.0, cellSize() * 0.08 * (1.0 - effectProgress_)));
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(c, r, r);
}
