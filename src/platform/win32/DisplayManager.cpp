// SPDX-License-Identifier: Apache-2.0
#include "platform/win32/DisplayManager.h" // 显示器管理声明

#include "utils/Logger.h" // 记录枚举结果

#include <algorithm> // 排序和相等比较

#ifndef NOMINMAX // 外部没定义时才定义
#define NOMINMAX // 避免 Windows.h 的 min/max 宏
#endif // 结束 NOMINMAX 守卫
#define WIN32_LEAN_AND_MEAN // 少包含无关头
#include <Windows.h> // 显示器枚举
#include <ShellScalingApi.h> // 每台显示器的 DPI

namespace { // 枚举回调只用在本文件

struct Collect { // 回调要把结果写回调用方的数组
    QVector<MonitorDesc>* monitors = nullptr; // 目标列表
}; // 结束收集器

BOOL CALLBACK monitorProc(HMONITOR monitor, HDC, LPRECT, LPARAM param) { // 每块显示器调用一次
    auto* collect = reinterpret_cast<Collect*>(param); // 取出目标列表
    MONITORINFOEXW info{}; // 含设备名的显示器信息
    info.cbSize = sizeof(info); // 必须先填大小
    if (!GetMonitorInfoW(monitor, &info)) { // 读失败
        return TRUE; // 继续下一块
    } // 结束读失败
    MonitorDesc desc; // 一块显示器
    desc.deviceName = QString::fromWCharArray(info.szDevice); // 设备名
    desc.physical = QRect(info.rcMonitor.left, info.rcMonitor.top, info.rcMonitor.right - info.rcMonitor.left, // 左边和宽度
                          info.rcMonitor.bottom - info.rcMonitor.top); // 高度，原点可为负
    desc.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0; // 主显示器标志

    UINT dpiX = 96; // 读不到时的默认
    UINT dpiY = 96; // 读不到时的默认
    if (SUCCEEDED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY))) { // 有效 DPI
        desc.dpiX = static_cast<int>(dpiX); // 水平
        desc.dpiY = static_cast<int>(dpiY); // 垂直
    } // 结束 DPI

    DISPLAY_DEVICEW device{}; // 友好名称
    device.cb = sizeof(device); // 必须先填大小
    if (EnumDisplayDevicesW(info.szDevice, 0, &device, 0)) { // 第一块适配输出
        desc.friendlyName = QString::fromWCharArray(device.DeviceString); // 给人看的名字
    } // 结束友好名
    if (desc.friendlyName.isEmpty()) { // 没有友好名
        desc.friendlyName = desc.deviceName; // 退回设备名
    } // 结束空名

    DEVMODEW mode{}; // 当前显示模式
    mode.dmSize = sizeof(mode); // 必须先填大小
    if (EnumDisplaySettingsW(info.szDevice, ENUM_CURRENT_SETTINGS, &mode)) { // 当前设置
        desc.refreshHz = static_cast<int>(mode.dmDisplayFrequency); // 刷新率
    } // 结束刷新率
    collect->monitors->push_back(desc); // 收进列表
    return TRUE; // 继续枚举
} // 结束显示器回调

}  // 结束匿名命名空间

DisplayManager::DisplayManager(QObject* parent) : QObject(parent) { // 创建时枚举
    refresh(); // 第一次不发变化信号
} // 结束构造

QVector<MonitorDesc> DisplayManager::monitors() const { // 界面填下拉框
    return m_monitors; // 返回副本
} // 结束取列表

void DisplayManager::refresh() { // 重新枚举
    QVector<MonitorDesc> next; // 新列表
    Collect collect{&next}; // 回调写入 next
    EnumDisplayMonitors(nullptr, nullptr, monitorProc, reinterpret_cast<LPARAM>(&collect)); // 全部显示器
    std::sort(next.begin(), next.end(), [](const MonitorDesc& a, const MonitorDesc& b) { // 主屏优先，再按坐标
        if (a.primary != b.primary) { // 主屏不同
            return a.primary; // 主屏排前面
        } // 结束主屏比较
        if (a.physical.x() != b.physical.x()) { // 横坐标不同
            return a.physical.x() < b.physical.x(); // 从左到右
        } // 结束横坐标
        return a.physical.y() < b.physical.y(); // 从上到下
    }); // 结束排序
    const bool changed = next.size() != m_monitors.size() || // 数量变了
                         !std::equal(next.begin(), next.end(), m_monitors.begin(), // 或逐项不同
                                     [](const MonitorDesc& a, const MonitorDesc& b) { // 比较会影响挂接的字段
                                         return a.deviceName == b.deviceName && a.physical == b.physical && // 名字和矩形
                                                a.primary == b.primary && a.dpiX == b.dpiX && a.dpiY == b.dpiY; // 主屏和 DPI
                                     }); // 结束逐项比较
    m_monitors = next; // 换成新列表
    const bool emitChange = m_loaded && changed; // 第一次只记录，不通知
    m_loaded = true; // 之后的变化要通知
    Logger::info(QStringLiteral("DisplayManager"), QStringLiteral("显示器数量 %1").arg(m_monitors.size())); // 日志不翻译
    for (const MonitorDesc& monitor : m_monitors) { // 每块记一行
        Logger::info(QStringLiteral("DisplayManager"), // 类别名保持英文
                     QStringLiteral("%1 %2 primary=%3 %4x%5 @(%6,%7) dpi=%8 refresh=%9") // 固定格式
                         .arg(monitor.deviceName, monitor.friendlyName) // 设备名和友好名
                         .arg(monitor.primary ? 1 : 0) // 主屏用 1 或 0
                         .arg(monitor.physical.width()) // 宽
                         .arg(monitor.physical.height()) // 高
                         .arg(monitor.physical.x()) // 左
                         .arg(monitor.physical.y()) // 上
                         .arg(monitor.dpiX) // DPI
                         .arg(monitor.refreshHz)); // 刷新率
    } // 结束逐块日志
    if (emitChange) { // 热插拔或分辨率变了
        emit displaysChanged(); // 壁纸重新挂接
    } // 结束通知
} // 结束刷新

QVector<MonitorDesc> selectMonitors(const QVector<MonitorDesc>& monitors, int mode, const QString& deviceName) { // 纯函数，测试可直接调
    QVector<MonitorDesc> chosen; // 选中的结果
    if (monitors.isEmpty()) { // 一台都没有
        return chosen; // 空列表
    } // 结束空
    if (mode == 1) { // 只要主显示器
        for (const MonitorDesc& monitor : monitors) { // 找主屏
            if (monitor.primary) { // 命中
                chosen.push_back(monitor); // 只留这一块
                return chosen; // 结束
            } // 结束命中主屏
        } // 结束寻找
        chosen.push_back(monitors.front()); // 没有主屏标记时用第一块
        return chosen; // 结束主屏模式
    } // 结束模式 1
    if (mode == 2) { // 指定设备
        for (const MonitorDesc& monitor : monitors) { // 按设备名
            if (monitor.deviceName.compare(deviceName, Qt::CaseInsensitive) == 0) { // 忽略大小写
                chosen.push_back(monitor); // 命中
                return chosen; // 结束
            } // 结束名字相同
        } // 结束按名寻找
        for (const MonitorDesc& monitor : monitors) { // 名字没有时退回主屏
            if (monitor.primary) { // 主屏
                chosen.push_back(monitor); // 选用
                return chosen; // 结束
            } // 结束主屏
        } // 结束退回主屏
        chosen.push_back(monitors.front()); // 再不行用第一块
        return chosen; // 结束指定模式
    } // 结束模式 2
    return monitors; // 模式 0 和其他值用全部
} // 结束选择

QVector<MonitorDesc> DisplayManager::select(int mode, const QString& deviceName) const { // 带日志的选择
    if (mode == 2) { // 指定设备时检查是否存在
        bool found = false; // 是否找到
        for (const MonitorDesc& monitor : m_monitors) { // 扫当前列表
            if (monitor.deviceName.compare(deviceName, Qt::CaseInsensitive) == 0) { // 忽略大小写
                found = true; // 找到了
                break; // 不用再扫
            } // 结束命中
        } // 结束扫描
        if (!found) { // 设备已拔出
            Logger::warn(QStringLiteral("DisplayManager"), // 类别名
                         QStringLiteral("找不到显示器 %1，改用主显示器").arg(deviceName)); // 日志不进翻译表
        } // 结束缺失警告
    } // 结束指定模式检查
    return selectMonitors(m_monitors, mode, deviceName); // 实际选择
} // 结束成员选择
