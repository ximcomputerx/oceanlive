// SPDX-License-Identifier: Apache-2.0
#pragma once // 头文件只包含一次

#include <QRect> // 窗口和显示器矩形
#include <QString> // 窗口类名和标题

struct WindowMetrics { // 判断全屏时用到的窗口事实
    QRect window; // 窗口外接矩形
    QRect monitor; // 所在显示器的矩形
    bool iconic = false; // 最小化时为真
    bool visible = true; // 可见时为真
    bool cloaked = false; // 系统隐藏该窗口时为真
    bool child = false; // 子窗口不参与全屏判断
    bool ours = false; // 属于本进程时为真
    QString className; // Win32 窗口类名
    QString title; // 窗口标题
}; // 结束窗口事实

// 可见窗口是否像独占或无边框全屏那样盖住整块显示器。
// 最大化但任务栏仍可见的窗口不算。
bool isLikelyFullscreenApp(const WindowMetrics& metrics); // 像全屏应用时返回真
