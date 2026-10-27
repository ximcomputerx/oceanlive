// SPDX-License-Identifier: Apache-2.0
#pragma once // 头文件只包含一次

#include "platform/win32/DisplayManager.h" // 托盘菜单需要显示器相关类型时保持包含
#include "services/SettingsManager.h" // 改主题时写回设置
#include "ui/MainWindow.h" // 托盘动作打开主窗口
#include "wallpaper/WallpaperManager.h" // 启动、暂停和恢复壁纸

#include <QObject> // 托盘控制器本身是 QObject

class TrayController : public QObject { // 系统托盘图标和右键菜单
    Q_OBJECT // 接收托盘激活和设置变化

public: // 创建并显示托盘
    TrayController(MainWindow* window, SettingsManager* settings, WallpaperManager* wallpapers, QObject* parent = nullptr); // 接上窗口、设置和壁纸
    void show(); // 显示托盘图标

private: // 重建菜单和首次最小化提示
    void rebuild(); // 按当前状态重填菜单，语言变化也会走到这里
    void notifyStillRunning(); // 第一次缩进托盘时提示仍在运行

    MainWindow* m_window = nullptr; // 要唤起的主窗口
    SettingsManager* m_settings = nullptr; // 主题菜单写入这里
    WallpaperManager* m_wallpapers = nullptr; // 菜单按它的运行状态启用
    class QSystemTrayIcon* m_tray = nullptr; // 托盘图标
    class QMenu* m_menu = nullptr; // 右键菜单
    bool m_notified = false; // 气泡只提示一次
}; // 结束托盘控制器
