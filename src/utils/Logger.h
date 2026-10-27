// SPDX-License-Identifier: Apache-2.0
#pragma once // 头文件只包含一次

#include <QString> // 日志正文用 QString

class Logger { // 把运行记录写到本地日志文件
public: // 对外的安装和分级写入
    static void install(); // 打开日志文件，重复调用无效果
    static void info(const QString& module, const QString& message); // 写一条信息
    static void warn(const QString& module, const QString& message); // 写一条警告
    static void error(const QString& module, const QString& message); // 写一条错误
    static QString logFilePath(); // 返回当前日志路径

private: // 实际拼行和落盘
    static void write(const QString& level, const QString& module, const QString& message); // 按级别写一行
}; // 结束日志类
