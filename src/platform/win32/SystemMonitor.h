// SPDX-License-Identifier: Apache-2.0
#pragma once // 头文件只包含一次

#include <QObject> // 全屏、电池、锁屏信号
#include <QString> // 全屏窗口的标题说明

struct ProcessSample { // 本进程的一小段采样
    double cpuPercent = -1.0; // 第一次或失败时为 -1
    double workingSetMb = -1.0; // 工作集，失败时为 -1
    bool valid = false; // 至少有一项读到了
}; // 结束采样

class SystemMonitor : public QObject { // 听系统消息并轮询全屏和电源
    Q_OBJECT // 启用下面的信号

public: // 查询和轮询
    explicit SystemMonitor(QObject* parent = nullptr); // 安装原生事件过滤器并立刻 poll
    ~SystemMonitor() override; // 卸下过滤器

    bool fullscreenAppActive() const; // 最近一次是否看到全屏应用
    QString fullscreenDescription() const; // 那个窗口的标题和类名，标题本身不翻译
    bool onBattery() const; // 交流电断开时为真
    bool sessionLocked() const; // 工作站锁定
    ProcessSample processSample(); // 工作集和自上次以来的 CPU
    void poll(); // 注册锁屏通知并刷新全屏和电源

signals: // 状态变化
    void fullscreenChanged(bool active); // 全屏应用出现或消失
    void batteryChanged(bool onBattery); // 电源切换
    void sessionLockChanged(bool locked); // 锁定或解锁
    void systemSuspend(); // 即将睡眠
    void systemResume(); // 从睡眠回来
    void displayChangeRequested(); // 分辨率或显示器变化

protected: // 由内部过滤器转进来
    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result); // 处理显示、电源和会话消息

private: // 注册通知和枚举全屏窗口
    void ensureSessionNotification(); // 有顶层窗口后注册一次锁屏通知
    bool detectFullscreen(QString* description) const; // 枚举顶层窗口

    bool m_fullscreen = false; // 当前是否有全屏应用
    bool m_battery = false; // 当前是否用电池
    bool m_locked = false; // 当前是否锁定
    bool m_sessionRegistered = false; // 锁屏通知只注册一次
    QString m_fullscreenDescription; // 最近的全屏窗口说明
    bool m_cpuPrimed = false; // 有上一拍才能算 CPU 百分比
    unsigned long long m_lastProc = 0; // 上一拍进程 CPU 时间
    unsigned long long m_lastSys = 0; // 上一拍系统 CPU 时间
    class NativeFilter; // 把 Qt 原生事件转给本对象
    NativeFilter* m_filter = nullptr; // 事件过滤器
}; // 结束系统监视
