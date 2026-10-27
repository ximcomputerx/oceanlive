// SPDX-License-Identifier: Apache-2.0
#include "app/AppCommands.h" // 视频壁纸自测声明

#include "platform/win32/WorkerWHost.h" // 宿主说明
#include "render/VideoPlayer.h" // 进程内那一个播放器
#include "services/SettingsManager.h" // 改视频设置并在结束时还原
#include "ui/MainWindow.h" // 自测时隐藏主窗口
#include "utils/Logger.h" // 测试日志
#include "wallpaper/WallpaperManager.h" // 用正常路径挂接视频壁纸

#include <QApplication> // 泵事件
#include <QCoreApplication> // 缺失文件文案必须用同一上下文翻译
#include <QAudioOutput> // 对照控件静音
#include <QColor> // 识别测试图案颜色
#include <QDir> // 临时视频和报告目录
#include <QElapsedTimer> // 等待计时
#include <QFile> // 写报告
#include <QFileInfo> // 报告旁边的截图路径
#include <QImage> // 帧缓冲和 PrintWindow 图像
#include <QMediaPlayer> // 仅用于对照的控件播放器
#include <QThread> // 等待时睡眠
#include <QUrl> // 本地视频地址
#include <QVideoWidget> // 对照用的视频控件

#include <cstring> // 拷贝帧像素
#include <vector> // 未压缩帧缓冲

#ifndef NOMINMAX // 外面没定义时才定义
#define NOMINMAX // 避免 Windows 头里的 min/max 宏
#endif // 结束 NOMINMAX 守卫
#define WIN32_LEAN_AND_MEAN // 少包含无关声明
#include <Windows.h> // 父窗口、扩展样式和 PrintWindow
#include <mfapi.h> // 启动 Media Foundation
#include <mfidl.h> // 样本和媒体类型
#include <mfreadwrite.h> // 写测试 MP4
#include <psapi.h> // 停止前后的工作集

namespace { // 图案、编码和抓图只在本文件使用

void pump(int milliseconds) { // 在限定时间里处理事件
    QElapsedTimer timer; // 累计等待
    timer.start(); // 开始计时
    while (timer.elapsed() < milliseconds) { // 还没到点
        QApplication::processEvents(QEventLoop::AllEvents, 30); // 每轮最多 30 毫秒
        QThread::msleep(10); // 让出 CPU
    } // 结束等待
} // 结束泵事件

QString argumentAfter(const QStringList& arguments, const QString& flag, const QString& fallback) { // 读取标志后的报告路径
    const int index = arguments.indexOf(flag); // 标志位置
    if (index >= 0 && index + 1 < arguments.size() && !arguments.at(index + 1).startsWith(QLatin1Char('-'))) { // 下一项不是另一个标志
        return arguments.at(index + 1); // 使用传入路径
    } // 结束找到值
    return fallback; // 否则用默认路径
} // 结束读取参数

struct ColorHits { // 测试图案的颜色计数
    int magenta = 0; // 左半洋红
    int yellow = 0; // 右半黄
    int redTop = 0; // 顶部红条
    int redBottom = 0; // 底部红点，用来看有没有上下颠倒
    int samples = 0; // 采样点数
}; // 结束颜色计数

ColorHits scanPattern(const QImage& image) { // 在画面里找测试色
    ColorHits hits; // 从零开始
    if (image.isNull()) { // 没有图像
        return hits; // 全部是零
    } // 结束空图
    const int stepX = std::max(1, image.width() / 64); // 横向大约 64 步
    const int stepY = std::max(1, image.height() / 40); // 纵向大约 40 步
    const int top = std::max(1, image.height() * 8 / 100); // 顶部 8%
    const int bottom = image.height() * 92 / 100; // 底部 8% 的起点
    for (int y = 0; y < image.height(); y += stepY) { // 按行抽样
        for (int x = 0; x < image.width(); x += stepX) { // 按列抽样
            const QColor color = image.pixelColor(x, y); // 该点颜色
            ++hits.samples; // 点数
            if (color.red() > 150 && color.blue() > 100 && color.green() < 90) { // 偏洋红
                ++hits.magenta; // 洋红计数
            } // 结束洋红
            if (color.red() > 170 && color.green() > 150 && color.blue() < 90) { // 偏黄
                ++hits.yellow; // 黄色计数
            } // 结束黄色
            if (y < top && color.red() > 200 && color.green() < 80 && color.blue() < 80) { // 顶部纯红
                ++hits.redTop; // 顶部红条
            } // 结束顶部
            if (y > bottom && color.red() > 200 && color.green() < 80 && color.blue() < 80) { // 底部纯红
                ++hits.redBottom; // 底部红点
            } // 结束底部
        } // 结束一行
    } // 结束所有行
    return hits; // 给断言使用
} // 结束扫描图案

double workingSetMb() { // 当前进程工作集，单位 MB
    PROCESS_MEMORY_COUNTERS_EX counters{}; // 内存计数
    if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters))) { // 读取失败
        return -1.0; // 表示不可用
    } // 结束读取失败
    return static_cast<double>(counters.WorkingSetSize) / (1024.0 * 1024.0); // 字节换算成 MB
} // 结束工作集

QImage printWindowImage(HWND hwnd) { // 用 PrintWindow 抓控件，不依赖桌面合成
    RECT rect{}; // 客户区
    if (!hwnd || !GetClientRect(hwnd, &rect) || rect.right < 2 || rect.bottom < 2) { // 没有窗口或太小
        return {}; // 空图
    } // 结束无效窗口
    const int width = rect.right; // 客户区宽
    const int height = rect.bottom; // 客户区高
    HDC windowDc = GetDC(hwnd); // 窗口 DC
    HDC memory = CreateCompatibleDC(windowDc); // 内存 DC
    HBITMAP bitmap = CreateCompatibleBitmap(windowDc, width, height); // 目标位图
    HGDIOBJ old = SelectObject(memory, bitmap); // 选入并记住旧对象
    const BOOL printed = PrintWindow(hwnd, memory, 2); // 标志 2 包含客户区之外的内容渲染
    BITMAPINFO info{}; // 取像素格式
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); // 头大小
    info.bmiHeader.biWidth = width; // 宽
    info.bmiHeader.biHeight = -height; // 自上而下
    info.bmiHeader.biPlanes = 1; // 一个平面
    info.bmiHeader.biBitCount = 32; // 32 位
    info.bmiHeader.biCompression = BI_RGB; // 不压缩
    std::vector<unsigned char> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u); // 像素缓冲
    GetDIBits(memory, bitmap, 0, static_cast<UINT>(height), pixels.data(), &info, DIB_RGB_COLORS); // 读出像素
    SelectObject(memory, old); // 恢复旧对象
    DeleteObject(bitmap); // 释放位图
    DeleteDC(memory); // 释放内存 DC
    if (windowDc) { // DC 拿成功过
        ReleaseDC(hwnd, windowDc); // 归还
    } // 结束归还 DC
    if (!printed) { // PrintWindow 失败
        return {}; // 不当成有效图
    } // 结束打印失败
    QImage image(pixels.data(), width, height, width * 4, QImage::Format_ARGB32); // 包装缓冲
    return image.copy(); // 拷贝后缓冲可以释放
} // 结束 PrintWindow

void fillPattern(unsigned char* data, int width, int height, int frame) { // 画一帧可识别的测试图
    const int box = (frame * 14) % std::max(1, width - 90); // 白块随帧移动
    for (int y = 0; y < height; ++y) { // 每一行
        unsigned char* row = data + static_cast<size_t>(y) * static_cast<size_t>(width) * 4u; // 这一行的 BGRA
        for (int x = 0; x < width; ++x) { // 每一个像素
            unsigned char r = 210; // 右半默认偏黄
            unsigned char g = 190; // 黄的绿
            unsigned char b = 30; // 黄的蓝
            if (x < width / 2) { // 左半改成洋红
                r = 180; // 洋红的红
                g = 20; // 洋红的绿
                b = 140; // 洋红的蓝
            } // 结束左半
            if (y < 24) { // 顶部一条红
                r = 255; // 纯红
                g = 0; // 无绿
                b = 0; // 无蓝
            } // 结束顶栏
            if (x > box && x < box + 80 && y > 40 && y < 130) { // 移动的白块
                r = 255; // 白
                g = 255; // 白
                b = 255; // 白
            } // 结束白块
            row[x * 4 + 0] = b; // MF 的 RGB32 按 B、G、R、A 存放
            row[x * 4 + 1] = g; // 绿
            row[x * 4 + 2] = r; // 红
            row[x * 4 + 3] = 255; // 不透明
        } // 结束像素
    } // 结束行
} // 结束填充图案

QString hresultText(HRESULT hr) { // 把失败码格式化成日志
    return QStringLiteral("0x%1").arg(static_cast<unsigned long>(hr), 8, 16, QLatin1Char('0')); // 八位十六进制
} // 结束错误码文本

bool writeMediaFoundationVideo(const QString& path, QString* error) { // 生成短测试 MP4
    const int width = 640; // 测试视频宽
    const int height = 360; // 测试视频高
    const int frames = 48; // 大约一秒半
    HRESULT hr = MFStartup(MF_VERSION); // 启动 Media Foundation
    if (FAILED(hr)) { // 启动失败
        if (error) { // 调用方要原因
            *error = QStringLiteral("MFStartup 失败 %1").arg(hresultText(hr)); // 日志原文保持中文
        } // 结束填写错误
        return false; // 无法编码
    } // 结束启动失败
    IMFSinkWriter* writer = nullptr; // MP4 写入器
    hr = MFCreateSinkWriterFromURL(reinterpret_cast<const wchar_t*>(path.utf16()), nullptr, nullptr, &writer); // 按路径创建
    if (FAILED(hr) || !writer) { // 没创建出来
        MFShutdown(); // 配对关闭
        if (error) { // 填写原因
            *error = QStringLiteral("无法创建 MP4 写入器 %1").arg(hresultText(hr)); // 日志原文
        } // 结束填写
        return false; // 失败
    } // 结束创建失败
    IMFMediaType* outType = nullptr; // 输出类型
    MFCreateMediaType(&outType); // 创建空类型
    outType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video); // 视频
    outType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264); // 输出为 H.264
    outType->SetUINT32(MF_MT_AVG_BITRATE, 2500000); // 平均码率
    MFSetAttributeSize(outType, MF_MT_FRAME_SIZE, width, height); // 输出尺寸
    MFSetAttributeRatio(outType, MF_MT_FRAME_RATE, 30, 1); // 每秒 30 帧
    MFSetAttributeRatio(outType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1); // 方形像素
    outType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive); // 逐行
    DWORD stream = 0; // 写入器分配的流号
    hr = writer->AddStream(outType, &stream); // 加入输出流
    outType->Release(); // 类型已交给写入器
    if (FAILED(hr)) { // 输出类型被拒绝
        writer->Release(); // 释放写入器
        MFShutdown(); // 关闭 MF
        if (error) { // 填写原因
            *error = QStringLiteral("H264 输出类型失败 %1").arg(hresultText(hr)); // 日志原文
        } // 结束填写
        return false; // 失败
    } // 结束输出类型失败
    IMFMediaType* inType = nullptr; // 未压缩输入类型
    MFCreateMediaType(&inType); // 创建
    inType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video); // 视频
    inType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32); // 32 位输入
    MFSetAttributeSize(inType, MF_MT_FRAME_SIZE, width, height); // 输入尺寸
    MFSetAttributeRatio(inType, MF_MT_FRAME_RATE, 30, 1); // 同样 30 帧
    MFSetAttributeRatio(inType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1); // 方形像素
    inType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive); // 逐行
    inType->SetUINT32(MF_MT_DEFAULT_STRIDE, static_cast<UINT32>(width * 4)); // 每行字节数
    hr = writer->SetInputMediaType(stream, inType, nullptr); // 告诉写入器输入格式
    inType->Release(); // 释放输入类型
    if (FAILED(hr)) { // 输入类型被拒绝
        writer->Release(); // 释放写入器
        MFShutdown(); // 关闭 MF
        if (error) { // 填写原因
            *error = QStringLiteral("RGB32 输入类型失败 %1").arg(hresultText(hr)); // 日志原文
        } // 结束填写
        return false; // 失败
    } // 结束输入类型失败
    hr = writer->BeginWriting(); // 开始写样本
    if (FAILED(hr)) { // 不能开始
        writer->Release(); // 释放写入器
        MFShutdown(); // 关闭 MF
        if (error) { // 填写原因
            *error = QStringLiteral("BeginWriting 失败 %1").arg(hresultText(hr)); // 日志原文
        } // 结束填写
        return false; // 失败
    } // 结束开始失败
    bool wrote = true; // 所有帧都写成功才算
    std::vector<unsigned char> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u); // 一帧缓冲
    for (int frame = 0; frame < frames; ++frame) { // 逐帧
        fillPattern(pixels.data(), width, height, frame); // 填测试图
        IMFMediaBuffer* buffer = nullptr; // 样本缓冲
        IMFSample* sample = nullptr; // 一帧样本
        hr = MFCreateMemoryBuffer(static_cast<DWORD>(pixels.size()), &buffer); // 按字节数分配
        BYTE* destination = nullptr; // 锁定后的指针
        if (SUCCEEDED(hr)) { // 缓冲创建成功
            hr = buffer->Lock(&destination, nullptr, nullptr); // 锁定以便拷贝
        } // 结束锁定
        if (SUCCEEDED(hr) && destination) { // 可以写入
            memcpy(destination, pixels.data(), pixels.size()); // 拷进 MF 缓冲
            buffer->Unlock(); // 解锁
            buffer->SetCurrentLength(static_cast<DWORD>(pixels.size())); // 有效长度
            hr = MFCreateSample(&sample); // 创建样本
        } // 结束拷贝
        if (SUCCEEDED(hr) && sample) { // 样本可用
            sample->AddBuffer(buffer); // 挂上像素
            const LONGLONG time = static_cast<LONGLONG>(frame) * 333333; // 30 帧对应的 100 纳秒时间
            sample->SetSampleTime(time); // 时间戳
            sample->SetSampleDuration(333333); // 一帧时长
            hr = writer->WriteSample(stream, sample); // 编码这一帧
        } // 结束写入样本
        if (sample) { // 样本创建过
            sample->Release(); // 释放样本
        } // 结束释放样本
        if (buffer) { // 缓冲创建过
            buffer->Release(); // 释放缓冲
        } // 结束释放缓冲
        if (FAILED(hr)) { // 这一帧失败
            wrote = false; // 整段视频无效
            if (error) { // 填写原因
                *error = QStringLiteral("写入视频帧失败 %1").arg(hresultText(hr)); // 日志原文
            } // 结束填写
            break; // 不再写后面的帧
        } // 结束帧失败
    } // 结束逐帧
    if (wrote) { // 帧都写完了
        hr = writer->Finalize(); // 收尾 MP4
        if (FAILED(hr)) { // 收尾失败
            wrote = false; // 文件不完整
            if (error) { // 填写原因
                *error = QStringLiteral("完成 MP4 失败 %1").arg(hresultText(hr)); // 日志原文
            } // 结束填写
        } // 结束收尾失败
    } // 结束收尾
    writer->Release(); // 关闭写入器
    MFShutdown(); // 关闭 Media Foundation
    return wrote; // 是否得到可播放文件
} // 结束生成测试视频

}  // 结束匿名命名空间

int runVideoDesktopTest(QApplication&, MainWindow& window, WallpaperManager& wallpapers, SettingsManager* settings, // 用正式壁纸路径测视频
                        VideoPlayer* video, const QStringList& arguments) { // 只用传入的这一个播放器挂壁纸
    const QString reportPath = argumentAfter(arguments, QStringLiteral("--video-desktop-test"), // 报告可跟在标志后
                                             QCoreApplication::applicationDirPath() + QStringLiteral("/video-desktop-report.txt")); // 默认文件名
    QDir().mkpath(QFileInfo(reportPath).absolutePath()); // 报告目录
    QStringList lines; // 报告行
    int failures = 0; // FAIL 的条数
    auto record = [&](const QString& mark, const QString& text) { // PASS、FAIL 或 BLOCKED
        if (mark == QStringLiteral("FAIL")) { // 只有 FAIL 影响退出码
            ++failures; // 累加
        } // 结束计数
        const QString line = mark + QLatin1Char(' ') + text; // 标记加说明
        lines << line; // 放入报告
        Logger::info(QStringLiteral("VideoTest"), line); // 写入日志
    }; // 结束记录

    window.hide(); // 主窗口不要挡住检查
    const AppSettings original = settings->current(); // 测完要原样恢复
    struct Restore { // 任何返回路径都还原
        SettingsManager* settings; // 设置对象
        AppSettings original; // 进入测试前的设置
        WallpaperManager* wallpapers; // 用来停止壁纸
        ~Restore() { // 离开函数时执行
            wallpapers->stop(); // 卸下壁纸并让唯一播放器释放
            settings->apply(original); // 恢复用户原来的设置
        } // 结束还原
    } restore{settings, original, &wallpapers}; // 立即武装还原

    const QString videoPath = QDir::tempPath() + QStringLiteral("/OceanLive-pattern.mp4"); // 临时测试视频
    QString encoderError; // 编码失败原因
    const bool encoded = writeMediaFoundationVideo(videoPath, &encoderError); // 生成图案 MP4
    if (!encoded) { // 本机编码器不可用
        record(QStringLiteral("BLOCKED"), QStringLiteral("测试视频没有生成：%1").arg(encoderError)); // 阻塞而不是普通失败
        QFile report(reportPath); // 仍写出报告
        if (report.open(QIODevice::WriteOnly | QIODevice::Truncate)) { // 覆盖
            report.write(lines.join(QLatin1Char('\n')).toUtf8()); // 已有的行
        } // 结束写报告
        return 2; // 2 表示环境阻塞
    } // 结束不能编码
    record(QStringLiteral("PASS"), QStringLiteral("已生成测试图案 MP4 %1").arg(videoPath)); // 视频已生成

    AppSettings next = original; // 在原设置上改画面模式
    next.ocean = next.ocean.withTheme(OceanTheme::Sunny); // 主题先设晴天
    next.ocean.quality = 2; // 高质量
    next.ocean.targetFps = 60; // 60 帧
    next.ocean.renderMode = RenderMode::Video; // 切换到视频壁纸
    next.ocean.videoPath = videoPath; // 播放刚生成的文件
    next.ocean.videoLoop = true; // 循环
    next.ocean.videoMuted = true; // 静音，避免测试出声
    settings->apply(next); // 写入设置，管理器会收到变化

    wallpapers.stop(); // 先停掉可能已存在的海面
    wallpapers.start(); // 再按视频模式挂接
    pump(2500); // 等解码出稳定画面
    const bool started = wallpapers.state() != RunState::Stopped && wallpapers.windowCount() > 0; // 窗口已挂上
    record(started ? QStringLiteral("PASS") : QStringLiteral("FAIL"), // 通过或失败
           started ? QStringLiteral("视频模式壁纸窗口已挂接") : wallpapers.lastError()); // 失败时带原因
    if (!started) { // 没有窗口就无法继续
        QFile report(reportPath); // 写出已有结果
        if (report.open(QIODevice::WriteOnly | QIODevice::Truncate)) { // 覆盖
            report.write(lines.join(QLatin1Char('\n')).toUtf8()); // 报告正文
        } // 结束写报告
        return 1; // 还原对象会停止并恢复设置
    } // 结束没挂上

    HWND wallpaperHwnd = reinterpret_cast<HWND>(wallpapers.windowAt(0)->winId()); // 第一块视频壁纸
    const HWND parent = GetParent(wallpaperHwnd); // 实际父窗口
    const HWND expectedParent = wallpapers.host().mode == DesktopHostMode::ProgmanLayeredChild ? wallpapers.host().progman // 分层模式父级是 Progman
                                                                                                : wallpapers.host().wallpaperHost; // 否则是壁纸宿主
    const bool parentOk = parent == expectedParent && wallpapers.host().mode != DesktopHostMode::None; // 父级正确且宿主有效
    record(parentOk ? QStringLiteral("PASS") : QStringLiteral("FAIL"), // 父级检查
           QStringLiteral("视频窗口父级是 WorkerW 背景层 parent=%1 host=%2") // 说明文字保持原文
               .arg(reinterpret_cast<quintptr>(parent), 0, 16) // 实际父级
               .arg(reinterpret_cast<quintptr>(wallpapers.host().wallpaperHost), 0, 16)); // 壁纸宿主
    const LONG ex = GetWindowLongW(wallpaperHwnd, GWL_EXSTYLE); // 扩展样式
    record((ex & WS_EX_TOPMOST) == 0 ? QStringLiteral("PASS") : QStringLiteral("FAIL"), QStringLiteral("视频壁纸不是置顶窗口")); // 不能置顶

    QImage framebuffer; // 壁纸自己的帧缓冲
    ColorHits hits; // 图案计数
    for (int i = 0; i < 12 && (hits.magenta < 5 || hits.yellow < 5); ++i) { // 最多等 12 轮
        pump(400); // 给解码器时间
        framebuffer = wallpapers.windowAt(0)->view()->surface()->capture(); // 从共享播放器画进的表面抓取
        hits = scanPattern(framebuffer); // 重新计数
    } // 结束等待图案
    const QString framePath = QFileInfo(reportPath).absolutePath() + QStringLiteral("/video-wallpaper-frame.png"); // 帧图路径
    framebuffer.save(framePath); // 留给人工查看
    record(hits.magenta > 5 && hits.yellow > 5 ? QStringLiteral("PASS") : QStringLiteral("FAIL"), // 两种颜色都要出现
           QStringLiteral("WorkerW 子窗口帧缓冲含测试图案 magenta=%1 yellow=%2 size=%3x%4 file=%5") // 计数和尺寸
               .arg(hits.magenta) // 洋红
               .arg(hits.yellow) // 黄
               .arg(framebuffer.width()) // 宽
               .arg(framebuffer.height()) // 高
               .arg(framePath)); // 文件
    lines << QStringLiteral("NOTE 红条 top=%1 bottom=%2。top 更大表示画面方向与图案一致。").arg(hits.redTop).arg(hits.redBottom); // 方向备注
    lines << QStringLiteral("NOTE 播放器 %1").arg(video->statusText()); // 唯一播放器的状态
    lines << QStringLiteral("NOTE %1").arg(WorkerWHost::describe(wallpapers.host())); // 宿主说明

    auto* widget = new QVideoWidget(wallpapers.windowAt(0)); // 对照控件，不是壁纸用的那一个播放器
    widget->setGeometry(wallpapers.windowAt(0)->rect()); // 盖住壁纸客户区
    widget->show(); // 显示对照控件
    QAudioOutput widgetAudio; // 对照播放器自己的音频
    widgetAudio.setMuted(true); // 静音
    widgetAudio.setVolume(0.0f); // 音量也设为 0
    QMediaPlayer widgetPlayer; // 只为 PrintWindow 对照而存在
    widgetPlayer.setAudioOutput(&widgetAudio); // 接上静音输出
    widgetPlayer.setVideoOutput(widget); // 画到对照控件
    widgetPlayer.setLoops(QMediaPlayer::Infinite); // 循环
    widgetPlayer.setSource(QUrl::fromLocalFile(videoPath)); // 同一段测试视频
    widgetPlayer.play(); // 开始对照播放
    pump(1800); // 等控件出画
    const QImage widgetImage = printWindowImage(reinterpret_cast<HWND>(widget->winId())); // 抓控件画面
    const ColorHits widgetHits = scanPattern(widgetImage); // 统计对照颜色
    const QString widgetPath = QFileInfo(reportPath).absolutePath() + QStringLiteral("/video-widget-print.png"); // 对照图路径
    if (!widgetImage.isNull()) { // 抓到了才保存
        widgetImage.save(widgetPath); // 写盘
    } // 结束保存对照图
    const bool widgetVisible = widgetHits.magenta > 5 && widgetHits.yellow > 5; // 对照图也要两种颜色
    record(widgetVisible ? QStringLiteral("PASS") : QStringLiteral("FAIL"), // 控件是否看得见图案
           QStringLiteral("QVideoWidget PrintWindow magenta=%1 yellow=%2 size=%3x%4 error=%5") // 计数和播放器错误
               .arg(widgetHits.magenta) // 洋红
               .arg(widgetHits.yellow) // 黄
               .arg(widgetImage.width()) // 宽
               .arg(widgetImage.height()) // 高
               .arg(widgetPlayer.errorString().isEmpty() ? QStringLiteral("无") : widgetPlayer.errorString())); // 没有错误就写无
    widgetPlayer.stop(); // 停掉对照播放器
    delete widget; // 销毁对照控件，壁纸仍用原来的 VideoPlayer
    pump(200); // 等控件销毁

    wallpapers.pauseByUser(); // 用户暂停应停掉唯一播放器
    pump(250); // 等暂停传到解码器
    const quint64 framesBefore = wallpapers.windowAt(0)->view()->surface()->frameCount(); // 暂停后的帧号
    pump(700); // 再等一会儿
    const quint64 framesAfter = wallpapers.windowAt(0)->view()->surface()->frameCount(); // 帧号应不变
    record(!video->isPlaying() && framesAfter == framesBefore ? QStringLiteral("PASS") : QStringLiteral("FAIL"), // 解码停止且不再出帧
           QStringLiteral("用户暂停后解码停止且帧数不变 playing=%1 before=%2 after=%3") // 状态和帧号
               .arg(video->isPlaying() ? QStringLiteral("yes") : QStringLiteral("no")) // 是否还在播
               .arg(framesBefore) // 暂停后
               .arg(framesAfter)); // 再等之后
    wallpapers.resumeByUser(); // 只取消用户暂停
    pump(1200); // 等重新解码
    record(video->isPlaying() ? QStringLiteral("PASS") : QStringLiteral("FAIL"), QStringLiteral("恢复后继续播放")); // 同一个播放器应继续

    const QString missingPath = QStringLiteral("D:/OceanLive/missing-ocean-video.mp4"); // 故意不存在的文件
    AppSettings missing = settings->current(); // 当前视频设置
    missing.ocean.videoPath = missingPath; // 换成缺失路径
    settings->apply(missing); // 让唯一播放器去打开它
    pump(400); // 等错误文本出现
    const QString expectedMissing = QCoreApplication::translate("VideoPlayer", "找不到视频文件：%1").arg(missingPath); // 必须与播放器同一上下文和原文比较
    record(video->lastError() == expectedMissing ? QStringLiteral("PASS") : QStringLiteral("FAIL"), // 错误文本要完全一致
           QStringLiteral("缺失文件提示：%1").arg(video->lastError().isEmpty() ? QStringLiteral("无") : video->lastError())); // 实际提示

    const double memoryBeforeStop = workingSetMb(); // 停止前的工作集
    wallpapers.stop(); // 停止壁纸并释放播放画面
    pump(300); // 等释放完成
    record(wallpapers.state() == RunState::Stopped && !video->isPlaying() && video->currentFrame().isNull() // 状态、播放和帧都要清掉
               ? QStringLiteral("PASS") // 通过
               : QStringLiteral("FAIL"), // 失败
           QStringLiteral("停止后释放播放画面 playing=%1 frame=%2") // 播放和帧状态
               .arg(video->isPlaying() ? QStringLiteral("yes") : QStringLiteral("no")) // 是否还在播
               .arg(video->currentFrame().isNull() ? QStringLiteral("empty") : QStringLiteral("kept"))); // 帧是否丢掉
    lines << QStringLiteral("NOTE 停止前工作集 %1 MB").arg(memoryBeforeStop, 0, 'f', 1); // 停止前
    lines << QStringLiteral("NOTE 停止后工作集 %1 MB").arg(workingSetMb(), 0, 'f', 1); // 停止后

    QFile report(reportPath); // 最终报告
    if (report.open(QIODevice::WriteOnly | QIODevice::Truncate)) { // 覆盖
        report.write(lines.join(QLatin1Char('\n')).toUtf8()); // 正文
        report.write("\n"); // 结尾换行
    } // 结束写报告
    return failures == 0 ? 0 : 1; // 还原对象还会再 stop 一次并恢复设置
} // 结束视频壁纸自测
