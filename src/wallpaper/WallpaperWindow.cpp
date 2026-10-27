// SPDX-License-Identifier: Apache-2.0
#include "wallpaper/WallpaperWindow.h" // 壁纸窗口声明

#include "utils/Logger.h" // 挂接失败时记日志

#include <QVBoxLayout> // 让视图铺满客户区

#include <algorithm> // 保留原有标准算法头

#ifndef NOMINMAX // 外面没定义时才定义
#define NOMINMAX // 避免 Windows 头里的 min/max 宏
#endif // 结束 NOMINMAX 守卫
#define WIN32_LEAN_AND_MEAN // 少包含无关声明
#include <Windows.h> // 父窗口判断和鼠标激活消息

WallpaperWindow::WallpaperWindow(QWidget* parent) : QWidget(parent) { // 先作为普通窗口，随后再嵌进桌面
    setWindowTitle(QStringLiteral("OceanLiveWallpaper")); // 标题保持 OceanLiveWallpaper，用来把自己排除出全屏检测
    setWindowFlags(Qt::FramelessWindowHint | Qt::Tool); // 无边框工具窗，不进任务栏，也不加分层扩展样式
    setAttribute(Qt::WA_NativeWindow); // 创建时就要原生句柄
    setAttribute(Qt::WA_ShowWithoutActivating); // 显示时不激活
    setAttribute(Qt::WA_TransparentForMouseEvents, true); // 鼠标留给桌面图标
    setFocusPolicy(Qt::NoFocus); // 不拿键盘焦点
    setAttribute(Qt::WA_QuitOnClose, false); // 关掉壁纸窗口不退出进程

    m_view = new OceanView(this); // 每个壁纸窗口一个视图
    auto* layout = new QVBoxLayout(this); // 布局挂在本窗口上
    layout->setContentsMargins(0, 0, 0, 0); // 四边不留白
    layout->setSpacing(0); // 控件之间不留缝
    layout->addWidget(m_view); // 视图填满
    winId(); // 立刻创建 HWND，后面才能嵌进宿主
} // 结束构造

OceanView* WallpaperWindow::view() const { // 管理器用它绑参数和播放器
    return m_view; // 不在这里另建播放器
} // 结束取视图

void WallpaperWindow::rememberHost(const DesktopHost& host, const QRect& physicalRect) { // 记住这次挂接
    m_host = host; // 宿主和挂接模式
    m_physical = physicalRect; // 重新嵌入时还用这块物理矩形
} // 结束记录

bool WallpaperWindow::embed(const DesktopHost& host, const QRect& physicalRect, QString* error) { // 嵌进桌面层
    rememberHost(host, physicalRect); // 先保存，失败也留下矩形
    const HWND hwnd = reinterpret_cast<HWND>(winId()); // Qt 窗口句柄
    m_embedded = WorkerWHost::attach(hwnd, host, physicalRect, error); // 按模式设父窗口，不把窗口抬到最前
    if (m_embedded) { // 挂上才开始画
        m_view->surface()->setUnderDesktop(true); // 标明这是桌面下层
        m_view->surface()->setPaused(false); // 嵌入成功先恢复动画，总开关随后再推
    } else { // 没挂上
        Logger::error(QStringLiteral("WallpaperWindow"), error ? *error : QStringLiteral("挂接失败")); // 日志保持中文原文
    } // 结束失败日志
    return m_embedded; // 由管理器决定是否放弃窗口
} // 结束嵌入

bool WallpaperWindow::reembed(const DesktopHost& host, QString* error) { // 宿主变了，矩形仍用上次的
    if (m_physical.width() < 32 || m_physical.height() < 32) { // 没有可用的显示器矩形
        if (error) { // 调用方需要原因
            *error = tr("缺少有效的显示器矩形，无法重新挂接。"); // 可翻译，矩形无效
        } // 结束填写错误
        return false; // 交给管理器做完整重排
    } // 结束矩形过小
    return embed(host, m_physical, error); // 用记住的物理矩形再挂一次
} // 结束重新嵌入

bool WallpaperWindow::needsReattach(const DesktopHost& host) const { // 父窗口是否还对
    if (!m_embedded || !host.wallpaperHost) { // 还没挂上，或宿主已经空了
        return true; // 需要重新处理
    } // 结束明显失效
    const HWND hwnd = reinterpret_cast<HWND>(winId()); // 自己的句柄
    if (!IsWindow(hwnd)) { // 句柄已失效
        return true; // 窗口不在了
    } // 结束句柄检查
    const HWND expected = host.mode == DesktopHostMode::ProgmanLayeredChild ? host.progman : host.wallpaperHost; // 分层模式父窗口是 Progman，否则是壁纸宿主
    return GetParent(hwnd) != expected; // 父窗口变了就要重挂
} // 结束重挂判断

bool WallpaperWindow::nativeEvent(const QByteArray& eventType, void* message, qintptr* result) { // 再挡一次激活
    if (eventType == "windows_generic_MSG" || eventType == "windows_dispatcher_MSG") { // Qt 转发的 Win32 消息
        auto* msg = static_cast<MSG*>(message); // 原始消息
        if (msg->message == WM_MOUSEACTIVATE) { // 鼠标按下时系统问要不要激活
            *result = MA_NOACTIVATE; // 不激活，点击继续给桌面
            return true; // 这条消息已处理
        } // 结束鼠标激活
    } // 结束 Win32 消息
    return QWidget::nativeEvent(eventType, message, result); // 其他消息交给 Qt
} // 结束原生事件
