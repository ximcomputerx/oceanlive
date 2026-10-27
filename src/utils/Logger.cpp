// SPDX-License-Identifier: Apache-2.0
#include "utils/Logger.h" // 日志类声明

#include <QDateTime> // 行首时间戳
#include <QDir> // 创建日志目录
#include <QFile> // 日志文件
#include <QMutex> // 多线程写入互斥
#include <QMutexLocker> // 作用域内加锁
#include <QStandardPaths> // 本地数据目录
#include <QTextStream> // 按 UTF-8 写文本

namespace { // 日志文件只在本翻译单元内可见

QMutex g_mutex; // 保护文件和路径
QFile g_file; // 当前打开的日志
QString g_path; // 日志完整路径
bool g_ready = false; // 文件已成功打开

void rotateIfNeeded() { // 超过 2 MB 时改名为 .1
    if (!g_file.isOpen()) { // 还没打开就不用滚动
        return; // 直接返回
    } // 结束未打开判断
    if (g_file.size() < 2 * 1024 * 1024) { // 未到 2 MB
        return; // 继续追加
    } // 结束大小判断
    g_file.close(); // 先关掉才能改名
    const QString previous = g_path + QStringLiteral(".1"); // 上一份日志的路径
    QFile::remove(previous); // 删掉更旧的那份
    QFile::rename(g_path, previous); // 当前日志变成 .1
    g_file.setFileName(g_path); // 重新指向原路径
    g_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text); // 追加打开新文件
} // 结束滚动

}  // 结束匿名命名空间

void Logger::install() { // 创建目录并打开日志
    QMutexLocker lock(&g_mutex); // 安装过程加锁
    if (g_ready) { // 已经打开过
        return; // 不重复打开
    } // 结束重复安装判断
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/logs"); // 本地数据下的 logs
    QDir().mkpath(dir); // 目录不存在就创建
    g_path = dir + QStringLiteral("/oceanlive.log"); // 固定文件名
    g_file.setFileName(g_path); // 绑定路径
    g_ready = g_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text); // 追加写入成功才算就绪
} // 结束安装

QString Logger::logFilePath() { // 给诊断和关于页用
    return g_path; // 尚未安装时为空
} // 结束取路径

void Logger::info(const QString& module, const QString& message) { // 信息级
    write(QStringLiteral("INFO"), module, message); // 级别字面量不翻译
} // 结束信息

void Logger::warn(const QString& module, const QString& message) { // 警告级
    write(QStringLiteral("WARN"), module, message); // 级别字面量不翻译
} // 结束警告

void Logger::error(const QString& module, const QString& message) { // 错误级
    write(QStringLiteral("ERROR"), module, message); // 级别字面量不翻译
} // 结束错误

void Logger::write(const QString& level, const QString& module, const QString& message) { // 拼一行并落盘
    const QString line = QStringLiteral("%1 [%2] [%3] %4") // 时间、级别、模块、正文
                             .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")), // 本地时间到毫秒
                                  level, module, message); // 填入其余三段
    QMutexLocker lock(&g_mutex); // 写文件前加锁
    if (g_ready) { // 只有打开成功才写
        rotateIfNeeded(); // 必要时先滚动
        QTextStream stream(&g_file); // 绑到已打开的文件
        stream.setEncoding(QStringConverter::Utf8); // 中文日志用 UTF-8
        stream << line << '\n'; // 一行一条
        stream.flush(); // 立刻落盘，崩溃前尽量留下
    } // 结束可写判断
} // 结束写入
