// SPDX-License-Identifier: Apache-2.0
#pragma once // 头文件只包含一次

#include "render/OceanParams.h" // 海洋参数随设置一起保存

#include <QObject> // 设置变化时发信号
#include <QString> // 显示器设备和语言偏好

struct AppSettings { // 一份完整的用户设置
    OceanParams ocean; // 主题、海面和视频参数
    int monitorMode = 0; // 0 全部，1 主显示器，2 指定显示器
    QString monitorDevice; // 指定显示器时的设备名
    bool pauseOnFullscreen = true; // 有全屏应用时暂停动画
    int batteryPolicy = 0; // 0 不处理，1 降到 30 FPS，2 暂停
    bool launchAtStartup = false; // 开机启动
    bool startMinimized = false; // 启动后只留托盘
    bool resumeOnLaunch = true; // 启动时恢复壁纸
    bool showDiagnostics = false; // 主窗口是否显示诊断
    QString language = QStringLiteral("system"); // 界面语言：system、zh_CN 或 en
}; // 结束设置结构

class SettingsManager : public QObject { // 读写 OceanLive.ini 并通知界面
    Q_OBJECT // 启用 changed 和 errorOccurred

public: // 读取、应用和查询路径
    explicit SettingsManager(QObject* parent = nullptr); // 打开 ini 并同步开机启动

    AppSettings current() const; // 返回内存中的当前设置
    bool apply(const AppSettings& settings, QString* error = nullptr); // 校验后写入，失败时填 error
    QString configPath() const; // ini 的完整路径

signals: // 写入成功或开机启动失败
    void changed(); // 有字段相对上次发生变化
    void errorOccurred(const QString& message); // 开机启动写入失败

private: // 校验、读盘和写盘
    AppSettings sanitize(AppSettings settings) const; // 把越界值夹回合法范围
    void read(); // 从 ini 填入 m_current
    void write(const AppSettings& settings); // 把设置写回 ini

    class QSettings* m_store = nullptr; // ini 存储器
    AppSettings m_current; // 已应用的设置
}; // 结束设置管理
