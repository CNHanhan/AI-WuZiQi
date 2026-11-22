#pragma once

// RankDialog — 排行榜弹窗（ELO / 胜场），QTableWidget 展示
// 请求意图通过 signals 抛出，数据通过 public 方法回填。

#include <QDialog>
#include <QString>
#include <QVector>

class QTableWidget;
class QComboBox;
class QPushButton;
class QLabel;

struct RankEntry {
    int     rank = 0;
    QString username;
    int     eloScore = 0;
    int     wins = 0;
    int     losses = 0;
    int     draws = 0;
};

class RankDialog : public QDialog
{
    Q_OBJECT

public:
    explicit RankDialog(QWidget *parent = nullptr);

    void setRankings(const QVector<RankEntry> &entries);

signals:
    // rankType: "elo" / "wins"
    void rankRequested(const QString &rankType, int page);

private slots:
    void onTypeChanged(int index);
    void onRefreshClicked();

private:
    void setupUi();
    void refreshTable();

    QTableWidget *table_;
    QComboBox *typeCombo_;
    QPushButton *refreshBtn_;
    QLabel *pageHint_;

    QVector<RankEntry> entries_;
    int currentPage_ = 1;
};
