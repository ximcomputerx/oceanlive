// SPDX-License-Identifier: Apache-2.0
// 临时桌面层级探针，打印 Progman、WorkerW 和 SHELLDLL_DefView。
#ifndef NOMINMAX // 外面没定义时才定义
#define NOMINMAX // 避免 Windows 头改写 min/max
#endif // 结束 NOMINMAX 守卫
#define WIN32_LEAN_AND_MEAN // 少包含无关声明
#include <Windows.h> // 枚举窗口并发送 0x052C

#include <cstdio> // 宽字符输出
#include <string> // 保存窗口类名

static std::wstring classOf(HWND hwnd) { // 读取窗口类名
    wchar_t buf[256] = {}; // 类名缓冲
    GetClassNameW(hwnd, buf, 255); // 留出结尾
    return buf; // 空句柄得到空串
} // 结束读取类名

static void printWindow(const wchar_t* tag, HWND hwnd, int depth) { // 按缩进打印一个窗口
    if (!hwnd) { // 没有句柄
        std::wprintf(L"%*s%s: <null>\n", depth * 2, L"", tag); // 只打印标签
        return; // 不再读样式
    } // 结束空句柄
    RECT rc{}; // 屏幕坐标矩形
    GetWindowRect(hwnd, &rc); // 读取外框
    LONG style = GetWindowLongW(hwnd, GWL_STYLE); // 基本样式
    LONG ex = GetWindowLongW(hwnd, GWL_EXSTYLE); // 扩展样式
    std::wprintf( // 输出句柄、类名、父窗口和矩形
        L"%*s%s hwnd=%p class=%s parent=%p visible=%d style=0x%08lX ex=0x%08lX " // 前半段格式
        L"rect=(%ld,%ld)-(%ld,%ld) %ldx%ld\n", // 矩形和宽高
        depth * 2, L"", tag, hwnd, classOf(hwnd).c_str(), GetParent(hwnd), // 缩进、标签、句柄和父窗口
        IsWindowVisible(hwnd) ? 1 : 0, style, ex, rc.left, rc.top, rc.right, rc.bottom, // 可见性、样式和四边
        rc.right - rc.left, rc.bottom - rc.top); // 宽度和高度
} // 结束打印窗口

static void dumpChildren(HWND parent, int depth, int maxDepth) { // 递归打印子窗口
    if (!parent || depth > maxDepth) { // 没有父窗口或超过深度
        return; // 这一支到此为止
    } // 结束深度限制
    HWND child = nullptr; // 枚举起点为空
    int index = 0; // 当前层的序号
    while ((child = FindWindowExW(parent, child, nullptr, nullptr)) != nullptr) { // 下一个直接子窗口
        wchar_t label[64]; // 序号标签
        swprintf_s(label, L"child[%d]", index++); // 写上序号并前进
        printWindow(label, child, depth); // 打印这个子窗口
        dumpChildren(child, depth + 1, maxDepth); // 再往下走一层
        if (index > 40) { // 防止异常树刷屏
            break; // 这一层最多 40 个
        } // 结束数量限制
    } // 结束子窗口循环
} // 结束递归

static BOOL CALLBACK enumTop(HWND hwnd, LPARAM) { // 只展开关心的顶层窗口
    const std::wstring cls = classOf(hwnd); // 顶层类名
    if (cls == L"Progman" || cls == L"WorkerW" || cls == L"Shell_TrayWnd") { // 桌面、壁纸层或任务栏
        printWindow(L"top", hwnd, 0); // 顶层不缩进
        dumpChildren(hwnd, 1, 4); // 最多再打印 4 层
    } // 结束关心的类
    return TRUE; // 继续枚举
} // 结束顶层回调

int wmain() { // 探针入口，不创建 Qt 界面
    std::wprintf(L"=== before 0x052C ===\n"); // 发送拆桌面消息之前
    EnumWindows(enumTop, 0); // 先记录当前层级

    HWND progman = FindWindowW(L"Progman", nullptr); // 找到桌面 Progman
    DWORD_PTR result = 0; // 超时调用的应答
    LRESULT lr = SendMessageTimeoutW( // 未公开消息，资源管理器卡住时最多等一秒
        progman, 0x052C, 0, 0, SMTO_NORMAL | SMTO_ABORTIFHUNG, 1000, &result); // 参数用 0,0
    std::wprintf(L"\nSendMessageTimeout 0x052C -> lr=%lld ack=%llu err=%lu\n", // 打印返回值、应答和错误码
                 static_cast<long long>(lr), static_cast<unsigned long long>(result), GetLastError()); // 三个数值

    std::wprintf(L"\n=== after 0x052C ===\n"); // 发送之后再记一次
    EnumWindows(enumTop, 0); // 看是否多出 WorkerW

    HWND workerBehind = nullptr; // 图标层后面的 WorkerW
    EnumWindows( // 寻找 DefView 后面的兄弟窗口
        [](HWND hwnd, LPARAM lp) -> BOOL { // 检查每个顶层窗口
            HWND def = FindWindowExW(hwnd, nullptr, L"SHELLDLL_DefView", nullptr); // 直接子级里的图标视图
            if (!def) { // 这个窗口不放图标
                return TRUE; // 继续
            } // 结束没有图标层
            HWND* out = reinterpret_cast<HWND*>(lp); // 结果写回调用方
            HWND sibling = FindWindowExW(nullptr, hwnd, L"WorkerW", nullptr); // hwnd 后面的下一个 WorkerW
            std::wprintf(L"DefView owner=%p class=%s siblingWorker=%p\n", hwnd, // 打印图标宿主
                         classOf(hwnd).c_str(), sibling); // 类名和兄弟句柄
            if (sibling) { // 找到了后面的 WorkerW
                *out = sibling; // 只记录，不改它的 Z 序
            } // 结束记下兄弟
            return TRUE; // 继续看还有没有别的
        }, // 结束查找回调
        reinterpret_cast<LPARAM>(&workerBehind)); // 传入输出指针
    std::wprintf(L"chosen sibling WorkerW=%p\n", workerBehind); // 最后选中的兄弟
    return 0; // 探针只打印
} // 结束探针
