// SPDX-License-Identifier: Apache-2.0
#pragma once // 头文件只包含一次

#include <QRect> // 壁纸要盖住的物理矩形
#include <QString> // 失败原因和层级说明

struct HWND__; // 不在头文件里包含 Windows.h
using HWND = HWND__*; // 句柄用不透明指针

// 壁纸窗口怎么挂到桌面。0x052C 之后，Windows 10 和早期 Windows 11
// 会给出和图标层并列的空 WorkerW。Windows 11 24H2 上那个空层可能不留着，
// 那时改用微软文档里的 Progman 分层子窗口。
enum class DesktopHostMode { // 挂接模式
    None = 0, // 还没找到宿主
    WorkerWBehindIcons, // 图标层后面的 WorkerW，Win10 走这条
    ProgmanLayeredChild // Progman 下的分层子窗口，只给 24H2 后备
}; // 结束挂接模式

struct DesktopHost { // 一次定位的结果
    HWND progman = nullptr; // 桌面 Progman
    HWND iconHost = nullptr; // 放着 DefView 的窗口，不要把它提到 HWND_TOP
    HWND wallpaperHost = nullptr; // 壁纸要挂上去的窗口
    HWND defView = nullptr; // 桌面图标视图
    DesktopHostMode mode = DesktopHostMode::None; // 实际采用的模式
    QString detail; // 定位过程的说明，创建时已翻译
}; // 结束宿主

class WorkerWHost { // 查找桌面层并把壁纸窗口嵌进去
public: // 全部是无状态的静态操作
    static DesktopHost locate(QString* error); // 发 0x052C 并找出宿主
    static bool attach(HWND child, const DesktopHost& host, const QRect& physicalRect, QString* error); // 把子窗口放进宿主
    static bool hostStillValid(const DesktopHost& host); // 资源管理器重启后宿主是否还在
    static bool iconsAboveWallpaper(const DesktopHost& host); // 图标层是否仍在壁纸之上
    static QString describe(const DesktopHost& host); // 给诊断区的一行说明
    static QString hierarchySummary(); // 日志用的桌面层级，不翻译整段格式
}; // 结束宿主操作
