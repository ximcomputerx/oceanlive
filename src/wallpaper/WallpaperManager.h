// SPDX-License-Identifier: Apache-2.0
#pragma once // 头文件只包含一次

#include "platform/win32/DisplayManager.h" // 选择要铺壁纸的显示器
#include "platform/win32/SystemMonitor.h" // 全屏、电池和会话状态
#include "platform/win32/WorkerWHost.h" // 桌面宿主句柄
#include "services/SettingsManager.h" // 当前设置
#include "wallpaper/WallpaperWindow.h" // 嵌进桌面的窗口

#include <QObject> // 信号与对象树
#include <QTimer> // 定时轮询宿主
#include <QVector> // 每个显示器一个窗口

enum class RunState { Stopped, Running, Paused }; // 停止、运行或暂停

enum class PauseReason { None, User, Fullscreen, Battery, Session, Reattaching }; // 暂停原因，查询时只返回优先级最高的一个

class VideoPlayer; // 进程里只有一个播放器，这里前向声明

class WallpaperManager : public QObject { // 负责挂接、暂停和重新挂上壁纸
    Q_OBJECT // 启用元对象才能发信号

public: // 启停和对外状态
    WallpaperManager(SettingsManager* settings, DisplayManager* displays, QObject* parent = nullptr); // 不拥有设置和显示器
    void setVideoPlayer(VideoPlayer* video); // 注入那一个共享播放器
    ~WallpaperManager() override; // 卸下窗口并释放播放器

    void start(); // 定位宿主并创建壁纸窗口
    void stop(); // 停止视频并销毁壁纸窗口
    void pauseByUser(); // 用户暂停，退出全屏也不会把它清掉
    void resumeByUser(); // 只取消用户暂停
    void pollSystem(); // 看宿主是否还在，并轮询系统

    RunState state() const; // 当前运行状态
    PauseReason pauseReason() const; // 当前生效的暂停原因
    bool userPaused() const; // 是否仍是用户暂停
    QString statusText() const; // 界面上的一行状态
    QString lastError() const; // 最近一次失败原因
    QString diagnosticsText() const; // 多行诊断
    DesktopHost host() const; // 当前桌面宿主
    int windowCount() const; // 已经挂上的窗口数
    WallpaperWindow* windowAt(int index) const; // 越界时返回空

signals: // 通知界面刷新
    void stateChanged(); // 运行或暂停发生变化
    void errorOccurred(const QString& message); // 启动或重新挂接失败

private: // 布局、参数和宿主丢失后的处理
    void onSettingsChanged(); // 设置变了就推参数，显示器变了再重排
    void relayout(); // 拆掉窗口再挂，并保留用户暂停
    bool createWindows(); // 每个选中显示器一个窗口，共用一个播放器
    void destroyWindows(); // 先脱离桌面再删除窗口
    void pushParams(); // 把海面参数送进每个视图
    void updateAnimationState(); // 汇总暂停标志并同步视频
    void syncVideoPlayback(); // 只有视频模式才让唯一播放器出画
    void applyBattery(bool onBattery); // 策略 1 限到 30 帧，策略 2 则暂停
    void reattach(); // 宿主还在时只重新嵌入，不先整页重建
    void reportStartFailure(const QString& message); // 记下失败并发出错误

    SettingsManager* m_settings = nullptr; // 不拥有
    DisplayManager* m_displays = nullptr; // 不拥有
    VideoPlayer* m_video = nullptr; // 指向进程内唯一播放器，可为空
    SystemMonitor* m_monitor = nullptr; // 由本对象拥有
    QTimer* m_poll = nullptr; // 一秒查一次
    QVector<WallpaperWindow*> m_windows; // 已嵌入的壁纸窗口
    DesktopHost m_host; // 最近一次定位结果
    RunState m_state = RunState::Stopped; // 初始是停止
    bool m_userPaused = false; // 用户主动暂停
    bool m_fullscreenPaused = false; // 检测到全屏应用
    bool m_batteryPaused = false; // 电池策略要求暂停
    bool m_batteryThrottle = false; // 电池策略要求降帧
    bool m_sessionPaused = false; // 锁屏或睡眠
    bool m_reattaching = false; // 正在重新寻找宿主
    bool m_started = false; // 用户期望壁纸保持启动
    int m_appliedMonitorMode = -1; // 已铺上的显示器模式，-1 表示还没有
    QString m_appliedMonitorDevice; // 已铺上的设备名
    int m_hostMisses = 0; // 连续找不到宿主的次数
    bool m_hostErrorReported = false; // 连续失败只提示一次
    QString m_error; // 最近的错误文本
    RenderStats m_stats; // 最近一帧的渲染统计
}; // 结束壁纸管理类
