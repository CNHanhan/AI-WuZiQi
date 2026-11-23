#pragma once

// MainWindow — 游戏大厅
// 所有用户操作通过 signals 抛给 controller；数据通过 public 方法回写。
// 绝不 include 网络层头文件。

#include <QMainWindow>
#include <QString>
#include <QVector>
#include <QPair>

class QTableWidget;
class QListWidget;
class QTextEdit;
class QLineEdit;
class QPushButton;
class QLabel;
class QSpinBox;
class QShowEvent;

// 房间条目（净化的值对象，不依赖任何 protocol/网络类型）
struct RoomEntry {
    qint64 id = 0;
    QString ownerName;
    int     ownerElo = 1500;
    QString status;          // waiting / playing
    bool    hasPassword = false;
    qint64  createTime = 0;
};

struct PlayerEntry {
    QString name;
    int     elo = 1500;
    QString status;          // online / in_game
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

    // ---------- 用户信息栏 ----------
    void setUserInfo(const QString &username, int elo, int wins, qint64 coins);

    // ---------- 数据回填 ----------
    void setRooms(const QVector<RoomEntry> &rooms, int total, int page, int pageCount);
    void setPlayers(const QVector<PlayerEntry> &players);
    void appendChat(const QString &sender, const QString &content, qint64 timestamp = 0);
    void clearChat();

signals:
    // 房间
    void refreshRoomsRequested(int page, int pageSize);
    void createRoomRequested(int totalTime, int stepTime, const QString &password);
    void joinRoomRequested(qint64 roomId, const QString &password);
    void randomMatchRequested();
    // 大厅聊天
    void chatRequested(const QString &content);
    // 入口
    void rankRequested();
    void shopRequested();
    void offlineGameRequested();
    void logoutRequested();

private slots:
    void onRefreshClicked();
    void onCreateRoomClicked();
    void onJoinRoomClicked();
    void onRandomMatchClicked();
    void onSendChatClicked();
    void onRoomDoubleClicked(int row, int column);
    void onPrevPage();
    void onNextPage();

private:
    void setupUi();
    void updatePageLabel();
    void showEvent(QShowEvent* event) override;   // 窗口显示时激活并把焦点交给聊天输入框

    QTableWidget *roomTable_;
    QListWidget  *playerList_;
    QTextEdit    *chatDisplay_;
    QLineEdit    *chatInput_;
    QPushButton  *sendChatBtn_;
    QPushButton  *refreshBtn_;
    QPushButton  *createRoomBtn_;
    QPushButton  *joinRoomBtn_;
    QPushButton  *randomJoinBtn_;
    QPushButton  *rankBtn_;
    QPushButton  *shopBtn_;
    QPushButton  *offlineBtn_;
    QPushButton  *logoutBtn_;
    QPushButton  *prevPageBtn_;
    QPushButton  *nextPageBtn_;
    QLabel       *userInfoLabel_;
    QLabel       *pageLabel_;
    QSpinBox     *pageSizeSpin_;

    int  currentPage_ = 1;
    int  pageCount_   = 1;
};
