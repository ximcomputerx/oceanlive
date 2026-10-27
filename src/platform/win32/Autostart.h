// SPDX-License-Identifier: Apache-2.0
#pragma once // 头文件只包含一次

#include <QString> // 命令行和错误文本

QString commandLineForExecutable(const QString& exePath); // 生成带 --minimized 的启动命令

class Autostart { // 读写当前用户的 Run 键
public: // 名字固定，测试可以换别的值名
    static constexpr const char* kRunValueName = "OceanLive"; // 注册表值名，不翻译

    static QString readValue(const QString& valueName = QString::fromLatin1(kRunValueName)); // 读命令行，没有则为空
    static bool writeValue(const QString& valueName, const QString& command, QString* error); // 写入并读回核对
    static bool removeValue(const QString& valueName, QString* error); // 删除，本来没有也算成功
    static bool isEnabled(); // 值非空即视为已启用
    static bool setEnabled(bool enabled, QString* error); // 按开关写入或删除 OceanLive
}; // 结束开机启动
