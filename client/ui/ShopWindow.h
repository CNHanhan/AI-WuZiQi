#pragma once

// ShopWindow — 商城
// 商品列表、预览、购买/装备、金币显示。意图通过 signals 抛出。

#include <QMainWindow>
#include <QString>
#include <QVector>

class QListWidget;
class QLabel;
class QPushButton;
class QComboBox;

// 商品值对象（与协议 shop_list_resp 对齐，不依赖网络层）
struct ShopItemInfo {
    qint64 id = 0;
    QString name;
    QString description;
    QString type;        // board / stone / effect / avatar
    qint64 price = 0;
    QString previewImage;
    bool owned = false;
    bool equipped = false;
};

class ShopWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit ShopWindow(QWidget *parent = nullptr);

    // ---------- 数据回填 ----------
    void setItems(const QVector<ShopItemInfo> &items);
    void setCoins(qint64 coins);

signals:
    void buyItemRequested(qint64 itemId);
    void equipItemRequested(qint64 itemId);
    void requestShopList(const QString &type = QString());

private slots:
    void onCurrentItemChanged();
    void onBuyClicked();
    void onEquipClicked();
    void onFilterChanged(int index);
    void onRefreshClicked();

private:
    void setupUi();
    void refreshList();
    ShopItemInfo currentItem() const;

    QListWidget *itemList_;
    QLabel *previewLabel_;
    QLabel *nameLabel_;
    QLabel *descLabel_;
    QLabel *priceLabel_;
    QLabel *coinsLabel_;
    QPushButton *buyBtn_;
    QPushButton *equipBtn_;
    QPushButton *refreshBtn_;
    QComboBox *filterCombo_;

    QVector<ShopItemInfo> items_;
    qint64 coins_ = 0;
    qint64 currentId_ = 0;
};
