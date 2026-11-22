/**
 * Database.cpp — MySQL C API（Prepared Statement）实现
 * 对应设计文档：01-服务端设计.md 2.3/2.7；04-数据库设计.md 全文；06-加密系统设计.md 3.2
 *
 * 所有涉及外部输入的 SQL 一律使用 mysql_stmt_prepare + bind 参数化，杜绝字符串拼接。
 */
#include "Database.h"
#include "Config.h"

#include <mysql/mysql.h>

#include <cstring>
#include <ctime>
#include <mutex>

#define DB_LOCK std::lock_guard<std::mutex> _dbLock(mtx_);

namespace AIWZQ {

namespace {

// 把 std::string 绑定到 MYSQL_BIND（注意：底层内存必须在 stmt 执行期间有效）
void bindStr(MYSQL_BIND& b, std::string& s) {
    std::memset(&b, 0, sizeof(b));
    b.buffer_type = MYSQL_TYPE_STRING;
    b.buffer = const_cast<char*>(s.data());
    b.buffer_length = s.size();
    b.length = nullptr;
    b.is_null = nullptr;
}

void bindLL(MYSQL_BIND& b, int64_t& v) {
    std::memset(&b, 0, sizeof(b));
    b.buffer_type = MYSQL_TYPE_LONGLONG;
    b.buffer = &v;
    b.buffer_length = sizeof(v);
    b.is_unsigned = 0;
}

void bindInt(MYSQL_BIND& b, int32_t& v) {
    std::memset(&b, 0, sizeof(b));
    b.buffer_type = MYSQL_TYPE_LONG;
    b.buffer = &v;
    b.buffer_length = sizeof(v);
    b.is_unsigned = 0;
}

void bindNull(MYSQL_BIND& b) {
    std::memset(&b, 0, sizeof(b));
    b.buffer_type = MYSQL_TYPE_NULL;
}

// 取当前本地时间 'YYYY-MM-DD HH:MM:SS'
std::string nowStr() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}

// 把 MYSQL_BIND 输出（定长 buffer）读成 std::string
std::string readStr(MYSQL_BIND& b) {
    if (*b.is_null) return "";
    return std::string(reinterpret_cast<const char*>(b.buffer), *b.length);
}

} // namespace

Database::Database() = default;

Database::~Database() { close(); }

void Database::close() {
    DB_LOCK
    if (conn_) {
        mysql_close(conn_);
        conn_ = nullptr;
    }
}

bool Database::connect() {
    DB_LOCK
    closeUnlocked_();
    conn_ = mysql_init(nullptr);
    if (!conn_) return false;

    const DbConfig& cfg = Config::instance().database();
    unsigned int timeout = 5;
    mysql_options(conn_, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
    mysql_options(conn_, MYSQL_SET_CHARSET_NAME, "utf8mb4");

    if (!mysql_real_connect(conn_, cfg.host.c_str(), cfg.user.c_str(),
                            cfg.password.c_str(), cfg.dbname.c_str(), cfg.port,
                            nullptr, 0)) {
        mysql_close(conn_);
        conn_ = nullptr;
        return false;
    }
    return true;
}

void Database::closeUnlocked_() {
    if (conn_) { mysql_close(conn_); conn_ = nullptr; }
}

bool Database::ensureConnected() {
    if (conn_) return true;
    closeUnlocked_();
    conn_ = mysql_init(nullptr);
    if (!conn_) return false;
    const DbConfig& cfg = Config::instance().database();
    unsigned int timeout = 5;
    mysql_options(conn_, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
    mysql_options(conn_, MYSQL_SET_CHARSET_NAME, "utf8mb4");
    if (!mysql_real_connect(conn_, cfg.host.c_str(), cfg.user.c_str(),
                            cfg.password.c_str(), cfg.dbname.c_str(), cfg.port,
                            nullptr, 0)) {
        mysql_close(conn_); conn_ = nullptr; return false;
    }
    return true;
}

bool Database::exec_(MYSQL_STMT* stmt) {
    if (mysql_stmt_execute(stmt) != 0) return false;
    return mysql_stmt_affected_rows(stmt) >= 0;
}

// ---------------- 用户 ----------------

bool Database::insertUser(const User& user) {
    DB_LOCK
    if (!ensureConnected()) return false;
    static const char* SQL =
        "INSERT INTO t_user(username,password_hash,salt,email,avatar,coins,elo_score,"
        "wins,losses,draws,total_games,status,reg_time) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)";
    MYSQL_STMT* stmt = mysql_stmt_init(conn_);
    if (!stmt) return false;
    bool ok = false;
    do {
        if (mysql_stmt_prepare(stmt, SQL, static_cast<unsigned long>(std::strlen(SQL)))) break;
        std::string username = user.username, hash = user.passwordHash, salt = user.salt;
        std::string email = user.email, avatar = user.avatar, regTime = nowStr();
        int64_t coins = user.coins;
        int32_t elo = user.eloScore, wins = user.wins, losses = user.losses;
        int32_t draws = user.draws, total = user.totalGames;
        int32_t status = user.status;

        MYSQL_BIND b[13];
        std::memset(b, 0, sizeof(b));
        bindStr(b[0], username); bindStr(b[1], hash); bindStr(b[2], salt);
        bindStr(b[3], email);    bindStr(b[4], avatar);
        bindLL(b[5], coins);
        bindInt(b[6], elo);      bindInt(b[7], wins); bindInt(b[8], losses);
        bindInt(b[9], draws);    bindInt(b[10], total); bindInt(b[11], status);
        bindStr(b[12], regTime);

        if (mysql_stmt_bind_param(stmt, b)) break;
        ok = exec_(stmt);
    } while (false);
    mysql_stmt_close(stmt);
    return ok;
}

namespace {

// 公共读取：按 username 或 uid 查询，row 回调由调用方填充 User
bool fetchUserBy(MYSQL* conn, const char* sql, const std::string& key, bool byUid, User& out) {
    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) return false;
    bool found = false;
    do {
        if (mysql_stmt_prepare(stmt, sql, static_cast<unsigned long>(std::strlen(sql)))) break;
        MYSQL_BIND pb[1];
        std::memset(pb, 0, sizeof(pb));
        std::string skey = key;
        int64_t ikey = byUid ? std::stoll(key) : 0;
        if (byUid) bindLL(pb[0], ikey); else bindStr(pb[0], skey);
        if (mysql_stmt_bind_param(stmt, pb)) break;
        if (mysql_stmt_execute(stmt)) break;

        char buf_uid[32], buf_uname[64], buf_hash[160], buf_salt[48], buf_email[80];
        char buf_avatar[48], buf_reg[32], buf_last[32], buf_lock[32];
        int64_t uid = 0, coins = 0;
        int32_t elo = 0, wins = 0, losses = 0, draws = 0, total = 0, status = 0;
        int32_t attempts = 0;
        unsigned long len_uname=0,len_hash=0,len_salt=0,len_email=0,len_avatar=0,len_reg=0,len_last=0,len_lock=0;
        my_bool n_last=1, n_lock=1;

        MYSQL_BIND rb[16];
        std::memset(rb, 0, sizeof(rb));
        rb[0].buffer_type=MYSQL_TYPE_LONGLONG; rb[0].buffer=&uid;              rb[0].buffer_length=sizeof(uid);
        rb[1].buffer_type=MYSQL_TYPE_STRING;   rb[1].buffer=buf_uname;          rb[1].buffer_length=sizeof(buf_uname); rb[1].length=&len_uname;
        rb[2].buffer_type=MYSQL_TYPE_STRING;   rb[2].buffer=buf_hash;           rb[2].buffer_length=sizeof(buf_hash);  rb[2].length=&len_hash;
        rb[3].buffer_type=MYSQL_TYPE_STRING;   rb[3].buffer=buf_salt;           rb[3].buffer_length=sizeof(buf_salt);  rb[3].length=&len_salt;
        rb[4].buffer_type=MYSQL_TYPE_STRING;   rb[4].buffer=buf_email;          rb[4].buffer_length=sizeof(buf_email); rb[4].length=&len_email;
        rb[5].buffer_type=MYSQL_TYPE_STRING;   rb[5].buffer=buf_avatar;         rb[5].buffer_length=sizeof(buf_avatar);rb[5].length=&len_avatar;
        rb[6].buffer_type=MYSQL_TYPE_LONGLONG; rb[6].buffer=&coins;             rb[6].buffer_length=sizeof(coins);
        rb[7].buffer_type=MYSQL_TYPE_LONG;     rb[7].buffer=&elo;               rb[7].buffer_length=sizeof(elo);
        rb[8].buffer_type=MYSQL_TYPE_LONG;     rb[8].buffer=&wins;              rb[8].buffer_length=sizeof(wins);
        rb[9].buffer_type=MYSQL_TYPE_LONG;     rb[9].buffer=&losses;            rb[9].buffer_length=sizeof(losses);
        rb[10].buffer_type=MYSQL_TYPE_LONG;    rb[10].buffer=&draws;            rb[10].buffer_length=sizeof(draws);
        rb[11].buffer_type=MYSQL_TYPE_LONG;    rb[11].buffer=&total;            rb[11].buffer_length=sizeof(total);
        rb[12].buffer_type=MYSQL_TYPE_LONG;    rb[12].buffer=&status;           rb[12].buffer_length=sizeof(status);
        rb[13].buffer_type=MYSQL_TYPE_STRING;  rb[13].buffer=buf_reg;           rb[13].buffer_length=sizeof(buf_reg);  rb[13].length=&len_reg;
        rb[14].buffer_type=MYSQL_TYPE_STRING;  rb[14].buffer=buf_last;          rb[14].buffer_length=sizeof(buf_last); rb[14].length=&len_last; rb[14].is_null=&n_last;
        rb[15].buffer_type=MYSQL_TYPE_LONG;    rb[15].buffer=&attempts;         rb[15].buffer_length=sizeof(attempts);

        if (mysql_stmt_bind_result(stmt, rb)) break;
        if (mysql_stmt_fetch(stmt) != 0) break; // MYSQL_DATA_TRUNCATED 视为无行，这里够用

        out.uid = uid;
        out.username.assign(buf_uname, len_uname);
        out.passwordHash.assign(buf_hash, len_hash);
        out.salt.assign(buf_salt, len_salt);
        out.email.assign(buf_email, len_email);
        out.avatar.assign(buf_avatar, len_avatar);
        out.coins = coins; out.eloScore = elo; out.wins = wins; out.losses = losses;
        out.draws = draws; out.totalGames = total; out.status = static_cast<int8_t>(status);
        found = true;
        (void)buf_uid; (void)buf_lock; (void)len_lock;
    } while (false);
    mysql_stmt_close(stmt);
    return found;
}

} // namespace

std::optional<User> Database::queryUserByUsername(const std::string& username) {
    DB_LOCK
    if (!ensureConnected()) return std::nullopt;
    const char* SQL =
        "SELECT uid,username,password_hash,salt,email,avatar,coins,elo_score,wins,losses,"
        "draws,total_games,status,reg_time,last_login_time,login_attempts "
        "FROM t_user WHERE username=? LIMIT 1";
    User u;
    if (!fetchUserBy(conn_, SQL, username, false, u)) return std::nullopt;
    return u;
}

std::optional<User> Database::queryUserByUid(int64_t uid) {
    DB_LOCK
    if (!ensureConnected()) return std::nullopt;
    const char* SQL =
        "SELECT uid,username,password_hash,salt,email,avatar,coins,elo_score,wins,losses,"
        "draws,total_games,status,reg_time,last_login_time,login_attempts "
        "FROM t_user WHERE uid=? LIMIT 1";
    User u;
    if (!fetchUserBy(conn_, SQL, std::to_string(uid), true, u)) return std::nullopt;
    return u;
}

bool Database::updateUser(const User& user) {
    DB_LOCK
    if (!ensureConnected()) return false;
    static const char* SQL =
        "UPDATE t_user SET password_hash=?,salt=?,email=?,avatar=?,coins=?,elo_score=?,"
        "wins=?,losses=?,draws=?,total_games=?,status=? WHERE uid=?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn_);
    if (!stmt) return false;
    bool ok = false;
    do {
        if (mysql_stmt_prepare(stmt, SQL, static_cast<unsigned long>(std::strlen(SQL)))) break;
        std::string hash=user.passwordHash, salt=user.salt, email=user.email, avatar=user.avatar;
        int64_t coins=user.coins, uid=user.uid;
        int32_t elo=user.eloScore,wins=user.wins,losses=user.losses,draws=user.draws,total=user.totalGames,status=user.status;
        MYSQL_BIND b[12];
        std::memset(b, 0, sizeof(b));
        bindStr(b[0],hash); bindStr(b[1],salt); bindStr(b[2],email); bindStr(b[3],avatar);
        bindLL(b[4],coins); bindLL(b[11],uid);
        bindInt(b[5],elo); bindInt(b[6],wins); bindInt(b[7],losses); bindInt(b[8],draws);
        bindInt(b[9],total); bindInt(b[10],status);
        if (mysql_stmt_bind_param(stmt,b)) break;
        ok = exec_(stmt);
    } while(false);
    mysql_stmt_close(stmt);
    return ok;
}

bool Database::updatePassword(int64_t uid, const std::string& passwordHash) {
    DB_LOCK
    if (!ensureConnected()) return false;
    static const char* SQL = "UPDATE t_user SET password_hash=? WHERE uid=?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn_);
    if (!stmt) return false;
    bool ok=false;
    do {
        if (mysql_stmt_prepare(stmt, SQL, static_cast<unsigned long>(std::strlen(SQL)))) break;
        std::string h=passwordHash; int64_t id=uid;
        MYSQL_BIND b[2]; std::memset(b,0,sizeof(b));
        bindStr(b[0],h); bindLL(b[1],id);
        if (mysql_stmt_bind_param(stmt,b)) break;
        ok=exec_(stmt);
    } while(false);
    mysql_stmt_close(stmt);
    return ok;
}

bool Database::updateLoginAttempts(int64_t uid, int attempts) {
    DB_LOCK
    if (!ensureConnected()) return false;
    static const char* SQL =
        "UPDATE t_user SET login_attempts=?, lock_until=CASE WHEN ?=0 THEN NULL ELSE lock_until END WHERE uid=?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn_);
    if (!stmt) return false;
    bool ok=false;
    do {
        if (mysql_stmt_prepare(stmt, SQL, static_cast<unsigned long>(std::strlen(SQL)))) break;
        int32_t a=attempts, zero=attempts, id32=0; int64_t id=uid;
        MYSQL_BIND b[3]; std::memset(b,0,sizeof(b));
        bindInt(b[0],a); bindInt(b[1],zero); bindLL(b[2],id);
        if (mysql_stmt_bind_param(stmt,b)) break;
        ok=exec_(stmt);
    } while(false);
    mysql_stmt_close(stmt);
    return ok;
}

// ---------------- 对局记录 ----------------

bool Database::insertGameRecord(const GameRecord& r) {
    DB_LOCK
    if (!ensureConnected()) return false;
    static const char* SQL =
        "INSERT INTO t_game_record(room_id,black_uid,white_uid,winner_uid,end_reason,"
        "black_time_used,white_time_used,moves_json,start_time,end_time) VALUES(?,?,?,?,?,?,?,?,?,?)";
    MYSQL_STMT* stmt = mysql_stmt_init(conn_);
    if (!stmt) return false;
    bool ok=false;
    do {
        if (mysql_stmt_prepare(stmt, SQL, static_cast<unsigned long>(std::strlen(SQL)))) break;
        int64_t room=r.roomId, black=r.blackUid, white=r.whiteUid;
        int64_t winner = r.winnerUid.value_or(0);
        int32_t reason=r.endReason, bu=r.blackTimeUsed, wu=r.whiteTimeUsed;
        std::string moves=r.movesJson, start=r.startTime.empty()?nowStr():r.startTime;
        std::string end=r.endTime.empty()?nowStr():r.endTime;
        MYSQL_BIND b[10]; std::memset(b,0,sizeof(b));
        bindLL(b[0],room); bindLL(b[1],black); bindLL(b[2],white);
        if (r.winnerUid) bindLL(b[3],winner); else bindNull(b[3]);
        bindInt(b[4],reason); bindInt(b[5],bu); bindInt(b[6],wu);
        bindStr(b[7],moves); bindStr(b[8],start); bindStr(b[9],end);
        if (mysql_stmt_bind_param(stmt,b)) break;
        ok=exec_(stmt);
    } while(false);
    mysql_stmt_close(stmt);
    return ok;
}

bool Database::updateUserStats(int64_t uid, GameResult result, int32_t eloDelta) {
    DB_LOCK
    if (!ensureConnected()) return false;
    // 用 CASE 表达式一次更新，避免多次往返
    static const char* SQL =
        "UPDATE t_user SET "
        "wins=wins+?, losses=losses+?, draws=draws+?, total_games=total_games+1, "
        "elo_score=elo_score+? WHERE uid=?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn_);
    if (!stmt) return false;
    bool ok=false;
    do {
        if (mysql_stmt_prepare(stmt, SQL, static_cast<unsigned long>(std::strlen(SQL)))) break;
        int32_t w = (result==GameResult::BLACK_WIN) ? 1 : 0; // 调用方传的是该 uid 的结果
        int32_t l = (result==GameResult::WHITE_WIN) ? 1 : 0;
        int32_t d = (result==GameResult::DRAW) ? 1 : 0;
        int32_t delta=eloDelta, id=0; int64_t uidv=uid;
        MYSQL_BIND b[5]; std::memset(b,0,sizeof(b));
        bindInt(b[0],w); bindInt(b[1],l); bindInt(b[2],d); bindInt(b[3],delta); bindLL(b[4],uidv);
        if (mysql_stmt_bind_param(stmt,b)) break;
        ok=exec_(stmt);
    } while(false);
    mysql_stmt_close(stmt);
    return ok;
}

// ---------------- 商城 ----------------

bool Database::queryShopItems(std::vector<ShopItem>& items) {
    DB_LOCK
    if (!ensureConnected()) return false;
    static const char* SQL =
        "SELECT item_id,name,description,type,price,resource_path,is_hot FROM t_shop_item ORDER BY item_id";
    MYSQL_STMT* stmt=mysql_stmt_init(conn_);
    if(!stmt) return false;
    bool ok=false;
    do {
        if(mysql_stmt_prepare(stmt,SQL,static_cast<unsigned long>(std::strlen(SQL)))) break;
        if(mysql_stmt_execute(stmt)) break;
        char name[96],desc[320],type[24],path[160];
        unsigned long lname=0,ldesc=0,ltype=0,lpath=0;
        int64_t id=0; uint32_t price=0; my_bool hot=0;
        MYSQL_BIND b[7]; std::memset(b,0,sizeof(b));
        b[0].buffer_type=MYSQL_TYPE_LONGLONG; b[0].buffer=&id;    b[0].buffer_length=sizeof(id); b[0].is_unsigned=1;
        b[1].buffer_type=MYSQL_TYPE_STRING;   b[1].buffer=name;   b[1].buffer_length=sizeof(name);   b[1].length=&lname;
        b[2].buffer_type=MYSQL_TYPE_STRING;   b[2].buffer=desc;   b[2].buffer_length=sizeof(desc);   b[2].length=&ldesc; b[2].is_null=&hot;
        b[3].buffer_type=MYSQL_TYPE_STRING;   b[3].buffer=type;   b[3].buffer_length=sizeof(type);   b[3].length=&ltype;
        b[4].buffer_type=MYSQL_TYPE_LONG;     b[4].buffer=&price; b[4].buffer_length=sizeof(price); b[4].is_unsigned=1;
        b[5].buffer_type=MYSQL_TYPE_STRING;   b[5].buffer=path;   b[5].buffer_length=sizeof(path);   b[5].length=&lpath;
        b[6].buffer_type=MYSQL_TYPE_TINY;     b[6].buffer=&hot;   b[6].buffer_length=sizeof(hot);
        if(mysql_stmt_bind_result(stmt,b)) break;
        items.clear();
        int rc;
        while((rc=mysql_stmt_fetch(stmt))==0){
            ShopItem it;
            it.itemId=id; it.name.assign(name,lname); it.description.assign(desc,ldesc);
            it.type.assign(type,ltype); it.price=price; it.resourcePath.assign(path,lpath); it.isHot=hot!=0;
            items.push_back(it);
        }
        ok = (rc==MYSQL_NO_DATA);
    } while(false);
    mysql_stmt_close(stmt);
    return ok;
}

bool Database::queryUserItems(int64_t uid, std::vector<UserItem>& items) {
    DB_LOCK
    if (!ensureConnected()) return false;
    static const char* SQL =
        "SELECT ui.id,ui.item_id,si.name,si.type,si.price,si.resource_path,ui.equipped "
        "FROM t_user_item ui JOIN t_shop_item si ON ui.item_id=si.item_id WHERE ui.uid=? ORDER BY ui.id";
    MYSQL_STMT* stmt=mysql_stmt_init(conn_);
    if(!stmt) return false;
    bool ok=false;
    do {
        if(mysql_stmt_prepare(stmt,SQL,static_cast<unsigned long>(std::strlen(SQL)))) break;
        int64_t id=uid;
        MYSQL_BIND pb[1]; std::memset(pb,0,sizeof(pb)); bindLL(pb[0],id);
        if(mysql_stmt_bind_param(stmt,pb)) break;
        if(mysql_stmt_execute(stmt)) break;
        char name[96],type[24],path[160];
        unsigned long lname=0,ltype=0,lpath=0;
        int64_t rowId=0,itemId=0; uint32_t price=0; my_bool eq=0;
        MYSQL_BIND b[7]; std::memset(b,0,sizeof(b));
        b[0].buffer_type=MYSQL_TYPE_LONGLONG; b[0].buffer=&rowId;  b[0].buffer_length=sizeof(rowId);
        b[1].buffer_type=MYSQL_TYPE_LONGLONG; b[1].buffer=&itemId;  b[1].buffer_length=sizeof(itemId);
        b[2].buffer_type=MYSQL_TYPE_STRING;   b[2].buffer=name;    b[2].buffer_length=sizeof(name); b[2].length=&lname;
        b[3].buffer_type=MYSQL_TYPE_STRING;   b[3].buffer=type;    b[3].buffer_length=sizeof(type); b[3].length=&ltype;
        b[4].buffer_type=MYSQL_TYPE_LONG;     b[4].buffer=&price;  b[4].buffer_length=sizeof(price);b[4].is_unsigned=1;
        b[5].buffer_type=MYSQL_TYPE_STRING;   b[5].buffer=path;    b[5].buffer_length=sizeof(path); b[5].length=&lpath;
        b[6].buffer_type=MYSQL_TYPE_TINY;     b[6].buffer=&eq;     b[6].buffer_length=sizeof(eq);
        if(mysql_stmt_bind_result(stmt,b)) break;
        items.clear();
        int rc;
        while((rc=mysql_stmt_fetch(stmt))==0){
            UserItem it;
            it.id=rowId; it.itemId=itemId; it.name.assign(name,lname);
            it.type.assign(type,ltype); it.price=price; it.resourcePath.assign(path,lpath); it.equipped=eq!=0;
            items.push_back(it);
        }
        ok=(rc==MYSQL_NO_DATA);
    } while(false);
    mysql_stmt_close(stmt);
    return ok;
}

bool Database::buyItem(int64_t uid, int64_t itemId) {
    DB_LOCK
    if (!ensureConnected()) return false;
    // 事务：查价格 -> 校验余额并扣款 -> 插入持有记录
    if (mysql_autocommit(conn_, 0) != 0) return false;
    bool ok=false, committed=false;
    do {
        // 1) 取价格
        int64_t price=0;
        {
            MYSQL_STMT* s1=mysql_stmt_init(conn_);
            if(!s1) break;
            static const char* Q="SELECT price FROM t_shop_item WHERE item_id=?";
            bool qok=false;
            if(!mysql_stmt_prepare(s1,Q,static_cast<unsigned long>(std::strlen(Q)))){
                int64_t iid=itemId; MYSQL_BIND pb[1]; std::memset(pb,0,sizeof(pb)); bindLL(pb[0],iid);
                if(!mysql_stmt_bind_param(s1,pb) && !mysql_stmt_execute(s1)){
                    MYSQL_BIND rb[1]; std::memset(rb,0,sizeof(rb));
                    rb[0].buffer_type=MYSQL_TYPE_LONGLONG; rb[0].buffer=&price; rb[0].buffer_length=sizeof(price); rb[0].is_unsigned=1;
                    if(!mysql_stmt_bind_result(s1,rb) && mysql_stmt_fetch(s1)==0) qok=true;
                }
            }
            mysql_stmt_close(s1);
            if(!qok) break;
        }
        // 2) 条件扣款（余额充足才更新）
        {
            MYSQL_STMT* s2=mysql_stmt_init(conn_);
            if(!s2) break;
            static const char* Q="UPDATE t_user SET coins=coins-? WHERE uid=? AND coins>=?";
            bool uok=false;
            if(!mysql_stmt_prepare(s2,Q,static_cast<unsigned long>(std::strlen(Q)))){
                int64_t p=price,u=uid;
                MYSQL_BIND pb[3]; std::memset(pb,0,sizeof(pb));
                bindLL(pb[0],p); bindLL(pb[1],u); bindLL(pb[2],p);
                if(!mysql_stmt_bind_param(s2,pb) && !mysql_stmt_execute(s2))
                    uok = mysql_stmt_affected_rows(s2)==1;
            }
            mysql_stmt_close(s2);
            if(!uok) break;
        }
        // 3) 写持有记录
        {
            MYSQL_STMT* s3=mysql_stmt_init(conn_);
            if(!s3) break;
            static const char* Q="INSERT INTO t_user_item(uid,item_id,equipped,acquire_time) VALUES(?,?,0,?)";
            bool iok=false;
            if(!mysql_stmt_prepare(s3,Q,static_cast<unsigned long>(std::strlen(Q)))){
                int64_t u=uid,i=itemId; std::string t=nowStr();
                MYSQL_BIND pb[3]; std::memset(pb,0,sizeof(pb));
                bindLL(pb[0],u); bindLL(pb[1],i); bindStr(pb[2],t);
                if(!mysql_stmt_bind_param(s3,pb) && !mysql_stmt_execute(s3)) iok=true;
            }
            mysql_stmt_close(s3);
            if(!iok) break;
        }
        if (mysql_commit(conn_)==0) { ok=true; committed=true; }
    } while(false);
    if (!committed) mysql_rollback(conn_);
    mysql_autocommit(conn_, 1);
    return ok;
}

bool Database::equipItem(int64_t uid, int64_t itemId, bool equipped) {
    DB_LOCK
    if (!ensureConnected()) return false;
    static const char* SQL = "UPDATE t_user_item SET equipped=? WHERE uid=? AND item_id=?";
    MYSQL_STMT* stmt=mysql_stmt_init(conn_);
    if(!stmt) return false;
    bool ok=false;
    do {
        if(mysql_stmt_prepare(stmt,SQL,static_cast<unsigned long>(std::strlen(SQL)))) break;
        int32_t eq=equipped?1:0; int64_t u=uid,i=itemId;
        MYSQL_BIND b[3]; std::memset(b,0,sizeof(b));
        bindInt(b[0],eq); bindLL(b[1],u); bindLL(b[2],i);
        if(mysql_stmt_bind_param(stmt,b)) break;
        ok=exec_(stmt);
    } while(false);
    mysql_stmt_close(stmt);
    return ok;
}

// ---------------- 审计 / 排行榜 ----------------

bool Database::insertSecurityLog(int64_t uid, const std::string& ip,
                                 const std::string& action, int status,
                                 const std::string& details) {
    DB_LOCK
    if (!ensureConnected()) return false;
    static const char* SQL =
        "INSERT INTO t_security_log(uid,ip,action,status,details,create_time) VALUES(?,?,?,?,?,?)";
    MYSQL_STMT* stmt=mysql_stmt_init(conn_);
    if(!stmt) return false;
    bool ok=false;
    do {
        if(mysql_stmt_prepare(stmt,SQL,static_cast<unsigned long>(std::strlen(SQL)))) break;
        int64_t u=uid; std::string sip=ip,act=action,det=details,t=nowStr(); int32_t st=status;
        MYSQL_BIND b[6]; std::memset(b,0,sizeof(b));
        if(uid>0) bindLL(b[0],u); else bindNull(b[0]);
        bindStr(b[1],sip); bindStr(b[2],act); bindInt(b[3],st); bindStr(b[4],det); bindStr(b[5],t);
        if(mysql_stmt_bind_param(stmt,b)) break;
        ok=exec_(stmt);
    } while(false);
    mysql_stmt_close(stmt);
    return ok;
}

bool Database::queryAllElo(std::vector<std::pair<int64_t, int32_t>>& out) {
    DB_LOCK
    if (!ensureConnected()) return false;
    static const char* SQL="SELECT uid,elo_score FROM t_user WHERE status=1";
    MYSQL_STMT* stmt=mysql_stmt_init(conn_);
    if(!stmt) return false;
    bool ok=false;
    do {
        if(mysql_stmt_prepare(stmt,SQL,static_cast<unsigned long>(std::strlen(SQL)))) break;
        if(mysql_stmt_execute(stmt)) break;
        int64_t uid=0; int32_t elo=0;
        MYSQL_BIND b[2]; std::memset(b,0,sizeof(b));
        b[0].buffer_type=MYSQL_TYPE_LONGLONG; b[0].buffer=&uid; b[0].buffer_length=sizeof(uid);
        b[1].buffer_type=MYSQL_TYPE_LONG;     b[1].buffer=&elo; b[1].buffer_length=sizeof(elo);
        if(mysql_stmt_bind_result(stmt,b)) break;
        out.clear(); int rc;
        while((rc=mysql_stmt_fetch(stmt))==0) out.emplace_back(uid,elo);
        ok=(rc==MYSQL_NO_DATA);
    } while(false);
    mysql_stmt_close(stmt);
    return ok;
}

} // namespace AIWZQ
