// SPDX-License-Identifier: Apache-2.0
#include "platform/win32/SystemMonitor.h" // 系统监视声明

#include "utils/FullscreenHeuristic.h" // 判断窗口是否像全屏应用
#include "utils/Logger.h" // 睡眠、锁屏和电源日志

#include <QAbstractNativeEventFilter> // 接收 Windows 消息
#include <QApplication> // 顶层控件，用来注册会话通知
#include <QCoreApplication> // 安装事件过滤器
#include <QWidget> // 取原生句柄

#ifndef NOMINMAX // 外部没定义时才定义
#define NOMINMAX // 避免 Windows.h 的 min/max 宏
#endif // 结束 NOMINMAX 守卫
#define WIN32_LEAN_AND_MEAN // 少包含无关头
#include <Windows.h> // 窗口和电源
#include <Psapi.h> // 工作集
#include <Wtsapi32.h> // 会话锁定通知
#include <dwmapi.h> // 窗口是否被隐藏

namespace { // 枚举全屏窗口的帮手

unsigned long long fileTimeValue(const FILETIME& time) { // FILETIME 收成 64 位
    ULARGE_INTEGER value{}; // 方便拼高低位
    value.LowPart = time.dwLowDateTime; // 低 32 位
    value.HighPart = time.dwHighDateTime; // 高 32 位
    return value.QuadPart; // 100 纳秒计数
} // 结束转时间

QString windowText(HWND hwnd) { // 窗口标题，不翻译
    wchar_t buffer[512] = {}; // 标题缓冲
    GetWindowTextW(hwnd, buffer, 511); // 留一个结尾
    return QString::fromWCharArray(buffer); // 转成 QString
} // 结束标题

QString windowClass(HWND hwnd) { // Win32 类名
    wchar_t buffer[256] = {}; // 类名缓冲
    GetClassNameW(hwnd, buffer, 255); // 留一个结尾
    return QString::fromWCharArray(buffer); // 转成 QString
} // 结束类名

bool windowCloaked(HWND hwnd) { // 虚拟桌面等方式隐藏的窗口
    BOOL cloaked = FALSE; // 默认未隐藏
    return SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked; // 查询成功且为真
} // 结束隐藏查询

struct FullscreenHit { // 枚举时记下第一块全屏窗口
    bool found = false; // 是否找到
    QString description; // 标题和类名
}; // 结束命中

BOOL CALLBACK enumFullscreen(HWND hwnd, LPARAM param) { // 每个顶层窗口
    auto* hit = reinterpret_cast<FullscreenHit*>(param); // 结果写这里
    if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) { // 不可见或最小化
        return TRUE; // 继续
    } // 结束可见性
    const LONG style = GetWindowLongW(hwnd, GWL_STYLE); // 窗口样式
    if (style & WS_CHILD) { // 子窗口
        return TRUE; // 不参与
    } // 结束子窗口
    RECT rect{}; // 窗口矩形
    if (!GetWindowRect(hwnd, &rect)) { // 读不到
        return TRUE; // 继续
    } // 结束无矩形
    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST); // 最近的显示器
    MONITORINFO info{}; // 该显示器矩形
    info.cbSize = sizeof(info); // 必须先填大小
    if (!monitor || !GetMonitorInfoW(monitor, &info)) { // 没有显示器信息
        return TRUE; // 继续
    } // 结束无显示器
    DWORD pid = 0; // 窗口所属进程
    GetWindowThreadProcessId(hwnd, &pid); // 取出进程号
    WindowMetrics metrics; // 交给全屏启发式
    metrics.window = QRect(rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top); // 窗口矩形
    metrics.monitor = QRect(info.rcMonitor.left, info.rcMonitor.top, info.rcMonitor.right - info.rcMonitor.left, // 显示器左和宽
                            info.rcMonitor.bottom - info.rcMonitor.top); // 显示器高
    metrics.iconic = false; // 前面已排除最小化
    metrics.visible = true; // 前面已要求可见
    metrics.cloaked = windowCloaked(hwnd); // 是否被系统隐藏
    metrics.child = false; // 前面已排除子窗口
    metrics.ours = pid == GetCurrentProcessId(); // 是不是本进程
    metrics.className = windowClass(hwnd); // 类名
    metrics.title = windowText(hwnd); // 标题原样保留
    if (!isLikelyFullscreenApp(metrics)) { // 不像全屏应用
        return TRUE; // 继续找
    } // 结束不是全屏
    hit->found = true; // 找到一块
    hit->description = QStringLiteral("%1 (%2)").arg(metrics.title, metrics.className); // 标题加类名
    return FALSE; // 停止枚举
} // 结束全屏枚举

}  // 结束匿名命名空间

class SystemMonitor::NativeFilter : public QAbstractNativeEventFilter { // 把消息转给监视器
public: // 构造和过滤
    explicit NativeFilter(SystemMonitor* owner) : m_owner(owner) {} // 记下主人

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override { // Qt 回调
        return m_owner->nativeEventFilter(eventType, message, result); // 交给监视器，不吞消息
    } // 结束转发

private: // 主人指针
    SystemMonitor* m_owner = nullptr; // 不拥有对方
}; // 结束过滤器

SystemMonitor::SystemMonitor(QObject* parent) : QObject(parent) { // 开始监视
    m_filter = new NativeFilter(this); // 创建过滤器
    QCoreApplication::instance()->installNativeEventFilter(m_filter); // 接收 Windows 消息
    poll(); // 立刻读一次全屏和电源
} // 结束构造

SystemMonitor::~SystemMonitor() { // 退出时卸下
    if (QCoreApplication::instance()) { // 应用还在
        QCoreApplication::instance()->removeNativeEventFilter(m_filter); // 不再收消息
    } // 结束应用仍在
    delete m_filter; // 释放过滤器
    m_filter = nullptr; // 指针清空
} // 结束析构

bool SystemMonitor::nativeEventFilter(const QByteArray& eventType, void* message, qintptr*) { // 处理三类消息
    if (eventType != "windows_generic_MSG" && eventType != "windows_dispatcher_MSG") { // 不是 Windows 消息
        return false; // 交给别人
    } // 结束类型过滤
    const MSG* msg = static_cast<MSG*>(message); // 原生消息
    if (msg->message == WM_DISPLAYCHANGE) { // 分辨率或显示器变了
        emit displayChangeRequested(); // 请壁纸重新枚举
        return false; // 不吞掉
    } // 结束显示变化
    if (msg->message == WM_POWERBROADCAST) { // 电源广播
        if (msg->wParam == PBT_APMSUSPEND) { // 即将睡眠
            Logger::info(QStringLiteral("SystemMonitor"), QStringLiteral("系统进入睡眠")); // 日志不翻译
            emit systemSuspend(); // 壁纸可暂停
        } else if (msg->wParam == PBT_APMRESUMEAUTOMATIC || msg->wParam == PBT_APMRESUMESUSPEND) { // 两种恢复
            Logger::info(QStringLiteral("SystemMonitor"), QStringLiteral("系统已恢复")); // 日志不翻译
            emit systemResume(); // 壁纸可恢复
        } // 结束恢复
        return false; // 不吞掉
    } // 结束电源
    if (msg->message == WM_WTSSESSION_CHANGE) { // 会话锁定变化
        if (msg->wParam == WTS_SESSION_LOCK) { // 锁定
            if (!m_locked) { // 避免重复
                m_locked = true; // 记下
                Logger::info(QStringLiteral("SystemMonitor"), QStringLiteral("会话已锁定")); // 日志不翻译
                emit sessionLockChanged(true); // 通知
            } // 结束新锁定
        } else if (msg->wParam == WTS_SESSION_UNLOCK) { // 解锁
            if (m_locked) { // 之前是锁着的
                m_locked = false; // 记下
                Logger::info(QStringLiteral("SystemMonitor"), QStringLiteral("会话已解锁")); // 日志不翻译
                emit sessionLockChanged(false); // 通知
            } // 结束新解锁
        } // 结束解锁
    } // 结束会话消息
    return false; // 一律不吞消息
} // 结束原生过滤

void SystemMonitor::ensureSessionNotification() { // 注册锁屏通知
    if (m_sessionRegistered) { // 已经注册
        return; // 不再注册
    } // 结束已注册
    const QWidgetList widgets = QApplication::topLevelWidgets(); // 需要一个顶层句柄
    for (QWidget* widget : widgets) { // 找一个有句柄的
        if (!widget->windowHandle()) { // 还没原生窗口
            continue; // 下一个
        } // 结束无句柄
        const HWND hwnd = reinterpret_cast<HWND>(widget->winId()); // 取出 HWND
        if (WTSRegisterSessionNotification(hwnd, NOTIFY_FOR_THIS_SESSION)) { // 只听本会话
            m_sessionRegistered = true; // 成功一次即可
            Logger::info(QStringLiteral("SystemMonitor"), QStringLiteral("已注册会话锁定通知")); // 日志不翻译
            return; // 结束
        } // 结束注册成功
    } // 结束寻找窗口
} // 结束确保通知

bool SystemMonitor::detectFullscreen(QString* description) const { // 扫一遍顶层窗口
    FullscreenHit hit; // 结果
    EnumWindows(enumFullscreen, reinterpret_cast<LPARAM>(&hit)); // 找到第一块就停
    if (description) { // 调用方要说明
        *description = hit.description; // 可能为空
    } // 结束填写说明
    return hit.found; // 是否有全屏应用
} // 结束检测

void SystemMonitor::poll() { // 定时器调用
    ensureSessionNotification(); // 窗口出现后再注册锁屏
    QString description; // 全屏窗口说明
    const bool fullscreen = detectFullscreen(&description); // 扫窗口
    if (fullscreen != m_fullscreen || description != m_fullscreenDescription) { // 状态或窗口变了
        m_fullscreen = fullscreen; // 更新
        m_fullscreenDescription = description; // 更新说明
        Logger::info(QStringLiteral("SystemMonitor"), fullscreen ? QStringLiteral("检测到全屏应用: %1").arg(description) // 有应用
                                                                 : QStringLiteral("没有全屏应用")); // 没有应用
        emit fullscreenChanged(fullscreen); // 壁纸可暂停或恢复
    } // 结束全屏变化

    SYSTEM_POWER_STATUS power{}; // 电源状态
    bool battery = false; // 默认交流电
    if (GetSystemPowerStatus(&power)) { // 读成功
        battery = power.ACLineStatus == 0; // 0 表示离线，用电池
    } // 结束读电源
    if (battery != m_battery) { // 电源变了
        m_battery = battery; // 更新
        Logger::info(QStringLiteral("SystemMonitor"), battery ? QStringLiteral("当前使用电池") // 电池
                                                              : QStringLiteral("当前使用交流电")); // 交流电
        emit batteryChanged(battery); // 按策略降帧或暂停
    } // 结束电源变化
} // 结束轮询

bool SystemMonitor::fullscreenAppActive() const { // 查询
    return m_fullscreen; // 最近一次 poll 的结果
} // 结束全屏查询

QString SystemMonitor::fullscreenDescription() const { // 诊断显示别的程序标题
    return m_fullscreenDescription; // 不翻译标题
} // 结束说明

bool SystemMonitor::onBattery() const { // 查询
    return m_battery; // 最近一次电源
} // 结束电池查询

bool SystemMonitor::sessionLocked() const { // 查询
    return m_locked; // 由会话消息更新
} // 结束锁定查询

ProcessSample SystemMonitor::processSample() { // 诊断用的本进程开销
    ProcessSample sample; // 默认无效
    PROCESS_MEMORY_COUNTERS_EX counters{}; // 含工作集
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), // 本进程
                             sizeof(counters))) { // 大小
        sample.workingSetMb = static_cast<double>(counters.WorkingSetSize) / (1024.0 * 1024.0); // 字节转 MB
        sample.valid = true; // 内存读到了
    } // 结束内存

    FILETIME idle{}, kernel{}, user{}; // 系统时间，空闲不用
    FILETIME creation{}, exitTime{}, procKernel{}, procUser{}; // 进程时间
    if (GetSystemTimes(&idle, &kernel, &user) && // 系统 CPU
        GetProcessTimes(GetCurrentProcess(), &creation, &exitTime, &procKernel, &procUser)) { // 进程 CPU
        const unsigned long long sys = fileTimeValue(kernel) + fileTimeValue(user); // 系统内核加用户
        const unsigned long long proc = fileTimeValue(procKernel) + fileTimeValue(procUser); // 进程内核加用户
        if (m_cpuPrimed && sys > m_lastSys) { // 有上一拍且系统时间前进
            sample.cpuPercent = 100.0 * static_cast<double>(proc - m_lastProc) / static_cast<double>(sys - m_lastSys); // 这段的占比
            sample.valid = true; // CPU 也有效
        } else { // 第一拍或时间没走
            sample.cpuPercent = -1.0; // 表示不可用
        } // 结束百分比
        m_lastSys = sys; // 留下一拍
        m_lastProc = proc; // 留下一拍
        m_cpuPrimed = true; // 下一拍可以算
    } else { // 时间接口失败
        sample.cpuPercent = -1.0; // CPU 不可用
    } // 结束 CPU
    return sample; // 交给诊断
} // 结束采样
