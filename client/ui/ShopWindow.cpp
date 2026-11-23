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
#include <QInputDialog>
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
    rechargeBtn_ = new QPushButton(QStringLiteral("充值"), central);
    rechargeBtn_->setStyleSheet(QStringLiteral(
        "QPushButton{background:#e8a33d;color:#fff;font-weight:bold;border-radius:4px;padding:4px 14px;}"));
    QHBoxLayout *top = new QHBoxLayout;
    top->addWidget(coinsLabel_, 1);
    top->addWidget(rechargeBtn_);
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
    connect(rechargeBtn_, &QPushButton::clicked, this, &ShopWindow::onRechargeClicked);
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
        // webp 兜底：部署若缺少 Qt webp 图像插件，同名 .webp 会加载失败，
        // 此时改用我们一并打入 qrc 的同名 .png（生成脚本对两张特效图都出了 png）。
        if (pm.isNull() && it.previewImage.endsWith(QStringLiteral(".webp"), Qt::CaseInsensitive)) {
            const QString pngPath = it.previewImage.left(it.previewImage.size() - 5) + QStringLiteral(".png");
            pm.load(pngPath);
        }
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

void ShopWindow::onRechargeClicked()
{
    // 本地演示：选档位或输入自定义金额，发出信号由 GameController 弹"是否已充值"确认后加金币。
    QStringList tiers;
    tiers << QStringLiteral("100 金币") << QStringLiteral("500 金币")
          << QStringLiteral("1000 金币") << QStringLiteral("5000 金币")
          << QStringLiteral("自定义金额...");
    bool ok = false;
    const QString picked = QInputDialog::getItem(
        this, QStringLiteral("充值"), QStringLiteral("选择充值档位："),
        tiers, 0, false, &ok);
    if (!ok || picked.isEmpty())
        return;

    qint64 amount = 0;
    if (picked == QStringLiteral("自定义金额...")) {
        amount = static_cast<qint64>(QInputDialog::getInt(
            this, QStringLiteral("充值"), QStringLiteral("输入充值金币数："),
            100, 1, 1000000, 100, &ok));
        if (!ok || amount <= 0)
            return;
    } else {
        // 从档位字符串头部分离数字
        amount = picked.section(QStringLiteral(" "), 0, 0).toLongLong();
        if (amount <= 0)
            return;
    }
    emit rechargeRequested(amount);
}
