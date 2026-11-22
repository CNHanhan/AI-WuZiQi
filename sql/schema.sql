-- schema.sql — AI-WuZiQi 数据库建表脚本
-- 对应设计文档：04-数据库设计.md 全文（3.1~3.8）
-- MySQL 8.0 / InnoDB / utf8mb4

CREATE DATABASE IF NOT EXISTS ai_wuziqi DEFAULT CHARSET utf8mb4 COLLATE utf8mb4_unicode_ci;
USE ai_wuziqi;

-- 3.1 用户表
CREATE TABLE IF NOT EXISTS t_user (
    uid BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    username VARCHAR(32) NOT NULL UNIQUE,
    password_hash VARCHAR(128) NOT NULL,
    salt VARCHAR(32) NOT NULL,
    email VARCHAR(64) NOT NULL UNIQUE,
    avatar VARCHAR(32) DEFAULT 'default',
    coins BIGINT DEFAULT 0,
    elo_score INT DEFAULT 1500,
    wins INT DEFAULT 0,
    losses INT DEFAULT 0,
    draws INT DEFAULT 0,
    total_games INT DEFAULT 0,
    status TINYINT DEFAULT 1,
    reg_time DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    last_login_time DATETIME,
    login_attempts INT DEFAULT 0,
    lock_until DATETIME,
    INDEX idx_username (username),
    INDEX idx_email (email),
    INDEX idx_elo (elo_score)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 3.2 对局记录表
CREATE TABLE IF NOT EXISTS t_game_record (
    record_id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    room_id BIGINT UNSIGNED NOT NULL,
    black_uid BIGINT UNSIGNED NOT NULL,
    white_uid BIGINT UNSIGNED NOT NULL,
    winner_uid BIGINT UNSIGNED,
    end_reason TINYINT,
    black_time_used INT,
    white_time_used INT,
    moves_json TEXT,
    start_time DATETIME NOT NULL,
    end_time DATETIME NOT NULL,
    INDEX idx_black (black_uid),
    INDEX idx_white (white_uid),
    INDEX idx_start_time (start_time)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 3.3 商城商品表
CREATE TABLE IF NOT EXISTS t_shop_item (
    item_id INT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    name VARCHAR(64) NOT NULL,
    description VARCHAR(256),
    type ENUM('board','stone','effect','avatar') NOT NULL,
    price INT UNSIGNED NOT NULL,
    resource_path VARCHAR(128),
    is_hot BOOLEAN DEFAULT FALSE,
    create_time DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 3.4 用户道具表
CREATE TABLE IF NOT EXISTS t_user_item (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    uid BIGINT UNSIGNED NOT NULL,
    item_id INT UNSIGNED NOT NULL,
    equipped BOOLEAN DEFAULT FALSE,
    acquire_time DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE KEY uk_uid_item (uid, item_id),
    INDEX idx_uid (uid)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 3.5 好友表
CREATE TABLE IF NOT EXISTS t_friend (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    uid BIGINT UNSIGNED NOT NULL,
    friend_uid BIGINT UNSIGNED NOT NULL,
    create_time DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE KEY uk_uid_friend (uid, friend_uid),
    INDEX idx_uid (uid)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 3.8 安全审计日志表
CREATE TABLE IF NOT EXISTS t_security_log (
    log_id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    uid BIGINT UNSIGNED,
    ip VARCHAR(45) NOT NULL,
    action VARCHAR(32) NOT NULL,
    status TINYINT NOT NULL,
    details TEXT,
    create_time DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_uid (uid),
    INDEX idx_ip (ip),
    INDEX idx_action (action),
    INDEX idx_create_time (create_time)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
