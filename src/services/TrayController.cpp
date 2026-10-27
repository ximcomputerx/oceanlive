// SPDX-License-Identifier: Apache-2.0
#include "services/TrayController.h" // 托盘声明

#include "ui/IconFactory.h" // 托盘图标

#include <QAction> // 菜单项
#include <QApplication> // 菜单文案的翻译上下文来自 QObject::tr
#include <QMenu> // 右键菜单和主题子菜单
#include <QSystemTrayIcon> // 托盘图标控件

TrayController::TrayController(MainWindow* window, SettingsManager* settings, WallpaperManager* wallpapers, QObject* parent) // 接上三方
    : QObject(parent), m_window(window), m_settings(settings), m_wallpapers(wallpapers) { // 保存指针
    m_tray = new QSystemTrayIcon(IconFactory::appIcon(), this); // 图标随本对象释放
    m_tray->setToolTip(QStringLiteral("OceanLive")); // 产品名不翻译
    m_menu = new QMenu(); // 右键菜单，重建时清空
    m_tray->setContextMenu(m_menu); // 绑到图标
    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) { // 单击或双击
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) { // 左键单击或双击
            m_window->bringToFront(); // 打开主窗口
        } // 结束点击种类
    }); // 结束激活回调
    connect(m_window, &MainWindow::minimizedToTray, this, &TrayController::notifyStillRunning); // 关窗口进托盘时提示
    connect(m_wallpapers, &WallpaperManager::stateChanged, this, &TrayController::rebuild); // 运行状态变了就重填菜单
    connect(m_settings, &SettingsManager::changed, this, &TrayController::rebuild); // 语言或主题变化也重填
    rebuild(); // 先做一份初始菜单
} // 结束构造

void TrayController::show() { // 主函数创建完后显示
    m_tray->show(); // 出现在系统托盘
} // 结束显示

void TrayController::notifyStillRunning() { // 第一次最小化到托盘
    if (m_notified || !QSystemTrayIcon::supportsMessages()) { // 已提示过，或系统不支持气泡
        return; // 不再显示
    } // 结束跳过
    m_notified = true; // 只提示一次
    m_tray->showMessage(tr("OceanLive"), // 标题用产品名，仍走 tr 以便目录里有条目
                        tr("程序仍在托盘中运行。右键托盘图标可以选择退出。"), // 说明怎么退出
                        QSystemTrayIcon::Information, 3500); // 信息图标，显示 3.5 秒
} // 结束气泡

void TrayController::rebuild() { // 按当前语言和状态重做菜单
    m_menu->clear(); // 丢掉旧动作
    QAction* showAction = m_menu->addAction(tr("显示主窗口")); // 可翻译
    connect(showAction, &QAction::triggered, m_window, &MainWindow::bringToFront); // 唤起窗口

    QAction* startAction = m_menu->addAction(tr("启动壁纸")); // 可翻译
    startAction->setEnabled(m_wallpapers->state() == RunState::Stopped); // 只有停着才能启动
    connect(startAction, &QAction::triggered, m_wallpapers, &WallpaperManager::start); // 开始挂接

    QAction* pauseAction = m_menu->addAction(tr("暂停动画")); // 可翻译
    pauseAction->setEnabled(m_wallpapers->state() == RunState::Running); // 正在跑才能暂停
    connect(pauseAction, &QAction::triggered, m_wallpapers, &WallpaperManager::pauseByUser); // 用户暂停

    QAction* resumeAction = m_menu->addAction(tr("恢复动画")); // 可翻译
    resumeAction->setEnabled(m_wallpapers->userPaused()); // 仅用户暂停时可恢复
    connect(resumeAction, &QAction::triggered, m_wallpapers, &WallpaperManager::resumeByUser); // 用户恢复

    QMenu* themes = m_menu->addMenu(tr("主题")); // 子菜单
    const OceanTheme current = m_settings->current().ocean.theme; // 当前主题打勾
    const OceanTheme values[] = {OceanTheme::Sunny, OceanTheme::Sunset, OceanTheme::Deep}; // 三个内置主题
    for (OceanTheme theme : values) { // 每个主题一项
        QAction* action = themes->addAction(oceanThemeName(theme)); // 名字随语言变化
        action->setCheckable(true); // 可打勾
        action->setChecked(theme == current); // 当前主题打勾
        connect(action, &QAction::triggered, this, [this, theme] { // 选中后写设置
            AppSettings settings = m_settings->current(); // 拷贝当前设置
            settings.ocean = settings.ocean.withTheme(theme); // 只换主题默认，保留滑块
            m_settings->apply(settings); // 写入并通知
        }); // 结束主题动作
    } // 结束主题循环

    QAction* settingsAction = m_menu->addAction(tr("设置")); // 可翻译
    connect(settingsAction, &QAction::triggered, m_window, [this] { // 先显示窗口再开设置
        m_window->bringToFront(); // 主窗口到前面
        m_window->openSettings(); // 打开设置对话框
    }); // 结束设置动作

    m_menu->addSeparator(); // 退出单独隔开
    QAction* quitAction = m_menu->addAction(tr("退出")); // 可翻译
    connect(quitAction, &QAction::triggered, m_window, &MainWindow::requestQuit); // 真正退出进程
} // 结束重建菜单
