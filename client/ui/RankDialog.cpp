#include "RankDialog.h"

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableWidget>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QHeaderView>
#include <QStringList>

RankDialog::RankDialog(QWidget *parent)
    : QDialog(parent)
{
    setupUi();
}

void RankDialog::setupUi()
{
    table_ = new QTableWidget(0, 6, this);
    table_->setHorizontalHeaderLabels(
        QStringList() << QStringLiteral("排名") << QStringLiteral("用户名")
                      << QStringLiteral("ELO") << QStringLiteral("胜")
                      << QStringLiteral("负") << QStringLiteral("平"));
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);

    typeCombo_ = new QComboBox(this);
    typeCombo_->addItem(QStringLiteral("ELO 积分"), QStringLiteral("elo"));
    typeCombo_->addItem(QStringLiteral("胜场"), QStringLiteral("wins"));
    refreshBtn_ = new QPushButton(QStringLiteral("刷新"), this);
    pageHint_ = new QLabel(QStringLiteral(""), this);

    QHBoxLayout *top = new QHBoxLayout;
    top->addWidget(new QLabel(QStringLiteral("排行类型："), this));
    top->addWidget(typeCombo_);
    top->addWidget(refreshBtn_);
    top->addStretch(1);
    top->addWidget(pageHint_);

    QVBoxLayout *main = new QVBoxLayout(this);
    main->addLayout(top);
    main->addWidget(table_, 1);

    setWindowTitle(QStringLiteral("排行榜 - AI-WuZiQi"));
    resize(620, 440);

    connect(typeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &RankDialog::onTypeChanged);
    connect(refreshBtn_, &QPushButton::clicked, this, &RankDialog::onRefreshClicked);
}

void RankDialog::setRankings(const QVector<RankEntry> &entries)
{
    entries_ = entries;
    refreshTable();
}

void RankDialog::refreshTable()
{
    table_->setRowCount(entries_.size());
    for (int i = 0; i < entries_.size(); ++i) {
        const RankEntry &e = entries_[i];
        table_->setItem(i, 0, new QTableWidgetItem(QString::number(e.rank)));
        table_->setItem(i, 1, new QTableWidgetItem(e.username));
        table_->setItem(i, 2, new QTableWidgetItem(QString::number(e.eloScore)));
        table_->setItem(i, 3, new QTableWidgetItem(QString::number(e.wins)));
        table_->setItem(i, 4, new QTableWidgetItem(QString::number(e.losses)));
        table_->setItem(i, 5, new QTableWidgetItem(QString::number(e.draws)));
    }
    pageHint_->setText(QStringLiteral("共 %1 条").arg(entries_.size()));
}

void RankDialog::onTypeChanged(int)
{
    currentPage_ = 1;
    emit rankRequested(typeCombo_->currentData().toString(), currentPage_);
}

void RankDialog::onRefreshClicked()
{
    emit rankRequested(typeCombo_->currentData().toString(), currentPage_);
}
