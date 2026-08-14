#ifndef STORAGEMANAGER_H
#define STORAGEMANAGER_H

#include <QString>

// 录像存储治理：录像目录总大小超过上限时，从最旧的录像开始删除，
// 直到降到上限的 keepRatio 比例，同时清理数据库中对应的 video_file / event 记录。
// 用途：防止长时间运行把硬盘吃满。
class StorageManager
{
public:
    // dir:       录像目录
    // maxBytes:  总大小上限(字节)，<=0 表示不限制
    // keepRatio: 清理到上限的比例(默认 0.8，即降到 80%)
    // 返回实际删除的文件数，失败返回 -1
    static int enforceQuota(const QString& dir, qint64 maxBytes, double keepRatio = 0.8);

private:
    StorageManager() = delete;
};

#endif // STORAGEMANAGER_H
