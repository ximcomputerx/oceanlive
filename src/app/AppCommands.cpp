// SPDX-License-Identifier: Apache-2.0
#include "app/AppCommands.h" // 预览导出和桌面自测声明

#include "platform/win32/WorkerWHost.h" // 宿主描述和图标层判断
#include "render/OceanView.h" // 离屏预览用的海面视图
#include "ui/MainWindow.h" // 自测期间隐藏主窗口
#include "utils/Logger.h" // 自测日志
#include "wallpaper/WallpaperManager.h" // 启动和停止真正的壁纸

#include <QApplication> // 泵事件
#include <QColor> // 采样像素
#include <QDir> // 创建输出目录
#include <QElapsedTimer> // 控制等待时间
#include <QFile> // 复制旧预览和写报告
#include <QFileInfo> // 报告所在目录
#include <QGuiApplication> // 主屏几何
#include <QImage> // 帧缓冲和屏幕采样
#include <QScreen> // 全屏探测窗口的屏幕
#include <QTextStream> // 自测报告用 UTF-8
#include <QThread> // 等待时短暂睡眠
#include <QVBoxLayout> // 预览宿主里的布局
#include <QWidget> // 预览和全屏探测窗口

#include <vector> // 屏幕采样的像素缓冲

#ifndef NOMINMAX // 外面没定义时才定义
#define NOMINMAX // 避免 Windows 头里的 min/max 宏
#endif // 结束 NOMINMAX 守卫
#define WIN32_LEAN_AND_MEAN // 少包含无关声明
#include <Windows.h> // 桌面命中、样式和右键菜单

namespace { // 采样和输入辅助只在本文件使用

QString classNameOf(HWND hwnd) { // 命中窗口的类名
    wchar_t buffer[256] = {}; // 类名缓冲
    GetClassNameW(hwnd, buffer, 255); // 留出结尾
    return QString::fromWCharArray(buffer); // 转成 QString
} // 结束类名

void pump(int milliseconds) { // 在限定时间里处理事件
    QElapsedTimer timer; // 累计已经等待的时间
    timer.start(); // 开始计时
    while (timer.elapsed() < milliseconds) { // 还没到时间
        QApplication::processEvents(QEventLoop::AllEvents, 30); // 每轮最多处理 30 毫秒
        QThread::msleep(10); // 让出一点 CPU
    } // 结束等待循环
} // 结束泵事件

QString argumentAfter(const QStringList& arguments, const QString& flag, const QString& fallback) { // 读取标志后面的路径
    const int index = arguments.indexOf(flag); // 标志在参数里的位置
    if (index >= 0 && index + 1 < arguments.size() && !arguments.at(index + 1).startsWith(QLatin1Char('-'))) { // 下一项存在且不是另一个标志
        return arguments.at(index + 1); // 使用调用方给的路径
    } // 结束找到了值
    return fallback; // 没有就用默认路径
} // 结束读取参数

int averageLuma(const QImage& image) { // 稀疏采样的平均亮度
    if (image.isNull()) { // 没有图像
        return -1; // 表示无效
    } // 结束空图
    qint64 sum = 0; // 亮度累加
    int count = 0; // 采样点数
    const int stepX = std::max(1, image.width() / 40); // 横向大约 40 步
    const int stepY = std::max(1, image.height() / 40); // 纵向大约 40 步
    for (int y = 0; y < image.height(); y += stepY) { // 按行抽样
        for (int x = 0; x < image.width(); x += stepX) { // 按列抽样
            const QColor color = image.pixelColor(x, y); // 该点颜色
            sum += (color.red() * 3 + color.green() * 6 + color.blue()) / 10; // 偏重绿色的亮度
            ++count; // 点数加一
        } // 结束一行里的点
    } // 结束所有行
    return count == 0 ? -1 : static_cast<int>(sum / count); // 没有点则无效
} // 结束平均亮度

bool saveScreenSample(const QString& path, int x, int y, int width, int height) { // 从屏幕拷一块位图
    HDC screen = GetDC(nullptr); // 整个屏幕的 DC
    if (!screen) { // 拿不到
        return false; // 无法采样
    } // 结束没有 DC
    HDC memory = CreateCompatibleDC(screen); // 内存 DC
    HBITMAP bitmap = CreateCompatibleBitmap(screen, width, height); // 同样大小的位图
    HGDIOBJ old = SelectObject(memory, bitmap); // 选入位图并记住旧对象
    BitBlt(memory, 0, 0, width, height, screen, x, y, SRCCOPY); // 从屏幕拷贝
    BITMAPINFO info{}; // 取像素用的格式
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); // 头大小
    info.bmiHeader.biWidth = width; // 宽度
    info.bmiHeader.biHeight = -height; // 负高度表示自上而下
    info.bmiHeader.biPlanes = 1; // 一个平面
    info.bmiHeader.biBitCount = 32; // 32 位
    info.bmiHeader.biCompression = BI_RGB; // 不压缩
    std::vector<unsigned char> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u); // 像素缓冲
    GetDIBits(memory, bitmap, 0, static_cast<UINT>(height), pixels.data(), &info, DIB_RGB_COLORS); // 读出像素
    SelectObject(memory, old); // 恢复旧对象
    DeleteObject(bitmap); // 释放位图
    DeleteDC(memory); // 释放内存 DC
    ReleaseDC(nullptr, screen); // 归还屏幕 DC
    QImage image(pixels.data(), width, height, width * 4, QImage::Format_ARGB32); // 包一层，数据仍在 vector 里
    return image.copy().save(path); // 拷出来再保存，避免缓冲释放后失效
} // 结束屏幕采样

void sendEscape() { // 关掉右键菜单
    INPUT input[2] = {}; // 按下和抬起
    input[0].type = INPUT_KEYBOARD; // 键盘输入
    input[0].ki.wVk = VK_ESCAPE; // 按下 Esc
    input[1].type = INPUT_KEYBOARD; // 仍是键盘
    input[1].ki.wVk = VK_ESCAPE; // 同一个键
    input[1].ki.dwFlags = KEYEVENTF_KEYUP; // 抬起
    SendInput(2, input, sizeof(INPUT)); // 送出一对事件
} // 结束发送 Esc

int zOrder(HWND target) { // 顶层 Z 序，数值越小越靠前
    int index = 0; // 从最前面数起
    for (HWND hwnd = GetTopWindow(nullptr); hwnd; hwnd = GetWindow(hwnd, GW_HWNDNEXT), ++index) { // 从前向后走
        if (hwnd == target) { // 找到目标
            return index; // 返回它的序号
        } // 结束命中
    } // 结束遍历
    return -1; // 不在顶层枚举里
} // 结束 Z 序

}  // 结束匿名命名空间

int rowLuma(const QImage& image, int y) { // 某一行的平均亮度
    if (image.isNull() || y < 0 || y >= image.height()) { // 空图或行号越界
        return -1; // 无效
    } // 结束无效行
    qint64 sum = 0; // 亮度累加
    const int step = std::max(1, image.width() / 80); // 一行大约 80 个点
    int count = 0; // 点数
    for (int x = 0; x < image.width(); x += step) { // 沿横向抽样
        const QColor color = image.pixelColor(x, y); // 该点颜色
        sum += (color.red() * 3 + color.green() * 6 + color.blue()) / 10; // 同样的亮度权重
        ++count; // 点数加一
    } // 结束这一行
    return count == 0 ? -1 : static_cast<int>(sum / count); // 平均亮度
} // 结束行亮度

int meanAbsDiff(const QImage& a, const QImage& b) { // 两帧的平均绝对差，用来看有没有在动
    if (a.isNull() || b.isNull() || a.size() != b.size()) { // 缺图或尺寸不同
        return -1; // 无法比较
    } // 结束无效输入
    qint64 sum = 0; // 通道差累加
    int count = 0; // 采样点数
    const int stepX = std::max(1, a.width() / 48); // 横向步长
    const int stepY = std::max(1, a.height() / 48); // 纵向步长
    for (int y = 0; y < a.height(); y += stepY) { // 按行抽样
        for (int x = 0; x < a.width(); x += stepX) { // 按列抽样
            const QColor ca = a.pixelColor(x, y); // 第一帧
            const QColor cb = b.pixelColor(x, y); // 第二帧
            sum += std::abs(ca.red() - cb.red()) + std::abs(ca.green() - cb.green()) + std::abs(ca.blue() - cb.blue()); // 三个通道的绝对差
            ++count; // 点数加一
        } // 结束一行
    } // 结束所有行
    return count == 0 ? -1 : static_cast<int>(sum / count); // 平均差
} // 结束帧差

bool saveShot(OceanView* view, const QString& path, QStringList* log) { // 等一等再抓一张预览
    pump(350); // 先让固定时刻的画面稳定
    const QImage image = view->surface()->capture(); // 抓帧缓冲
    const bool saved = !image.isNull() && image.save(path); // 非空才保存
    const int horizon = rowLuma(image, image.height() * 42 / 100); // 地平线附近
    const int above = rowLuma(image, image.height() * 36 / 100); // 地平线上方
    const int below = rowLuma(image, image.height() * 48 / 100); // 地平线下方
    log->append(QStringLiteral("%1 saved=%2 luma=%3 size=%4x%5 horizonRow=%6 above=%7 below=%8") // 测试日志格式保持原文
                    .arg(path) // 文件路径
                    .arg(saved ? QStringLiteral("yes") : QStringLiteral("no")) // 是否写盘
                    .arg(averageLuma(image)) // 整幅亮度
                    .arg(image.width()) // 宽
                    .arg(image.height()) // 高
                    .arg(horizon) // 地平线行
                    .arg(above) // 上方行
                    .arg(below)); // 下方行
    return saved; // 调用方据此记失败
} // 结束保存预览

int runDumpPreview(QApplication& application, const QStringList& arguments) { // 只导出预览，不挂桌面壁纸
    application.setQuitOnLastWindowClosed(false); // 关掉预览窗也不要顺便退出应用对象
    const QString directory = argumentAfter(arguments, QStringLiteral("--dump-preview"), // 标志后面的输出目录
                                            QCoreApplication::applicationDirPath() + QStringLiteral("/preview")); // 默认在程序旁的 preview
    QDir().mkpath(directory); // 保证目录存在
    QWidget host; // 临时预览窗，不是壁纸窗口
    host.setWindowTitle(QStringLiteral("OceanLivePreviewDump")); // 和壁纸标题区分开
    auto* view = new OceanView(&host); // 这个视图不绑定视频播放器
    auto* layout = new QVBoxLayout(&host); // 布局属于临时窗
    layout->setContentsMargins(0, 0, 0, 0); // 不留边
    layout->addWidget(view); // 视图填满
    host.resize(1280, 720); // 先给一个初始尺寸
    host.show(); // 创建 GL 表面
    pump(250); // 等表面起来
    const qreal dpr = std::max(1.0, host.devicePixelRatioF()); // 至少按 1 倍缩放
    host.resize(std::lround(1920.0 / dpr), std::lround(1080.0 / dpr)); // 让帧缓冲接近 1920x1080
    view->surface()->setFixedTime(8.0); // 固定动画时刻，导出才稳定
    QStringList log; // 指标行
    bool ok = true; // 任一张失败就变成假

    OceanParams sunny = OceanParams{}.withTheme(OceanTheme::Sunny); // 晴天基准
    sunny.quality = 2; // 高质量
    sunny.targetFps = 60; // 目标 60 帧
    struct Shot { // 一次导出的名字和参数
        const char* name; // 文件名不含扩展名
        OceanParams params; // 这一张用的参数
    }; // 结束镜头描述
    OceanParams hazeLow = sunny; // 从晴天复制
    hazeLow.atmosphereHaze = 0.0f; // 去掉大气雾
    hazeLow.horizonHaze = 0.0f; // 去掉地平线雾
    OceanParams sunHigh = sunny; // 另一份晴天
    sunHigh.sunIntensity = 1.65f; // 更强的太阳
    sunHigh.sunReflection = 1.70f; // 更强的反射
    sunHigh.sunDisk = 1.85f; // 更大的日盘
    sunHigh.sunHalo = 0.40f; // 光晕
    sunHigh.highlightWidth = 0.72f; // 高光宽度
    const Shot shots[] = { // 固定导出这五张
        {"sunny", sunny}, // 晴天
        {"sunny-haze-low", hazeLow}, // 低雾晴天
        {"sunny-sun-high", sunHigh}, // 强阳光
        {"sunset", OceanParams{}.withTheme(OceanTheme::Sunset)}, // 日落
        {"deep", OceanParams{}.withTheme(OceanTheme::Deep)}, // 深海
    }; // 结束镜头表
    const QString beforeSource = QCoreApplication::applicationDirPath() + QStringLiteral("/preview-v13/sunny.png"); // 上一版晴天图
    const QString beforeTarget = directory + QStringLiteral("/sunny-before.png"); // 拷到本次目录做对照
    if (QFile::exists(beforeSource)) { // 旧图还在
        QFile::remove(beforeTarget); // 先删掉目标，复制才不会被旧文件挡住
        log.append(QStringLiteral("sunny-before copied=%1") // 记录是否复制成功
                       .arg(QFile::copy(beforeSource, beforeTarget) ? QStringLiteral("yes") : QStringLiteral("no"))); // yes 或 no
    } else { // 没有旧图
        log.append(QStringLiteral("sunny-before missing=%1").arg(beforeSource)); // 记下缺失路径
    } // 结束旧图
    QImage sunnyImage; // 留给裁切和运动比较
    for (const Shot& shot : shots) { // 逐张导出
        Logger::info(QStringLiteral("Dump"), QStringLiteral("begin %1").arg(QString::fromLatin1(shot.name))); // 日志标记开始
        OceanParams params = shot.params; // 拷一份，避免改表里的原值
        params.quality = 2; // 再次强制高质量
        params.targetFps = 60; // 再次强制 60 帧
        view->setParams(params); // 应用到视图
        view->surface()->setFixedTime(8.0); // 每张都从同一时刻画
        pump(shot.name[0] == 's' && QLatin1String(shot.name) == "sunny" ? 900 : 500); // 第一张晴天多等一会
        if (!view->surface()->isReady()) { // 渲染器没起来
            log.append(QStringLiteral("FAIL renderer %1").arg(view->stats().error)); // 记下渲染错误
            ok = false; // 整次导出失败
            break; // 不再导出后面的
        } // 结束未就绪
        const QString path = directory + QLatin1Char('/') + QString::fromLatin1(shot.name) + QStringLiteral(".png"); // png 路径
        if (!saveShot(view, path, &log)) { // 保存失败
            ok = false; // 记失败但继续后面的镜头
        } // 结束保存失败
        Logger::info(QStringLiteral("Dump"), log.isEmpty() ? path : log.last()); // 把最后一条指标写进日志
        if (QLatin1String(shot.name) == "sunny") { // 只留基准晴天
            sunnyImage = view->surface()->capture(); // 给裁切和运动用
        } // 结束保存晴天帧
    } // 结束五张镜头
    if (!sunnyImage.isNull()) { // 晴天帧抓到了
        const QImage farCrop = sunnyImage.copy(0, sunnyImage.height() * 34 / 100, sunnyImage.width(), sunnyImage.height() * 16 / 100); // 地平线一带
        const QImage nearCrop = sunnyImage.copy(0, sunnyImage.height() * 76 / 100, sunnyImage.width(), sunnyImage.height() * 22 / 100); // 近处海面
        const bool farOk = farCrop.save(directory + QStringLiteral("/sunny-far.png")); // 保存地平线裁图
        const bool nearOk = nearCrop.save(directory + QStringLiteral("/sunny-near.png")); // 保存近处裁图
        log.append(QStringLiteral("sunny-far saved=%1 luma=%2").arg(farOk ? QStringLiteral("yes") : QStringLiteral("no")).arg(averageLuma(farCrop))); // 地平线裁图结果
        log.append(QStringLiteral("sunny-near saved=%1 luma=%2").arg(nearOk ? QStringLiteral("yes") : QStringLiteral("no")).arg(averageLuma(nearCrop))); // 近处裁图结果
        ok = ok && farOk && nearOk; // 两张裁图都要成功
        view->surface()->setFixedTime(8.35); // 时间稍往后，看海面是否变化
        const QImage moved = view->surface()->capture(); // 后一时刻的帧
        log.append(QStringLiteral("motion meanAbsDiff=%1").arg(meanAbsDiff(sunnyImage, moved))); // 两帧平均差
        if (!moved.save(directory + QStringLiteral("/sunny-motion.png"))) { // 运动帧写盘
            ok = false; // 写失败
        } // 结束运动帧
    } // 结束晴天后续

    view->setParams(sunny); // 全屏性能回到晴天基准
    view->surface()->setFixedTime(8.0); // 先回到原时刻
    host.showFullScreen(); // 预览窗铺满，用来看帧缓冲尺寸
    pump(2200); // 等全屏后的帧率稳定
    const RenderStats full = view->stats(); // 全屏时的统计
    const QImage fullFrame = view->surface()->capture(); // 全屏帧缓冲
    log.append(QStringLiteral("fullscreen framebuffer=%1x%2 fps=%3 cpuMs=%4 gpuMs=%5 gpuValid=%6 vram=%7/%8") // 性能日志格式保持原文
                   .arg(fullFrame.width()) // 帧缓冲宽
                   .arg(fullFrame.height()) // 帧缓冲高
                   .arg(full.fps, 0, 'f', 1) // 帧率
                   .arg(full.cpuFrameMs, 0, 'f', 2) // CPU 帧时间
                   .arg(full.gpuValid ? QString::number(full.gpuFrameMs, 'f', 2) : QStringLiteral("不可用")) // GPU 时间或不可用
                   .arg(full.gpuValid ? QStringLiteral("yes") : QStringLiteral("no")) // GPU 计时是否有效
                   .arg(full.gpuMemoryUsedMb >= 0.0 ? QString::number(full.gpuMemoryUsedMb, 'f', 0) : QStringLiteral("不可用")) // 已用显存
                   .arg(full.gpuMemoryTotalMb >= 0.0 ? QString::number(full.gpuMemoryTotalMb, 'f', 0) : QStringLiteral("不可用"))); // 总显存
    fullFrame.save(directory + QStringLiteral("/sunny-4k.png")); // 全屏帧，失败与否不单独改 ok
    OceanParams fps30 = sunny; // 再测 30 帧目标
    fps30.targetFps = 30; // 目标改为 30
    view->setParams(fps30); // 应用较低帧率
    pump(1600); // 等帧率降下来
    log.append(QStringLiteral("target30 fps=%1").arg(view->stats().fps, 0, 'f', 1)); // 记录实际帧率
    const quint64 beforePause = view->surface()->frameCount(); // 暂停前的帧号
    view->setPaused(true); // 暂停动画
    pump(700); // 暂停期间继续泵事件
    const quint64 afterPause = view->surface()->frameCount(); // 暂停后的帧号
    log.append(QStringLiteral("paused framesBefore=%1 framesAfter=%2 timer=%3") // 帧号不应再增加
                   .arg(beforePause) // 暂停前
                   .arg(afterPause) // 暂停后
                   .arg(view->surface()->timerActive() ? QStringLiteral("on") : QStringLiteral("off"))); // 计时器是否还开着
    log.append(QStringLiteral("gl=%1").arg(full.version)); // OpenGL 版本
    log.append(QStringLiteral("gpu=%1").arg(full.renderer)); // 显卡字符串

    QFile metrics(directory + QStringLiteral("/preview-metrics.txt")); // 指标文件
    if (metrics.open(QIODevice::WriteOnly | QIODevice::Truncate)) { // 覆盖写入
        metrics.write(log.join(QLatin1Char('\n')).toUtf8()); // 所有行
        metrics.write("\n"); // 文件以换行结束
    } // 结束写指标
    for (const QString& line : log) { // 每行再打到日志
        Logger::info(QStringLiteral("Dump"), line); // 分类为 Dump
    } // 结束刷日志
    host.close(); // 关掉临时预览窗
    application.processEvents(); // 把关闭处理完
    return ok ? 0 : 1; // 成功 0，失败 1；调用方在界面之前就返回
} // 结束预览导出

int runSelfTest(QApplication&, MainWindow& window, WallpaperManager& wallpapers, const QStringList& arguments) { // 检查真正挂上桌面的壁纸
    const QString reportPath = argumentAfter(arguments, QStringLiteral("--self-test"), // 报告路径可跟在标志后
                                             QCoreApplication::applicationDirPath() + QStringLiteral("/self-test-report.txt")); // 默认放在程序目录
    QDir().mkpath(QFileInfo(reportPath).absolutePath()); // 报告目录
    QStringList lines; // 报告正文
    int failures = 0; // 失败条数
    auto record = [&](bool pass, const QString& text) { // 记一条通过或失败
        const QString line = QStringLiteral("%1 %2").arg(pass ? QStringLiteral("PASS") : QStringLiteral("FAIL"), text); // 前缀加说明
        if (!pass) { // 只有失败才计数
            ++failures; // 累加
        } // 结束计数
        lines << line; // 写入报告
        Logger::info(QStringLiteral("SelfTest"), line); // 同时写日志
    }; // 结束记录
    auto note = [&](const QString& text) { // 只记录、不算失败
        lines << text; // 写入报告
        Logger::info(QStringLiteral("SelfTest"), text); // 写入日志
    }; // 结束备注

    note(QStringLiteral("日志: %1").arg(Logger::logFilePath())); // 日志路径保持这句中文
    wallpapers.start(); // 按正常路径挂接壁纸
    pump(400); // 给挂接和首帧一点时间
    const bool started = wallpapers.state() != RunState::Stopped && wallpapers.windowCount() > 0; // 状态不是停止且有窗口
    record(started, started ? QStringLiteral("壁纸窗口已挂接") : wallpapers.lastError()); // 失败时带上原因
    note(WorkerWHost::describe(wallpapers.host())); // 宿主说明
    note(WorkerWHost::hierarchySummary()); // 桌面层级

    HWND wallpaperHwnd = nullptr; // 第一块壁纸的句柄
    if (started) { // 没挂上就跳过窗口检查
        wallpaperHwnd = reinterpret_cast<HWND>(wallpapers.windowAt(0)->winId()); // 取第一个窗口
        RECT windowRect{}; // 屏幕矩形
        GetWindowRect(wallpaperHwnd, &windowRect); // 读取外框
        note(QStringLiteral("壁纸窗口矩形 %1,%2 %3x%4") // 左上角和尺寸
                 .arg(windowRect.left) // 左
                 .arg(windowRect.top) // 上
                 .arg(windowRect.right - windowRect.left) // 宽
                 .arg(windowRect.bottom - windowRect.top)); // 高
        const LONG style = GetWindowLongW(wallpaperHwnd, GWL_STYLE); // 基本样式
        const LONG ex = GetWindowLongW(wallpaperHwnd, GWL_EXSTYLE); // 扩展样式
        const bool child = (style & WS_CHILD) != 0; // 必须是子窗口
        const bool topmost = (ex & WS_EX_TOPMOST) != 0; // 不能是置顶
        const bool appWindow = (ex & WS_EX_APPWINDOW) != 0; // 不能进任务栏
        const bool noActivate = (ex & WS_EX_NOACTIVATE) != 0; // 不能被激活
        note(QStringLiteral("样式 child=%1 topmost=%2 appwindow=%3 noactivate=%4 parent=%5") // 样式和父句柄
                 .arg(child) // 是否子窗口
                 .arg(topmost) // 是否置顶
                 .arg(appWindow) // 是否任务栏按钮
                 .arg(noActivate) // 是否不激活
                 .arg(reinterpret_cast<quintptr>(GetParent(wallpaperHwnd)), 0, 16)); // 父窗口十六进制
        record(child && !topmost && !appWindow && noActivate, QStringLiteral("壁纸窗口样式不会置顶、不进任务栏、不激活")); // 四项必须同时满足
        record(WorkerWHost::iconsAboveWallpaper(wallpapers.host()), QStringLiteral("桌面图标层位于壁纸宿主之上")); // 图标不能被壁纸盖住

        const HWND tray = FindWindowW(L"Shell_TrayWnd", nullptr); // 任务栏
        const int trayZ = zOrder(tray); // 任务栏的 Z 序
        const int wallZ = zOrder(wallpapers.host().wallpaperHost); // 壁纸宿主的 Z 序
        note(QStringLiteral("Z 序 任务栏=%1 壁纸宿主=%2（数值越小越靠前）").arg(trayZ).arg(wallZ)); // 说明数值方向
        record(tray && trayZ >= 0 && wallZ >= 0 && trayZ < wallZ, QStringLiteral("任务栏位于壁纸宿主之前")); // 任务栏必须更靠前

        QImage framebuffer = wallpapers.windowAt(0)->view()->surface()->capture(); // 海洋帧缓冲
        const QString imagePath = QFileInfo(reportPath).absolutePath() + QStringLiteral("/wallpaper-framebuffer.png"); // 和报告放一起
        framebuffer.save(imagePath); // 保存给人工查看
        const int luma = averageLuma(framebuffer); // 判断是不是黑屏
        note(QStringLiteral("帧缓冲 %1 luma=%2 %3x%4") // 路径、亮度和尺寸
                 .arg(imagePath) // 图片路径
                 .arg(luma) // 亮度
                 .arg(framebuffer.width()) // 宽
                 .arg(framebuffer.height())); // 高
        record(luma > 8, QStringLiteral("海洋帧缓冲不是黑屏")); // 亮度要明显高于全黑
        note(QStringLiteral("OpenGL %1 / %2").arg(wallpapers.windowAt(0)->view()->stats().version, // GL 版本
                                                  wallpapers.windowAt(0)->view()->stats().renderer)); // 显卡
    } // 结束已启动的窗口检查

    window.hide(); // 主窗口躲开命中测试
    pump(250); // 等隐藏生效
    int desktopHits = 0; // 点到图标层的次数
    int wallpaperHits = 0; // 点到壁纸的次数，期望为 0
    int otherHits = 0; // 点到其他窗口
    POINT desktopPoint{-1, -1}; // 第一处空白桌面，留给右键
    const int width = GetSystemMetrics(SM_CXSCREEN); // 主屏宽
    const int height = GetSystemMetrics(SM_CYSCREEN); // 主屏高
    for (int y = 80; y < height - 80; y += 120) { // 躲开屏幕边缘
        for (int x = 80; x < width - 80; x += 160) { // 网格采样
            POINT point{x, y}; // 采样点
            HWND hit = WindowFromPoint(point); // 最上面的窗口
            bool wallpaperHit = false; // 这一下是否打中壁纸
            if (wallpaperHwnd && (hit == wallpaperHwnd || IsChild(wallpaperHwnd, hit))) { // 壁纸自己或它的子窗口
                wallpaperHit = true; // 记为穿透失败
            } // 结束壁纸判断
            const QString cls = classNameOf(hit); // 命中类名
            if (wallpaperHit) { // 打中了壁纸
                ++wallpaperHits; // 穿透计数
            } else if (cls == QLatin1String("SysListView32") || cls == QLatin1String("SHELLDLL_DefView")) { // 桌面图标层
                ++desktopHits; // 桌面计数
                if (desktopPoint.x < 0) { // 还没记下空白点
                    desktopPoint = point; // 留给右键
                } // 结束记录空白点
            } else { // 其他前台窗口
                ++otherHits; // 其他计数
            } // 结束分类
        } // 结束横向
    } // 结束纵向
    note(QStringLiteral("命中测试 桌面=%1 壁纸=%2 其他窗口=%3").arg(desktopHits).arg(wallpaperHits).arg(otherHits)); // 三类计数
    record(wallpaperHits == 0, QStringLiteral("采样点没有落在壁纸窗口上")); // 壁纸必须点不中

    HWND tray = FindWindowW(L"Shell_TrayWnd", nullptr); // 再找一次任务栏
    if (tray) { // 任务栏存在
        RECT trayRect{}; // 任务栏矩形
        GetWindowRect(tray, &trayRect); // 读取位置
        POINT point{(trayRect.left + trayRect.right) / 2, (trayRect.top + trayRect.bottom) / 2}; // 中心点
        HWND hit = WindowFromPoint(point); // 中心点命中的窗口
        HWND root = GetAncestor(hit, GA_ROOT); // 它的根窗口
        const bool onTray = root == tray || hit == tray; // 根或自己是任务栏
        record(onTray, QStringLiteral("任务栏中心点命中 Shell_TrayWnd，当前类 %1").arg(classNameOf(hit))); // 失败时带上实际类名
    } else { // 没有任务栏窗口
        record(false, QStringLiteral("找不到任务栏窗口")); // 记失败
    } // 结束任务栏命中

    if (desktopPoint.x >= 0) { // 有一处点到了桌面
        POINT previous{}; // 原来的鼠标位置
        GetCursorPos(&previous); // 测完要放回去
        SetCursorPos(desktopPoint.x, desktopPoint.y); // 移到空白桌面
        INPUT click[2] = {}; // 右键按下和抬起
        click[0].type = INPUT_MOUSE; // 鼠标
        click[0].mi.dwFlags = MOUSEEVENTF_RIGHTDOWN; // 按下右键
        click[1].type = INPUT_MOUSE; // 鼠标
        click[1].mi.dwFlags = MOUSEEVENTF_RIGHTUP; // 抬起右键
        SendInput(2, click, sizeof(INPUT)); // 送出右键
        pump(450); // 等系统菜单出现
        HWND menu = FindWindowW(L"#32768", nullptr); // 系统菜单类
        record(menu != nullptr, QStringLiteral("空白桌面右键出现系统菜单")); // 菜单必须出现
        sendEscape(); // 关掉菜单
        pump(150); // 等菜单消失
        SetCursorPos(previous.x, previous.y); // 鼠标放回原处
        if (menu) { // 菜单句柄曾存在
            sendEscape(); // 再送一次，防止菜单还在
        } // 结束补关菜单
    } else { // 采样点都被别的窗口挡住
        note(QStringLiteral("SKIP 空白桌面右键：当前采样点都被其他窗口挡住，桌面图标层本身仍在壁纸之上。")); // 跳过但不算失败
    } // 结束右键
    note(QStringLiteral("图标拖动：壁纸命中数为 %1。未移动用户的桌面图标。图标宿主保持在壁纸宿主之上。").arg(wallpaperHits)); // 声明没有拖动图标
    window.show(); // 主窗口回到前面

    if (started && wallpapers.state() == RunState::Running) { // 只有正在跑才测暂停
        QWidget probe; // 临时全屏窗口
        probe.setWindowTitle(QStringLiteral("OceanLiveFullscreenProbe")); // 启发式认得这个标题
        probe.setWindowFlags(Qt::FramelessWindowHint | Qt::Window); // 无边框顶层窗，不是壁纸
        if (QScreen* screen = QGuiApplication::primaryScreen()) { // 有主屏
            probe.setGeometry(screen->geometry()); // 先铺到主屏
        } // 结束设置几何
        probe.showFullScreen(); // 真正全屏
        probe.activateWindow(); // Qt 侧激活
        SetForegroundWindow(reinterpret_cast<HWND>(probe.winId())); // 再要一次前台
        pump(500); // 等系统把它当成前台
        wallpapers.pollSystem(); // 让监视器看到全屏
        record(wallpapers.pauseReason() == PauseReason::Fullscreen, QStringLiteral("全屏探测窗口触发暂停")); // 应当因全屏暂停
        probe.close(); // 关掉探测窗
        pump(300); // 等前台恢复
        wallpapers.pollSystem(); // 再查一次
        record(wallpapers.pauseReason() == PauseReason::None, QStringLiteral("全屏探测窗口关闭后恢复动画")); // 没有其他暂停时应恢复

        wallpapers.pauseByUser(); // 用户暂停必须留下来
        probe.showFullScreen(); // 再开一次全屏
        SetForegroundWindow(reinterpret_cast<HWND>(probe.winId())); // 放到前台
        pump(300); // 等检测
        wallpapers.pollSystem(); // 此时可能同时有用户暂停和全屏
        probe.close(); // 退出全屏
        pump(200); // 等关闭
        wallpapers.pollSystem(); // 全屏原因应消失
        record(wallpapers.userPaused(), QStringLiteral("用户暂停不会被全屏退出自动恢复")); // 用户标志还在
        wallpapers.resumeByUser(); // 测试完取消用户暂停
    } // 结束全屏暂停检查

    if (started) { // 再确认宿主还在
        wallpapers.pollSystem(); // 走一遍正常轮询
        record(wallpapers.windowCount() > 0 && wallpapers.state() != RunState::Stopped, QStringLiteral("重新检查宿主后壁纸仍在")); // 窗口和状态都还在
    } // 结束复查

    wallpapers.stop(); // 卸下壁纸并释放播放器
    pump(200); // 等销毁完成
    record(wallpapers.state() == RunState::Stopped && wallpapers.windowCount() == 0, QStringLiteral("停止后没有残留壁纸窗口")); // 不能留下子窗口

    note(QStringLiteral("主窗口标题: %1").arg(window.windowTitle())); // 记录主窗口标题
    QFile file(reportPath); // 报告文件
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) { // 覆盖成文本
        QTextStream stream(&file); // 用文本流
        stream.setEncoding(QStringConverter::Utf8); // 报告用 UTF-8
        stream << lines.join(QLatin1Char('\n')) << '\n'; // 正文加结尾换行
        stream << QStringLiteral("failures=%1\n").arg(failures); // 失败数
    } // 结束写报告
    note(QStringLiteral("报告 %1 failures=%2").arg(reportPath).arg(failures)); // 日志里再写一次路径
    return failures == 0 ? 0 : 1; // 有失败返回 1
} // 结束桌面自测
