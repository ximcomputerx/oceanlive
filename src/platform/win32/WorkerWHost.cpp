// SPDX-License-Identifier: Apache-2.0
#include "platform/win32/WorkerWHost.h" // 桌面宿主声明

#include "utils/Logger.h" // 记录定位和挂接

#include <QCoreApplication> // 用户可见错误用 translate
#include <QStringList> // 定位笔记和层级行

#ifndef NOMINMAX // 外部没定义时才定义
#define NOMINMAX // 避免 Windows.h 的 min/max 宏
#endif // 结束 NOMINMAX 守卫
#define WIN32_LEAN_AND_MEAN // 少包含无关头
#include <Windows.h> // 窗口查找和 SetParent
#include <objbase.h> // CoCreateInstance
#include <shobjidl.h> // 从任务栏去掉壁纸窗口

namespace { // 查找 DefView 和调整样式

QString hwndText(HWND hwnd) { // 日志里的句柄十六进制
    return QStringLiteral("0x%1").arg(reinterpret_cast<quintptr>(hwnd), 0, 16); // 空句柄也打印 0x0
} // 结束句柄文本

std::wstring classOf(HWND hwnd) { // 窗口类名
    wchar_t buffer[256] = {}; // 类名缓冲
    if (!hwnd) { // 空句柄
        return {}; // 空字符串
    } // 结束空句柄
    GetClassNameW(hwnd, buffer, 255); // 留一个结尾
    return buffer; // 宽字符串
} // 结束类名

bool isLargeWindow(HWND hwnd) { // 丢掉很小的 WorkerW
    RECT rect{}; // 窗口矩形
    if (!hwnd || !GetWindowRect(hwnd, &rect)) { // 没有或读失败
        return false; // 不算大窗口
    } // 结束读失败
    return (rect.right - rect.left) > 400 && (rect.bottom - rect.top) > 300; // 宽过 400 且高过 300
} // 结束尺寸过滤

struct SearchResult { // 枚举时记下图标层和它后面的空 WorkerW
    HWND defOwner = nullptr; // 含 DefView 的父窗口
    HWND defView = nullptr; // 桌面图标视图
    HWND sibling = nullptr; // 图标层后面的大块空 WorkerW
}; // 结束搜索结果

BOOL CALLBACK findDefViewOwner(HWND hwnd, LPARAM param) { // 找放图标的窗口
    auto* result = reinterpret_cast<SearchResult*>(param); // 结果写这里
    HWND defView = FindWindowExW(hwnd, nullptr, L"SHELLDLL_DefView", nullptr); // 直接子级的图标视图
    if (!defView) { // 这个窗口没有图标层
        return TRUE; // 继续
    } // 结束没有 DefView
    result->defOwner = hwnd; // 图标宿主
    result->defView = defView; // 图标视图
    HWND sibling = FindWindowExW(nullptr, hwnd, L"WorkerW", nullptr); // hwnd 后面的下一个 WorkerW
    if (sibling && sibling != hwnd && isLargeWindow(sibling) && // 是另一块大窗口
        !FindWindowExW(sibling, nullptr, L"SHELLDLL_DefView", nullptr)) { // 而且自己不含图标
        result->sibling = sibling; // 这就是壁纸层
        return FALSE; // 找到后停止
    } // 结束找到兄弟
    return TRUE; // 继续找更合适的
} // 结束查找图标宿主

void sendRaiseDesktop(HWND progman, QStringList* notes) { // 让资源管理器拆出壁纸层
    // 0x052C 是未公开的 Progman 消息。Windows 10 上它把图标放到一块 WorkerW，
    // 并在后面再创建一块 WorkerW。资源管理器卡住时不能把我们冻住，所以调用有超时。
    DWORD_PTR ack = 0; // 超时调用的应答
    const UINT kRaiseDesktop = 0x052C; // 拆桌面的消息号
    LRESULT result = SendMessageTimeoutW(progman, kRaiseDesktop, 0, 0, SMTO_NORMAL | SMTO_ABORTIFHUNG, 1000, &ack); // 先试 (0,0)，最多 1 秒
    notes->push_back(QStringLiteral("0x052C(0,0) lr=%1 ack=%2 err=%3") // 日志格式不翻译
                         .arg(result) // 返回值，0 表示超时或失败
                         .arg(static_cast<qulonglong>(ack)) // 应答
                         .arg(GetLastError())); // 错误码
    if (result != 0) { // 第一种参数已被接受
        return; // 不再试别的
    } // 结束第一种成功
    ack = 0; // 下一次重新计应答
    result = SendMessageTimeoutW(progman, kRaiseDesktop, 0xD, 0, SMTO_NORMAL | SMTO_ABORTIFHUNG, 1000, &ack); // 再试 (0xD,0)
    notes->push_back(QStringLiteral("0x052C(0xD,0) lr=%1 ack=%2").arg(result).arg(static_cast<qulonglong>(ack))); // 记下这次
    if (result != 0) { // 第二种成功
        return; // 结束
    } // 结束第二种
    ack = 0; // 第三次
    result = SendMessageTimeoutW(progman, kRaiseDesktop, 0xD, 1, SMTO_NORMAL | SMTO_ABORTIFHUNG, 1000, &ack); // 最后试 (0xD,1)
    notes->push_back(QStringLiteral("0x052C(0xD,1) lr=%1 ack=%2").arg(result).arg(static_cast<qulonglong>(ack))); // 记下这次
} // 结束发送拆桌面

HWND findEmptyProgmanWorker(HWND progman) { // Progman 下面不含图标的大 WorkerW
    HWND child = nullptr; // 枚举子窗口
    while ((child = FindWindowExW(progman, child, L"WorkerW", nullptr)) != nullptr) { // 下一个 WorkerW
        if (!FindWindowExW(child, nullptr, L"SHELLDLL_DefView", nullptr) && isLargeWindow(child)) { // 没有图标且够大
            return child; // 用这块
        } // 结束命中
    } // 结束子级循环
    return nullptr; // Progman 下没有合适的
} // 结束查找空 WorkerW

void enforceChildStyles(HWND child, bool layered) { // 嵌进去之后整理样式
    LONG_PTR style = GetWindowLongPtrW(child, GWL_STYLE); // 当前样式
    style &= ~(WS_POPUP | WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU | WS_OVERLAPPED); // 去掉顶层装饰
    style |= WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN; // 变成可见子窗口
    SetWindowLongPtrW(child, GWL_STYLE, style); // 写回

    LONG_PTR ex = GetWindowLongPtrW(child, GWL_EXSTYLE); // 扩展样式
    ex |= WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW; // 不激活，也不单独进任务栏
    ex &= ~(WS_EX_APPWINDOW | WS_EX_TOPMOST | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE); // 去掉顶层和置顶
    if (layered) { // 只有 Progman 后备路径
        // Windows 11 24H2 要在 DefView 和系统壁纸 WorkerW 之间放一个分层子窗口。
        // 不透明 alpha 让画面看得见。Win10 的 WorkerW 兄弟路径不用分层，
        // 因为那里的分层 OpenGL 窗口经常是黑的。
        ex |= WS_EX_LAYERED; // 打开分层
    } else { // Win10 的 WorkerW 路径
        ex &= ~WS_EX_LAYERED; // 明确去掉分层
    } // 结束分层选择
    SetWindowLongPtrW(child, GWL_EXSTYLE, ex); // 写回扩展样式
    if (layered) { // 分层窗口要给 alpha
        SetLayeredWindowAttributes(child, 0, 255, LWA_ALPHA); // 完全不透明
    } // 结束设置透明度
    SetWindowPos(child, nullptr, 0, 0, 0, 0, // 不移动不改大小
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED); // 让新样式生效，不改 z 序
} // 结束整理样式

}  // 结束匿名命名空间

QString WorkerWHost::hierarchySummary() { // 给日志的桌面层级
    QStringList lines; // 每一层一行，最后用竖线拼起来
    HWND progman = FindWindowW(L"Progman", nullptr); // 桌面窗口
    lines << QStringLiteral("Progman %1").arg(hwndText(progman)); // 先记 Progman
    if (progman) { // 找到了才列子窗口
        HWND child = nullptr; // 枚举起点
        while ((child = FindWindowExW(progman, child, nullptr, nullptr)) != nullptr) { // 任意类名的子窗口
            lines << QStringLiteral("  Progman child %1 class=%2") // 缩进表示子级
                         .arg(hwndText(child), QString::fromWCharArray(classOf(child).c_str())); // 句柄和类名
        } // 结束子窗口
    } // 结束 Progman 存在
    EnumWindows( // 再扫顶层的 WorkerW 和 Progman
        [](HWND hwnd, LPARAM param) -> BOOL { // 每个顶层窗口
            const std::wstring cls = classOf(hwnd); // 类名
            if (cls != L"WorkerW" && cls != L"Progman") { // 其他类不管
                return TRUE; // 继续
            } // 结束类过滤
            if (cls == L"WorkerW" && !isLargeWindow(hwnd)) { // 很小的 WorkerW
                return TRUE; // 跳过
            } // 结束小窗口
            RECT rect{}; // 位置
            GetWindowRect(hwnd, &rect); // 读矩形
            auto* lines = reinterpret_cast<QStringList*>(param); // 日志行
            lines->push_back(QStringLiteral("top %1 class=%2 parent=%3 visible=%4 rect=%5,%6 %7x%8 defview=%9") // 固定格式
                                 .arg(hwndText(hwnd), QString::fromWCharArray(cls.c_str()), hwndText(GetParent(hwnd))) // 句柄、类、父
                                 .arg(IsWindowVisible(hwnd) ? 1 : 0) // 是否可见
                                 .arg(rect.left) // 左
                                 .arg(rect.top) // 上
                                 .arg(rect.right - rect.left) // 宽
                                 .arg(rect.bottom - rect.top) // 高
                                 .arg(FindWindowExW(hwnd, nullptr, L"SHELLDLL_DefView", nullptr) ? 1 : 0)); // 是否含图标层
            return TRUE; // 继续列完
        }, // 结束回调
        reinterpret_cast<LPARAM>(&lines)); // 传入行列表
    return lines.join(QStringLiteral(" | ")); // 合成一条日志
} // 结束层级摘要

DesktopHost WorkerWHost::locate(QString* error) { // 找出壁纸该挂到哪里
    DesktopHost host; // 默认还没有模式
    QStringList notes; // 过程说明，有的句子可翻译
    host.progman = FindWindowW(L"Progman", nullptr); // 桌面
    if (!host.progman) { // 资源管理器还没起来
        if (error) { // 调用方要错误
            *error = QCoreApplication::translate("WorkerWHost", "未找到 Progman。资源管理器可能尚未就绪。"); // 可翻译
        } // 结束填写
        return host; // 模式仍是 None
    } // 结束没有 Progman
    sendRaiseDesktop(host.progman, &notes); // 尝试拆出壁纸 WorkerW

    SearchResult search; // 图标层和兄弟层
    EnumWindows(findDefViewOwner, reinterpret_cast<LPARAM>(&search)); // 找含 DefView 的顶层窗口
    host.defView = search.defView ? search.defView : FindWindowExW(host.progman, nullptr, L"SHELLDLL_DefView", nullptr); // 没有就看 Progman 自己
    host.iconHost = search.defOwner; // 图标宿主，后面不能把它设成 HWND_TOP

    if (search.sibling && IsWindow(search.sibling)) { // 经典的图标后面那块 WorkerW
        host.wallpaperHost = search.sibling; // 壁纸挂这里
        host.mode = DesktopHostMode::WorkerWBehindIcons; // Win10 路径，不用分层
        notes << QCoreApplication::translate("WorkerWHost", "使用图标层后面的 WorkerW"); // 说明可翻译
    } else { // 没有直接的兄弟层
        HWND empty = findEmptyProgmanWorker(host.progman); // 先看 Progman 的子级
        if (!empty) { // 子级没有
            EnumWindows( // 再在顶层找一块大的空 WorkerW
                [](HWND hwnd, LPARAM param) -> BOOL { // 每个顶层窗口
                    if (classOf(hwnd) != L"WorkerW" || !isLargeWindow(hwnd)) { // 不是大 WorkerW
                        return TRUE; // 继续
                    } // 结束尺寸过滤
                    if (FindWindowExW(hwnd, nullptr, L"SHELLDLL_DefView", nullptr)) { // 这块上有图标
                        return TRUE; // 不能当壁纸层
                    } // 结束含图标
                    *reinterpret_cast<HWND*>(param) = hwnd; // 记下
                    return FALSE; // 停止
                }, // 结束回调
                reinterpret_cast<LPARAM>(&empty)); // 写入 empty
        } // 结束顶层搜索
        if (empty) { // 找到空的大 WorkerW
            host.wallpaperHost = empty; // 用它
            host.mode = DesktopHostMode::WorkerWBehindIcons; // 仍是非分层路径
            notes << QCoreApplication::translate("WorkerWHost", "使用不含图标的大型 WorkerW"); // 说明可翻译
        } else if (host.defView) { // 有图标层但没有独立 WorkerW
            host.wallpaperHost = host.progman; // 后备挂到 Progman
            host.mode = DesktopHostMode::ProgmanLayeredChild; // 这条才用分层
            notes << QCoreApplication::translate("WorkerWHost", "未找到独立 WorkerW，回退到 Progman 子窗口"); // 说明可翻译
        } // 结束后备
    } // 结束选择宿主

    host.detail = notes.join(QStringLiteral("; ")); // 笔记拼进诊断
    Logger::info(QStringLiteral("WorkerWHost"), host.detail + QStringLiteral(" | ") + hierarchySummary()); // 和层级一起记
    if (host.mode == DesktopHostMode::None || !host.wallpaperHost) { // 两种都没找到
        if (error) { // 调用方要错误
            *error = QCoreApplication::translate("WorkerWHost", "未能定位桌面壁纸宿主窗口。动态壁纸没有启动。"); // 可翻译
        } // 结束填写
        Logger::error(QStringLiteral("WorkerWHost"), error ? *error : QStringLiteral("locate failed")); // 没有 error 指针时用英文短句
    } // 结束失败
    return host; // 可能是 None
} // 结束定位

bool WorkerWHost::attach(HWND child, const DesktopHost& host, const QRect& physicalRect, QString* error) { // 把壁纸嵌进宿主
    if (!child || !host.wallpaperHost || host.mode == DesktopHostMode::None) { // 缺窗口或没模式
        if (error) { // 要错误文本
            *error = QCoreApplication::translate("WorkerWHost", "壁纸窗口或桌面宿主无效。"); // 可翻译
        } // 结束填写
        return false; // 不挂
    } // 结束参数无效
    if (!IsWindow(child) || !IsWindow(host.wallpaperHost)) { // 句柄已死
        if (error) { // 要错误文本
            *error = QCoreApplication::translate("WorkerWHost", "窗口句柄已失效。"); // 可翻译
        } // 结束填写
        return false; // 不挂
    } // 结束句柄失效

    const HWND parent = host.mode == DesktopHostMode::ProgmanLayeredChild ? host.progman : host.wallpaperHost; // 分层挂 Progman，否则挂 WorkerW
    SetLastError(0); // 区分 SetParent 返回空是失败还是本来就没有父
    const HWND previous = SetParent(child, parent); // 改父窗口
    if (!previous && GetLastError() != 0) { // 空返回且有错误码
        if (error) { // 要错误文本
            *error = QCoreApplication::translate("WorkerWHost", "SetParent 失败，错误码 %1。").arg(GetLastError()); // 带上错误码
        } // 结束填写
        Logger::error(QStringLiteral("WorkerWHost"), error ? *error : QString()); // 记失败
        return false; // 挂接失败
    } // 结束 SetParent 失败

    const bool layered = host.mode == DesktopHostMode::ProgmanLayeredChild; // 只有这条开分层
    enforceChildStyles(child, layered); // 整理样式

    RECT rect{physicalRect.left(), physicalRect.top(), physicalRect.left() + physicalRect.width(), // 物理像素左右
              physicalRect.top() + physicalRect.height()}; // 物理像素下边
    MapWindowPoints(nullptr, parent, reinterpret_cast<POINT*>(&rect), 2); // 屏幕坐标改成父窗口客户区坐标
    const int width = rect.right - rect.left; // 映射后的宽
    const int height = rect.bottom - rect.top; // 映射后的高
    if (width < 32 || height < 32) { // 小到不像一块显示器
        if (error) { // 要错误文本
            *error = QCoreApplication::translate("WorkerWHost", "显示器矩形映射到宿主窗口后尺寸无效。"); // 可翻译
        } // 结束填写
        return false; // 不摆放
    } // 结束尺寸无效

    HWND insertAfter = HWND_BOTTOM; // 默认放到父窗口最底
    if (layered && host.defView) { // 分层路径要留在图标下面
        insertAfter = host.defView; // 插在 DefView 之后，也就是更靠里
    } // 结束 z 序选择
    SetWindowPos(child, insertAfter, rect.left, rect.top, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW); // 摆到显示器上且不激活
    ShowWindow(child, SW_SHOWNOACTIVATE); // 再显示一次，仍不激活

    if (!layered && host.iconHost && host.wallpaperHost && host.iconHost != host.wallpaperHost) { // Win10 两条 WorkerW
        // 让图标 WorkerW 留在壁纸 WorkerW 上面，但不要抬过任务栏。故意不用 HWND_TOP。
        SetWindowPos(host.wallpaperHost, host.iconHost, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE); // 壁纸放到图标宿主后面
    } else if (layered && host.defView) { // 分层路径再对一次图标层
        SetWindowPos(child, host.defView, rect.left, rect.top, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW); // 保持在 DefView 后面
    } // 结束 z 序

    ITaskbarList* taskbar = nullptr; // 任务栏接口
    if (SUCCEEDED(CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&taskbar))) && taskbar) { // 创建成功
        if (SUCCEEDED(taskbar->HrInit())) { // 初始化成功
            taskbar->DeleteTab(child); // 壁纸不要出现在任务栏
        } // 结束初始化
        taskbar->Release(); // 释放接口
    } // 结束任务栏

    Logger::info(QStringLiteral("WorkerWHost"), // 类别名
                 QStringLiteral("已挂接 child=%1 parent=%2 mode=%3 rect=%4,%5 %6x%7") // 日志格式不翻译
                     .arg(hwndText(child), hwndText(GetParent(child))) // 子窗口和实际父窗口
                     .arg(static_cast<int>(host.mode)) // 模式序号
                     .arg(rect.left) // 左
                     .arg(rect.top) // 上
                     .arg(width) // 宽
                     .arg(height)); // 高
    return GetParent(child) == parent; // 父窗口确实是我们设的
} // 结束挂接

bool WorkerWHost::hostStillValid(const DesktopHost& host) { // 资源管理器重启后检查
    if (host.mode == DesktopHostMode::None || !host.wallpaperHost || !IsWindow(host.wallpaperHost)) { // 没有宿主或句柄死了
        return false; // 需要重新定位
    } // 结束宿主无效
    if (host.progman && !IsWindow(host.progman)) { // Progman 曾经有，现在没了
        return false; // 桌面被重建
    } // 结束 Progman 失效
    HWND defView = nullptr; // 重新找图标层
    EnumWindows( // 顶层窗口
        [](HWND hwnd, LPARAM param) -> BOOL { // 每个窗口
            HWND found = FindWindowExW(hwnd, nullptr, L"SHELLDLL_DefView", nullptr); // 直接子级
            if (!found) { // 没有
                return TRUE; // 继续
            } // 结束没有
            *reinterpret_cast<HWND*>(param) = found; // 记下
            return FALSE; // 停止
        }, // 结束回调
        reinterpret_cast<LPARAM>(&defView)); // 写入
    return defView != nullptr; // 图标层还在才算桌面活着
} // 结束有效性

bool WorkerWHost::iconsAboveWallpaper(const DesktopHost& host) { // 图标是否仍盖在壁纸上
    if (!host.iconHost || !host.wallpaperHost || host.iconHost == host.wallpaperHost) { // 没有分开的两层
        return host.defView != nullptr; // 有图标视图就当作关系正常
    } // 结束同一窗口
    int iconZ = -1; // 图标在 z 序里的序号，越小越靠前
    int wallZ = -1; // 壁纸序号
    int index = 0; // 从最前开始数
    for (HWND hwnd = GetTopWindow(nullptr); hwnd; hwnd = GetWindow(hwnd, GW_HWNDNEXT), ++index) { // 从前到后
        if (hwnd == host.iconHost) { // 遇到图标宿主
            iconZ = index; // 记下位置
        } // 结束图标
        if (hwnd == host.wallpaperHost) { // 遇到壁纸宿主
            wallZ = index; // 记下位置
        } // 结束壁纸
        if (iconZ >= 0 && wallZ >= 0) { // 两个都找到了
            break; // 不用再走
        } // 结束提前退出
    } // 结束 z 序扫描
    if (iconZ < 0 || wallZ < 0) { // 有一个不在顶层链上
        return false; // 关系不清楚，当成不正常
    } // 结束缺失
    return iconZ < wallZ; // 序号更小表示更靠前，图标应在壁纸前面
} // 结束图标层级

QString WorkerWHost::describe(const DesktopHost& host) { // 诊断区的一行
    QString mode = QCoreApplication::translate("WorkerWHost", "无"); // 默认没有模式
    if (host.mode == DesktopHostMode::WorkerWBehindIcons) { // WorkerW 路径
        mode = QCoreApplication::translate("WorkerWHost", "WorkerW 背景层"); // 可翻译
    } else if (host.mode == DesktopHostMode::ProgmanLayeredChild) { // 分层后备
        mode = QCoreApplication::translate("WorkerWHost", "Progman 分层子窗口"); // 可翻译
    } // 结束模式名
    return QCoreApplication::translate("WorkerWHost", "模式=%1 Progman=%2 图标宿主=%3 壁纸宿主=%4 DefView=%5 %6") // 整句可翻译
        .arg(mode, hwndText(host.progman), hwndText(host.iconHost), hwndText(host.wallpaperHost), hwndText(host.defView), // 五个字段
             host.detail); // 定位笔记
} // 结束描述
