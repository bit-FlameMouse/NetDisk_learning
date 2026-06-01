-- ============================================================================
-- NetDisk 数据库初始化脚本
-- 版本: v1.0 | MySQL 8.0 InnoDB
-- 用法: mysql -u root -p < sql/schema.sql
-- ============================================================================

CREATE DATABASE IF NOT EXISTS netdisk
    DEFAULT CHARACTER SET utf8mb4
    DEFAULT COLLATE utf8mb4_unicode_ci;

USE netdisk;

-- ============================================================================
-- 1. 用户表
-- ============================================================================

CREATE TABLE users (
    id          BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    username    VARCHAR(64)     NOT NULL,
    password    CHAR(64)        NOT NULL COMMENT 'SHA256(password + salt) hex',
    salt        CHAR(64)        NOT NULL COMMENT '32字节随机盐的十六进制',
    email       VARCHAR(128)    DEFAULT '',
    quota_bytes BIGINT UNSIGNED DEFAULT 10737418240 COMMENT '默认 10GB',
    used_bytes  BIGINT UNSIGNED DEFAULT 0,
    last_login  DATETIME        DEFAULT NULL,
    created_at  DATETIME        DEFAULT CURRENT_TIMESTAMP,
    UNIQUE KEY uk_username (username)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- ============================================================================
-- 2. 虚拟路径节点表（核心表 — 邻接表模型）
-- ============================================================================

CREATE TABLE nodes (
    id           BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    user_id      BIGINT UNSIGNED NOT NULL,
    parent_id    BIGINT UNSIGNED DEFAULT NULL COMMENT 'NULL=根节点',
    name         VARCHAR(255)    NOT NULL,
    node_type    ENUM('file','directory') NOT NULL,
    file_size    BIGINT UNSIGNED DEFAULT 0,
    content_hash CHAR(64)        DEFAULT NULL COMMENT 'SHA256 hex, 仅 file 类型有值',
    mime_type    VARCHAR(128)    DEFAULT 'application/octet-stream',
    is_deleted   TINYINT(1)      DEFAULT 0 COMMENT '软删除标记',
    created_at   DATETIME        DEFAULT CURRENT_TIMESTAMP,
    updated_at   DATETIME        DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    FOREIGN KEY (user_id)   REFERENCES users(id),
    FOREIGN KEY (parent_id) REFERENCES nodes(id),
    UNIQUE KEY uk_user_path (user_id, parent_id, name, is_deleted),
    INDEX idx_parent (parent_id),
    INDEX idx_hash  (content_hash) COMMENT '秒传检测 + 去重查询'
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- ============================================================================
-- 3. 断点上传会话表
-- ============================================================================

CREATE TABLE upload_sessions (
    id              BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    user_id         BIGINT UNSIGNED NOT NULL,
    parent_id       BIGINT UNSIGNED NOT NULL,
    filename        VARCHAR(256)    NOT NULL,
    total_size      BIGINT UNSIGNED NOT NULL,
    chunk_size      INT UNSIGNED    DEFAULT 4194304 COMMENT '默认 4MB',
    total_chunks    INT UNSIGNED    NOT NULL,
    chunk_bitmap    VARBINARY(4096) NOT NULL COMMENT '位图: bit[i]=1 表示第i片已完成',
    completed_count INT UNSIGNED    DEFAULT 0,
    status          ENUM('CREATED','UPLOADING','MERGING','DONE','CANCELLED') DEFAULT 'CREATED',
    created_at      DATETIME        DEFAULT CURRENT_TIMESTAMP,
    updated_at      DATETIME        DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    FOREIGN KEY (user_id)   REFERENCES users(id),
    FOREIGN KEY (parent_id) REFERENCES nodes(id),
    INDEX idx_user_status (user_id, status)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
