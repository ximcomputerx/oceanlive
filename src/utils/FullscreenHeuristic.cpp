// SPDX-License-Identifier: Apache-2.0
#include "utils/FullscreenHeuristic.h" // 全屏判断声明

namespace { // 覆盖和壳窗口判断只在本文件使用

bool coversMonitor(const QRect& window, const QRect& monitor) { // 窗口是否盖住整块显示器
    constexpr int kTolerance = 8; // 允许 8 像素误差
    return window.left() <= monitor.left() + kTolerance && window.top() <= monitor.top() + kTolerance && // 左边和上边贴住
           window.right() >= monitor.right() - kTolerance && window.bottom() >= monitor.bottom() - kTolerance; // 右边和下边贴住
} // 结束覆盖判断

bool isShellClass(const QString& className) { // 桌面壳和任务栏不是全屏应用
    return className == QLatin1String("Progman") || className == QLatin1String("WorkerW") || // 桌面和壁纸宿主
           className == QLatin1String("Shell_TrayWnd") || className == QLatin1String("Shell_SecondaryTrayWnd") || // 主副任务栏
           className == QLatin1String("SHELLDLL_DefView") || className == QLatin1String("SysListView32"); // 图标层和列表
} // 结束壳类判断

}  // 结束匿名命名空间

bool isLikelyFullscreenApp(const WindowMetrics& metrics) { // 可见窗口是否像独占或无边框全屏
    if (!metrics.visible || metrics.iconic || metrics.cloaked || metrics.child) { // 不可见、最小化、隐藏或子窗口
        return false; // 这些都不算全屏应用
    } // 结束可见性过滤
    if (metrics.monitor.width() < 64 || metrics.monitor.height() < 64) { // 显示器矩形过小
        return false; // 无法判断覆盖
    } // 结束尺寸过滤
    if (isShellClass(metrics.className)) { // 桌面、任务栏、图标层
        return false; // 壳窗口不算
    } // 结束壳过滤
    if (metrics.title == QLatin1String("OceanLiveWallpaper")) { // 我们自己的壁纸窗口
        return false; // 避免把自己当成全屏应用
    } // 结束壁纸标题过滤
    if (metrics.ours && metrics.title != QLatin1String("OceanLiveFullscreenProbe")) { // 本进程窗口，探针除外
        return false; // 主窗口最大化不算外来全屏
    } // 结束本进程过滤
    return coversMonitor(metrics.window, metrics.monitor); // 贴住显示器四边才算
} // 结束全屏判断
