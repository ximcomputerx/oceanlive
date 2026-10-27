// SPDX-License-Identifier: Apache-2.0
#pragma once // 头文件只包含一次

#include "platform/win32/DisplayManager.h" // 显示器枚举与几何
#include "services/SettingsManager.h" // 读写应用设置
#include "wallpaper/WallpaperManager.h" // 壁纸启停与状态

class VideoPlayer; // 视频播放器前置声明

#include <QMainWindow> // 主窗口基类

namespace Ui { // 界面生成类所在命名空间
class MainWindow; // 前置声明生成的主窗口界面
} // 结束界面命名空间

class MainWindow : public QMainWindow { // 主窗口，负责预览和快捷控制
    Q_OBJECT // 启用信号槽元对象

public: // 对外接口
    MainWindow(SettingsManager* settings, WallpaperManager* wallpapers, DisplayManager* displays, QWidget* parent = nullptr); // 构造并装配主界面
    ~MainWindow() override; // 析构并释放界面对象

    void setVideoPlayer(VideoPlayer* video); // 绑定预览用的播放器
    void bringToFront(); // 还原并激活到前台
    void requestQuit(); // 标记真正退出并结束程序
    void openSettings(); // 打开设置对话框

signals: // 对外信号
    void minimizedToTray(); // 关闭后已缩进托盘

protected: // 窗口事件
    void changeEvent(QEvent* event) override; // 语言切换时重译界面
    void closeEvent(QCloseEvent* event) override; // 关闭时缩进托盘而不是退出
    void showEvent(QShowEvent* event) override; // 显示时启用深色标题栏

private: // 内部实现
    void bind(); // 连接按钮、滑条和设置信号
    void refillChoiceCombos(); // 按当前语言重填下拉项
    void reloadFromSettings(); // 用已保存设置回填界面
    void reloadMonitors(); // 重填显示器下拉列表
    void pushQuickSettings(); // 把快捷控件写回设置
    void syncVideoPreview(); // 按模式同步预览播放
    void refreshState(); // 按运行状态刷新按钮和标题
    void refreshDiagnostics(); // 刷新诊断文本

    Ui::MainWindow* ui = nullptr; // 生成的界面对象
    SettingsManager* m_settings = nullptr; // 设置管理器，不拥有
    WallpaperManager* m_wallpapers = nullptr; // 壁纸管理器，不拥有
    DisplayManager* m_displays = nullptr; // 显示器管理器，不拥有
    class QTimer* m_diagTimer = nullptr; // 半秒刷新诊断的定时器
    VideoPlayer* m_video = nullptr; // 预览播放器，不拥有
    QString m_lastVideoPath; // 上次已同步的视频路径
    bool m_quit = false; // 为真时关闭窗口会退出
    bool m_loading = false; // 回填界面时挡住回写
    bool m_seenRunning = false; // 是否曾经进入运行状态
    bool m_suppressVideoPreview = false; // 停止后不再自动预览视频
}; // 结束主窗口类
