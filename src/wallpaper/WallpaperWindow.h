// SPDX-License-Identifier: Apache-2.0
#pragma once // 头文件只包含一次

#include "platform/win32/WorkerWHost.h" // 挂接目标和物理矩形
#include "render/OceanView.h" // 窗口里只有一个海面视图

#include <QWidget> // 无边框原生窗口

class WallpaperWindow : public QWidget { // 嵌在桌面图标层下面的壁纸窗口
    Q_OBJECT // 启用元对象

public: // 嵌入和取出视图
    explicit WallpaperWindow(QWidget* parent = nullptr); // 通常没有父对象

    OceanView* view() const; // 这个窗口的视图，不在这里创建播放器
    bool embed(const DesktopHost& host, const QRect& physicalRect, QString* error); // 挂到宿主并记住矩形
    bool reembed(const DesktopHost& host, QString* error); // 用上次的矩形再挂一次
    bool needsReattach(const DesktopHost& host) const; // 父窗口已经不是预期宿主
    void rememberHost(const DesktopHost& host, const QRect& physicalRect); // 保存宿主和物理像素矩形

protected: // 原生消息
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override; // 鼠标点进来时不激活

private: // 挂接状态
    OceanView* m_view = nullptr; // 铺满客户区
    DesktopHost m_host; // 上次使用的宿主
    QRect m_physical; // 显示器物理矩形，重新挂接还用它
    bool m_embedded = false; // 是否已经嵌进桌面
}; // 结束壁纸窗口
