// SPDX-License-Identifier: Apache-2.0
#include "platform/win32/DarkTitle.h" // 深色标题栏声明

#ifndef NOMINMAX // 外部没定义时才定义
#define NOMINMAX // 避免 Windows.h 的 min/max 宏
#endif // 结束 NOMINMAX 守卫
#define WIN32_LEAN_AND_MEAN // 少拉进无关的 Windows 头
#include <Windows.h> // HWND 和 BOOL
#include <dwmapi.h> // 标题栏颜色属性

void enableDarkTitleBar(quintptr windowHandle) { // 主窗口和设置窗口调用
    if (!windowHandle) { // 还没有原生句柄
        return; // 不做任何事
    } // 结束空句柄
    const HWND hwnd = reinterpret_cast<HWND>(windowHandle); // Qt 句柄转 Win32
    const BOOL enabled = TRUE; // 打开深色
    DwmSetWindowAttribute(hwnd, 20, &enabled, sizeof(enabled)); // Win11 的 DWMWA_USE_IMMERSIVE_DARK_MODE
    DwmSetWindowAttribute(hwnd, 19, &enabled, sizeof(enabled)); // 旧系统上的同一属性编号
} // 结束深色标题栏
