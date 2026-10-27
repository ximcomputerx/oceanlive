// SPDX-License-Identifier: Apache-2.0
#include "platform/win32/Autostart.h" // 开机启动声明

#include "utils/Logger.h" // 记录注册表结果

#include <QCoreApplication> // 可执行路径和可翻译错误
#include <QDir> // 路径分隔符转成 Windows 形式
#include <QSettings> // 用原生格式写注册表

namespace { // Run 键路径只在本文件使用

QSettings runKey() { // 当前用户的启动项
    return QSettings(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"), // 不需要管理员
                     QSettings::NativeFormat); // 注册表而不是 ini
} // 结束打开 Run 键

}  // 结束匿名命名空间

QString commandLineForExecutable(const QString& exePath) { // 开机时最小化启动
    QString native = QDir::toNativeSeparators(exePath).trimmed(); // 反斜杠路径
    if (native.startsWith(QLatin1Char('"')) && native.endsWith(QLatin1Char('"')) && native.size() >= 2) { // 已经带引号
        native = native.mid(1, native.size() - 2); // 先去掉，避免双层引号
    } // 结束去引号
    native.replace(QLatin1Char('"'), QString()); // 路径里的引号删掉
    return QStringLiteral("\"%1\" --minimized").arg(native); // 重新包一层并带最小化参数
} // 结束命令行

QString Autostart::readValue(const QString& valueName) { // 读某个值
    QSettings key = runKey(); // 打开 Run
    return key.value(valueName).toString(); // 不存在则空串
} // 结束读取

bool Autostart::writeValue(const QString& valueName, const QString& command, QString* error) { // 写入并核对
    QSettings key = runKey(); // 打开 Run
    key.setValue(valueName, command); // 写入命令
    key.sync(); // 立刻提交
    if (key.status() != QSettings::NoError) { // 注册表拒绝
        if (error) { // 调用方要说明
            *error = QCoreApplication::translate("Autostart", "写入启动项失败。"); // 可翻译
        } // 结束填写错误
        return false; // 写入失败
    } // 结束状态错误
    const QString stored = key.value(valueName).toString(); // 再读一次
    if (stored != command) { // 读回和写入不一致
        if (error) { // 调用方要说明
            *error = QCoreApplication::translate("Autostart", "启动项写入后读回不一致。"); // 可翻译
        } // 结束填写不一致
        return false; // 视为失败
    } // 结束读回核对
    return true; // 写入可靠
} // 结束写入

bool Autostart::removeValue(const QString& valueName, QString* error) { // 删除启动项
    QSettings key = runKey(); // 打开 Run
    if (!key.contains(valueName)) { // 本来就没有
        return true; // 目标状态已满足
    } // 结束不存在
    key.remove(valueName); // 删除
    key.sync(); // 提交
    if (key.contains(valueName)) { // 删完还在
        if (error) { // 调用方要说明
            *error = QCoreApplication::translate("Autostart", "未能删除启动项 %1。").arg(valueName); // 带上值名
        } // 结束填写删除失败
        return false; // 删除失败
    } // 结束仍存在
    return true; // 已删除
} // 结束删除

bool Autostart::isEnabled() { // 界面勾选状态
    return !readValue().trimmed().isEmpty(); // 默认值名 OceanLive 非空
} // 结束查询

bool Autostart::setEnabled(bool enabled, QString* error) { // 按开关更新 OceanLive
    if (!enabled) { // 关闭开机启动
        const bool ok = removeValue(QString::fromLatin1(kRunValueName), error); // 只删这个名字
        Logger::info(QStringLiteral("Autostart"), ok ? QStringLiteral("已移除 HKCU Run\\OceanLive") // 成功日志不翻译
                                                     : QStringLiteral("移除启动项失败")); // 失败日志不翻译
        return ok; // 把删除结果交回
    } // 结束关闭
    const QString command = commandLineForExecutable(QCoreApplication::applicationFilePath()); // 当前 exe 加 --minimized
    const bool ok = writeValue(QString::fromLatin1(kRunValueName), command, error); // 值名保持 OceanLive
    Logger::info(QStringLiteral("Autostart"), QStringLiteral("设置启动项 %1 -> %2").arg(ok ? QStringLiteral("成功") // 成功或失败
                                                                                          : QStringLiteral("失败"), // 第二段是命令
                                                                                     command)); // 命令原文
    return ok; // 写入结果
} // 结束设置开关
