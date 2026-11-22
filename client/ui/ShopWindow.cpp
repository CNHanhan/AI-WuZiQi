#include "ShopWindow.h"

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QSplitter>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QPixmap>
#include <QMessageBox>
#include <QFont>

ShopWindow::ShopWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setupUi();
}

void ShopWindow::setupUi()
{
    QWidget *central = new QWidget(this);
    setCentralWidget(central);

    coinsLabel_ = new QLabel(QStringLiteral("金币：0"), central);
    refreshBtn_ = new QPushButton(QStringLiteral("刷新商品"), central);
    QHBoxLayout *top = new QHBoxLayout;
    top->addWidget(coinsLabel_, 1);
    top->addWidget(refreshBtn_);

    itemList_ = new QListWidget(central);

    filterCombo_ = new QComboBox(central);
    filterCombo_->addItem(QStringLiteral("全部"), QStringLiteral(""));
    filterCombo_->addItem(QStringLiteral("棋盘"), QStringLiteral("board"));
    filterCombo_->addItem(QStringLiteral("棋子"), QStringLiteral("stone"));
    filterCombo_->addItem(QStringLiteral("特效"), QStringLiteral("effect"));
    filterCombo_->addItem(QStringLiteral("形象"), QStringLiteral("avatar"));

    previewLabel_ = new QLabel(QStringLiteral("预览"), central);
    previewLabel_->setAlignment(Qt::AlignCenter);
    previewLabel_->setMinimumSize(220, 180);
    previewLabel_->setStyleSheet(QStringLiteral("border: 1px solid #888; background:#eee;"));

    nameLabel_ = new QLabel(QStringLiteral("商品名称"), central);
    QFont f = nameLabel_->font();
    f.setBold(true);
    f.setPointSize(14);
    nameLabel_->setFont(f);
    descLabel_ = new QLabel(QStringLiteral(""), central);
    descLabel_->setWordWrap(true);
    priceLabel_ = new QLabel(QStringLiteral("价格：0"), central);

    buyBtn_ = new QPushButton(QStringLiteral("购买"), central);
    equipBtn_ = new QPushButton(QStringLiteral("装备"), central);
    QHBoxLayout *actionRow = new QHBoxLayout;
    actionRow->addWidget(buyBtn_);
    actionRow->addWidget(equipBtn_);

    QVBoxLayout *detail = new QVBoxLayout;
    detail->addWidget(filterCombo_);
    detail->addWidget(previewLabel_);
    detail->addWidget(nameLabel_);
    detail->addWidget(descLabel_);
    detail->addWidget(priceLabel_);
    detail->addStretch(1);
    detail->addLayout(actionRow);

    QSplitter *splitter = new QSplitter(central);
    splitter->addWidget(itemList_);
    QWidget *detailPanel = new QWidget(splitter);
    detailPanel->setLayout(detail);
    splitter->addWidget(detailPanel);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);

    QVBoxLayout *main = new QVBoxLayout(central);
    main->addLayout(top);
    main->addWidget(splitter, 1);

    setWindowTitle(QStringLiteral("商城 - AI-WuZiQi"));
    resize(760, 520);

    connect(itemList_, &QListWidget::currentItemChanged, this, &ShopWindow::onCurrentItemChanged);
    connect(buyBtn_, &QPushButton::clicked, this, &ShopWindow::onBuyClicked);
    connect(equipBtn_, &QPushButton::clicked, this, &ShopWindow::onEquipClicked);
    connect(filterCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ShopWindow::onFilterChanged);
    connect(refreshBtn_, &QPushButton::clicked, this, &ShopWindow::onRefreshClicked);
}

// ---------- 数据回填 ----------
void ShopWindow::setItems(const QVector<ShopItemInfo> &items)
{
    items_ = items;
    refreshList();
}

void ShopWindow::setCoins(qint64 coins)
{
    coins_ = coins;
    coinsLabel_->setText(QStringLiteral("金币：%1").arg(coins_));
}

void ShopWindow::refreshList()
{
    itemList_->clear();
    QString filter = filterCombo_->currentData().toString();
    for (const ShopItemInfo &it : items_) {
        if (!filter.isEmpty() && it.type != filter)
            continue;
        QString mark = it.equipped ? QStringLiteral(" [已装备]")
                     : (it.owned ? QStringLiteral(" [已拥有]") : QString());
        QListWidgetItem *row = new QListWidgetItem(it.name + mark);
        row->setData(Qt::UserRole, it.id);
        itemList_->addItem(row);
    }
    if (itemList_->count() > 0)
        itemList_->setCurrentRow(0);
}

ShopItemInfo ShopWindow::currentItem() const
{
    for (const ShopItemInfo &it : items_)
        if (it.id == currentId_)
            return it;
    return ShopItemInfo();
}

// ---------- 槽 ----------
void ShopWindow::onCurrentItemChanged()
{
    QListWidgetItem *row = itemList_->currentItem();
    if (!row) {
        currentId_ = 0;
        return;
    }
    currentId_ = row->data(Qt::UserRole).toLongLong();
    ShopItemInfo it = currentItem();
    nameLabel_->setText(it.name);
    descLabel_->setText(it.description.isEmpty() ? QStringLiteral("暂无描述") : it.description);
    priceLabel_->setText(it.owned ? QStringLiteral("已拥有")
                                  : QStringLiteral("价格：%1 金币").arg(it.price));

    if (!it.previewImage.isEmpty()) {
        QPixmap pm(it.previewImage);
        if (!pm.isNull())
            previewLabel_->setPixmap(pm.scaled(previewLabel_->size(),
                                               Qt::KeepAspectRatio, Qt::SmoothTransformation));
        else
            previewLabel_->setText(QStringLiteral("预览图加载失败"));
    } else {
        previewLabel_->setText(QStringLiteral("无预览图"));
    }

    buyBtn_->setDisabled(it.owned);
    equipBtn_->setDisabled(!it.owned || it.equipped);
}

void ShopWindow::onBuyClicked()
{
    if (currentId_ <= 0)
        return;
    ShopItemInfo it = currentItem();
    if (it.price > coins_) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("金币不足"));
        return;
    }
    emit buyItemRequested(currentId_);
}

void ShopWindow::onEquipClicked()
{
    if (currentId_ > 0)
        emit equipItemRequested(currentId_);
}

void ShopWindow::onFilterChanged(int)
{
    refreshList();
}

void ShopWindow::onRefreshClicked()
{
    emit requestShopList(filterCombo_->currentData().toString());
}
