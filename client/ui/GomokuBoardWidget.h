#pragma once

// GomokuBoardWidget — 15x15 五子棋棋盘自定义控件
// 只负责显示与鼠标坐标采集，不依赖网络层；落子意图通过 clickAt 信号抛出。

#include <QWidget>
#include <QPoint>
#include <QString>
#include <QTimer>

#include "../../common/include/Types.h"   // AIWZQ::Color / AIWZQ::BOARD_SIZE

class QPaintEvent;
class QMouseEvent;
class QResizeEvent;
class QPainter;

class GomokuBoardWidget : public QWidget
{
    Q_OBJECT

public:
    explicit GomokuBoardWidget(QWidget *parent = nullptr);

    // ---------- 棋盘数据 ----------
    // color: AIWZQ::Color::EMPTY / BLACK / WHITE
    void setCell(int x, int y, int color);
    int  cellAt(int x, int y) const;
    void setBoard(const int cells[AIWZQ::BOARD_SIZE][AIWZQ::BOARD_SIZE]);
    void clearBoard();

    // ---------- 显示状态 ----------
    void setLastMove(int x, int y);              // 传 (-1,-1) 隐藏
    QPoint lastMove() const { return lastMove_; }
    void setInteractive(bool enabled) { interactive_ = enabled; }
    bool isInteractive() const { return interactive_; }

    // ---------- 皮肤切换 ----------
    void setBoardSkin(const QString &skin);
    QString boardSkin() const { return boardSkin_; }
    void setStoneSkin(const QString &skin);
    QString stoneSkin() const { return stoneSkin_; }

    // ---------- 落子特效 ----------
    void playPlaceEffect(int x, int y);

signals:
    void clickAt(int x, int y);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void onEffectTick();

private:
    // 绘制流水线（顺序与客户端设计文档 2.4 一致）
    void drawBoardBackground(QPainter &p);
    void drawGridLines(QPainter &p);
    void drawStarPoints(QPainter &p);
    void drawStone(QPainter &p, int x, int y, int color);
    void drawLastMoveMarker(QPainter &p);
    void drawEffect(QPainter &p);

    qreal cellSize() const;
    qreal margin() const;                 // 网格左上角偏移
    QPointF cellCenter(int x, int y) const;
    QPoint  pointToCell(const QPointF &pos) const;

    int      grid_[AIWZQ::BOARD_SIZE][AIWZQ::BOARD_SIZE];
    QPoint   lastMove_;
    QString  boardSkin_;
    QString  stoneSkin_;
    bool     interactive_;
    QTimer  *effectTimer_;
    qreal    effectProgress_;             // 0..1，>=1 表示无特效
    QPoint   effectPos_;
};
