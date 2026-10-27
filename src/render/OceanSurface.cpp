// SPDX-License-Identifier: Apache-2.0
#include "render/OceanSurface.h" // 海面窗口声明

#include "render/ShaderManager.h" // 海面和视频两套程序
#include "utils/Logger.h" // 记录上下文和初始化失败

#include <QCoreApplication> // 驱动字符串缺失时走 translate
#include <QEvent> // 判断平台表面事件
#include <QImage> // 视频帧和截图
#include <QMatrix4x4> // 视图、投影和求逆
#include <QPlatformSurfaceEvent> // 表面即将销毁
#include <QTimer> // 按目标帧率触发
#include <QVector2D> // 补丁尺寸、分辨率和采样范围
#include <QVector3D> // 相机位置和颜色

#include <algorithm> // clamp、min、max
#include <cmath> // 把逻辑尺寸四舍五入成像素
#include <vector> // 网格数据和读回缓冲

#ifndef NOMINMAX // 防止和 std::min、std::max 撞宏
#define NOMINMAX // 不让 windows.h 定义 min/max
#endif // 结束 NOMINMAX 保护
#define WIN32_LEAN_AND_MEAN // 少包含 Win32 声明，减少宏
#include <Windows.h> // 取客户区像素尺寸

namespace { // 相机、补丁和 NVX 枚举只在本文件用

constexpr float kCameraHeight = 5.2f; // 相机高度
constexpr float kCameraZ = 0.0f; // 相机的 Z 位置
constexpr float kLookX = 0.0f; // 注视点水平居中
constexpr float kLookY = 1.05f; // 注视点略高于海面
constexpr float kLookZ = 46.0f; // 注视点沿正 Z
constexpr float kFov = 52.0f; // 垂直视场角，单位度
constexpr float kPatchWidth = 480.0f; // 海面补丁宽度
constexpr float kPatchLength = 560.0f; // 海面补丁长度
constexpr int kGpuMemoryTotalNvx = 0x9047; // NVX 整卡专用显存总量，单位 KB
constexpr int kGpuMemoryAvailableNvx = 0x9049; // NVX 整卡当前剩余显存，不是本进程

QString glString(GLenum name) { // 把 GL 字符串收成 QString
    const auto* text = glGetString(name); // 上下文未就绪时可能是空
    return text ? QString::fromLatin1(reinterpret_cast<const char*>(text)) // 驱动文本按 Latin-1
                : QCoreApplication::translate("OceanSurface", "不可用"); // 没有字符串时的可翻译占位
} // 结束读取 GL 字符串

}  // 结束匿名命名空间

OceanSurface::OceanSurface() : QOpenGLWindow(QOpenGLWindow::NoPartialUpdate) { // 整窗重绘，不做局部更新
    QSurfaceFormat format = QSurfaceFormat::defaultFormat(); // 在全局默认上改
    format.setVersion(3, 3); // 只要 OpenGL 3.3
    format.setProfile(QSurfaceFormat::CoreProfile); // 核心剖面，不带兼容入口
    format.setDepthBufferSize(24); // 海面深度测试用 24 位
    format.setStencilBufferSize(0); // 不用模板
    format.setSwapInterval(1); // 交换间隔 1，对齐刷新
    format.setSwapBehavior(QSurfaceFormat::DoubleBuffer); // 双缓冲
    format.setRenderableType(QSurfaceFormat::OpenGL); // 桌面 OpenGL，不是 GLES
    setFormat(format); // 显示之前定下格式

    m_params = OceanParams::clamped(OceanParams{}); // 默认参数夹进合法范围
    m_palette = OceanParams::paletteFor(m_params.theme); // 当前主题的颜色
    m_lerpFrom = m_palette; // 过渡起点
    m_lerpTo = m_palette; // 终点相同，先不混合
    m_paletteReady = true; // 之后换主题才做过渡

    m_timer = new QTimer(this); // 跟窗口一起销毁
    m_timer->setTimerType(Qt::PreciseTimer); // 尽量按间隔醒来
    m_timer->setInterval(1000 / m_params.targetFps); // 30 或 60 对应的毫秒
    connect(m_timer, &QTimer::timeout, this, &OceanSurface::onTick); // 到点再画
    m_clock.start(); // 墙钟开始走
    m_lastNsecs = 0; // 第一帧还没有间隔
} // 结束构造

OceanSurface::~OceanSurface() { // 停表，并在上下文还在时释放 GL
    m_timer->stop(); // 析构过程中不再进入 onTick
    if (context() && context()->makeCurrent(this)) { // 删除对象必须在所属上下文
        destroyGl(); // 查询、缓冲、纹理和着色器
        context()->doneCurrent(); // 用完放开
    } // 结束上下文仍可用
} // 结束析构

void OceanSurface::setParams(const OceanParams& params) { // 更新参数，换主题时混合颜色
    const OceanParams next = OceanParams::clamped(params); // 先夹进合法范围
    const bool themeChanged = m_paletteReady && next.theme != m_params.theme; // 已有颜色且主题变了
    const bool qualityChanged = next.quality != m_params.quality; // 画质变了要换网格
    if (!m_paletteReady || !themeChanged) { // 第一次，或主题没变
        if (!themeChanged && m_lerp >= 1.0f) { // 过渡中的颜色不要被盖掉
            m_palette = OceanParams::paletteFor(next.theme); // 写回该主题的默认色
            m_lerpFrom = m_palette; // 起点对齐
            m_lerpTo = m_palette; // 终点对齐
            m_lerp = 1.0f; // 保持过渡完成
        } // 结束写回主题色
    } // 结束非切换分支
    if (themeChanged) { // 主题切换
        m_lerpFrom = m_palette; // 从当前显示色出发
        m_lerpTo = OceanParams::paletteFor(next.theme); // 新主题的颜色
        m_lerp = 0.0f; // 重新开始混合
    } // 结束主题切换
    m_paletteReady = true; // 之后的主题差才会触发过渡
    m_params = next; // 保存夹紧后的参数
    m_timer->setInterval(std::max(1, 1000 / m_params.targetFps)); // 至少 1 毫秒，避免间隔为 0
    if (qualityChanged) { // 网格密度跟画质走
        m_meshDirty = true; // 下次绘制再重建，这里不一定有上下文
    } // 结束画质变化
} // 结束更新参数

void OceanSurface::setPaused(bool paused) { // 暂停动画，窗口仍在
    m_paused = paused; // 绘制时也不再推进时间
    if (paused) { // 暂停
        m_timer->stop(); // 不再触发 onTick
        return; // 停在当前画面
    } // 结束暂停
    if (!m_timer->isActive()) { // 避免重复启动
        m_timer->start(); // 按当前间隔继续
    } // 结束恢复
} // 结束暂停开关

void OceanSurface::setUnderDesktop(bool underDesktop) { // 嵌进桌面后改由定时器直接交换
    m_underDesktop = underDesktop; // paintGL 见到它就返回
    if (!m_paused && !m_timer->isActive()) { // 暂停时不要偷偷启动
        m_timer->start(); // 桌面路径靠这个定时器画
    } // 结束补启动
} // 结束桌面开关

void OceanSurface::setVideoFrame(const QImage& frame) { // 记下新帧，绘制时再上传
    m_videoFrame = frame; // 保留这一帧
    m_videoDirty = true; // 纹理还是上一帧
    if (m_params.renderMode == RenderMode::Video && !m_paused && !m_underDesktop && isExposed()) { // 可见的实时视频
        requestUpdate(); // 不等定时器，尽快贴上
    } // 结束提前请求
} // 结束接收视频帧

void OceanSurface::setFixedTime(double seconds) { // 只给定时导出，不请求重绘
    m_useFixedTime = true; // 之后动画秒不再跟随墙钟
    m_time = seconds; // 着色器时间停在这一秒
} // 结束固定时间

quint64 OceanSurface::frameCount() const { // 已绘制的帧数
    return m_frameSerial; // 通过资源和尺寸检查后才增加
} // 结束帧数

bool OceanSurface::timerActive() const { // 动画定时器是否在跑
    return m_timer && m_timer->isActive(); // 没有定时器也算没在跑
} // 结束定时器状态

RenderStats OceanSurface::stats() const { // 最近一次发布的统计
    return m_stats; // 按值返回
} // 结束取统计

bool OceanSurface::isReady() const { // 资源建成并且已经画出一帧
    return m_glReady && m_stats.ready; // 两个标志都要为真
} // 结束就绪查询

QImage OceanSurface::capture() { // 先画再读像素，给预览导出
    if (!context() && !handle()) { // 还没有原生窗口
        create(); // 先建表面才能有上下文
    } // 结束补建窗口
    if (!context() || !context()->makeCurrent(this)) { // 读像素必须在本上下文
        return {}; // 失败返回空图
    } // 结束无法激活
    if (!m_glReady && !initResources()) { // 第一次截图要先建资源
        context()->doneCurrent(); // 失败也要放开上下文
        return {}; // 没有画面
    } // 结束资源未就绪
    const QSize size = framebufferSize(); // 用像素尺寸
    paintOcean(size.width(), size.height()); // 先画进后缓冲
    std::vector<unsigned char> buffer(static_cast<size_t>(size.width()) * static_cast<size_t>(size.height()) * 4u); // 紧排 RGBA
    glPixelStorei(GL_PACK_ALIGNMENT, 1); // 行不做额外对齐，匹配下面的步长
    glReadPixels(0, 0, size.width(), size.height(), GL_RGBA, GL_UNSIGNED_BYTE, buffer.data()); // 读刚画完的后缓冲
    QImage image(buffer.data(), size.width(), size.height(), size.width() * 4, QImage::Format_RGBA8888); // 只引用缓冲
    const QImage copy = image.mirrored(false, true).copy(); // 翻成左上角原点，并脱离缓冲
    context()->swapBuffers(this); // 读完才交换，避免读到别的帧
    context()->doneCurrent(); // 放开上下文
    return copy; // 调用方拿到独立图像
} // 结束截图

bool OceanSurface::event(QEvent* event) { // 表面销毁前释放本上下文的 GL
    if (event->type() == QEvent::PlatformSurface) { // 平台表面通知
        auto* surfaceEvent = static_cast<QPlatformSurfaceEvent*>(event); // 具体的表面事件
        if (surfaceEvent->surfaceEventType() == QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed && m_glReady) { // 销毁前释放
            if (context() && context()->makeCurrent(this)) { // 删除调用必须在该上下文
                destroyGl(); // 删查询、缓冲、纹理和着色器
                context()->doneCurrent(); // 用完放开
            } else { // 上下文已经激活不了
                m_glReady = false; // 对象随表面消失，只标不可用
            } // 结束无法激活
        } // 结束即将销毁
    } // 结束平台表面
    return QOpenGLWindow::event(event); // 其余仍交给 Qt
} // 结束事件

void OceanSurface::initializeGL() { // 上下文就绪后建资源
    if (!initResources() && !m_failedEmitted) { // 失败只通知一次
        m_failedEmitted = true; // 挡住重复的失败信号
        Logger::error(QStringLiteral("OceanSurface"), m_stats.error); // 记下已有的失败原因
        emit renderFailed(m_stats.error); // 告诉界面没有启动
    } // 结束失败通知
} // 结束初始化回调

void OceanSurface::paintGL() { // Qt 的重绘入口
    if (m_underDesktop) { // 嵌在桌面时不走这里
        return; // 改由 forceFrame 画并交换
    } // 结束跳过桌面
    const QSize size = framebufferSize(); // 像素尺寸
    paintOcean(size.width(), size.height()); // 画海面或视频
} // 结束 Qt 重绘

void OceanSurface::onTick() { // 定时器推进一步
    if (m_paused) { // 停表后仍可能有迟到的超时
        return; // 暂停时不画
    } // 结束暂停
    if (m_underDesktop) { // 桌面子窗口不靠请求更新
        forceFrame(); // 自己激活、绘制、交换
        return; // 不再交给 paintGL
    } // 结束桌面节拍
    if (isExposed()) { // 窗口曝光
        requestUpdate(); // 让 Qt 合并重绘
    } else { // 没曝光也要出一帧
        forceFrame(); // 直接画并交换
    } // 结束曝光分支
} // 结束节拍

void OceanSurface::forceFrame() { // 不经 paintGL，直接画一帧并交换
    if (!context() && !handle()) { // 还没有原生窗口
        create(); // 先建表面
    } // 结束补建窗口
    QOpenGLContext* ctx = context(); // 可能仍是空
    if (!ctx || !ctx->makeCurrent(this)) { // 没有当前上下文就不能调 GL
        if (m_stats.error.isEmpty()) { // 不覆盖更具体的错误
            m_stats.error = tr("无法激活 OpenGL 上下文。"); // 可翻译的失败原因
        } // 结束填写错误
        return; // 这一帧画不了
    } // 结束无法激活
    if (!m_glReady && !initResources()) { // 资源还没建好
        ctx->doneCurrent(); // 失败也放开上下文
        if (!m_failedEmitted) { // 失败信号只发一次
            m_failedEmitted = true; // 记下已通知
            Logger::error(QStringLiteral("OceanSurface"), m_stats.error); // 记下失败原因
            emit renderFailed(m_stats.error); // 通知界面
        } // 结束失败通知
        return; // 不交换这一帧
    } // 结束资源失败
    const QSize size = framebufferSize(); // 像素尺寸
    paintOcean(size.width(), size.height()); // 画进后缓冲
    ctx->swapBuffers(this); // 换到前台
    ctx->doneCurrent(); // 放开上下文
} // 结束强制一帧

bool OceanSurface::initResources() { // 编译两套着色器并建网格，上下文须已当前
    if (m_glReady) { // 已经成功过
        return true; // 不重复创建
    } // 结束已就绪
    initializeOpenGLFunctions(); // 解析这个上下文的 OpenGL 3.3 入口
    const QSurfaceFormat format = context() ? context()->format() : this->format(); // 优先用实际上下文
    m_stats.vendor = glString(GL_VENDOR); // 厂商
    m_stats.renderer = glString(GL_RENDERER); // 显卡名
    m_stats.version = glString(GL_VERSION); // 驱动版本串
    int major = format.majorVersion(); // 上下文主版本
    int minor = format.minorVersion(); // 上下文次版本
    if (major <= 0) { // 格式对象里还没有版本
        major = m_stats.version.section(QLatin1Char('.'), 0, 0).toInt(); // 从版本串取主号
        minor = m_stats.version.section(QLatin1Char('.'), 1, 1).toInt(); // 从版本串取次号
    } // 结束补版本号
    Logger::info(QStringLiteral("OceanSurface"), // 记下驱动和上下文
                 QStringLiteral("OpenGL %1 / %2 / %3，上下文 %4.%5") // 版本串、厂商、渲染器、主次版本
                     .arg(m_stats.version, m_stats.vendor, m_stats.renderer) // 三个字符串
                     .arg(major) // 主版本
                     .arg(minor)); // 次版本
    if (major < 3 || (major == 3 && minor < 3)) { // 低于 OpenGL 3.3 不能跑这套着色器
        m_stats.error = tr("显卡上下文是 %1.%2，OceanLive 需要 OpenGL 3.3 Core。动态壁纸没有启动。") // 可翻译的拒绝原因
                            .arg(major) // 填主版本
                            .arg(minor); // 填次版本
        m_stats.ready = false; // 统计标记未就绪
        return false; // 不创建资源
    } // 结束版本不足

    if (m_shaders) { // 上次失败可能留下海面程序
        m_shaders->release(); // 卸掉 GL 程序
        delete m_shaders; // 释放封装
        m_shaders = nullptr; // 避免悬空
    } // 结束清理海面程序
    m_shaders = new ShaderManager(); // 海面着色器
    QString error; // 加载失败时的原因
    if (!m_shaders->load(QStringLiteral(":/shaders/ocean.vert"), QStringLiteral(":/shaders/ocean.frag"), &error)) { // 海面程序
        m_stats.error = error; // 交给界面显示
        m_stats.ready = false; // 未就绪
        delete m_shaders; // 失败的程序不留
        m_shaders = nullptr; // 清空
        return false; // 视频程序也不建
    } // 结束海面程序失败
    if (m_videoShaders) { // 上次可能留下视频程序
        m_videoShaders->release(); // 卸掉
        delete m_videoShaders; // 释放
        m_videoShaders = nullptr; // 清空
    } // 结束清理视频程序
    if (m_videoTex) { // 上次的视频纹理
        glDeleteTextures(1, &m_videoTex); // 删掉名字
        m_videoTex = 0; // 清空
    } // 结束清理纹理
    m_videoShaders = new ShaderManager(); // 视频着色器
    if (!m_videoShaders->load(QStringLiteral(":/shaders/video.vert"), QStringLiteral(":/shaders/video.frag"), &error)) { // 视频程序
        m_stats.error = error; // 交给界面显示
        m_stats.ready = false; // 未就绪
        delete m_videoShaders; // 失败的程序不留
        m_videoShaders = nullptr; // 清空
        return false; // 初始化失败
    } // 结束视频程序失败
    glGenTextures(1, &m_videoTex); // 一张二维纹理
    glBindTexture(GL_TEXTURE_2D, m_videoTex); // 后面的参数打在这张上
    const unsigned char black[4] = {0, 0, 0, 255}; // 第一帧到来前是不透明黑
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, black); // 1x1，内部格式 RGBA8
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); // 缩小时线性过滤
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); // 放大时线性过滤
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); // 横向钳到边缘
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE); // 纵向钳到边缘
    glBindTexture(GL_TEXTURE_2D, 0); // 解绑，避免后面误改
    m_videoWidth = 1; // 和占位纹理一致
    m_videoHeight = 1; // 高度同样是 1
    m_nvMemory = context() && context()->hasExtension(QByteArrayLiteral("GL_NVX_gpu_memory_info")); // 有扩展才读整卡显存
    rebuildMesh(); // 按当前画质建网格
    if (m_oceanVao == 0) { // 海面顶点数组没建出来
        m_stats.error = tr("无法创建海洋网格。"); // 可翻译的失败原因
        return false; // 不算初始化成功
    } // 结束网格失败
    glGenQueries(1, &m_query); // 一个时间查询，只计量本上下文
    m_glReady = true; // 资源可以绘制
    m_stats.error.clear(); // 清掉旧错误
    Logger::info(QStringLiteral("OceanSurface"), QStringLiteral("海洋渲染器已初始化")); // 资源已经建好
    return true; // 统计里的 ready 等第一帧再置上
} // 结束建资源

void OceanSurface::destroyGl() { // 删除本上下文的 GL 对象，调用时上下文必须当前
    if (m_query) { // 有时间查询
        glDeleteQueries(1, &m_query); // 删掉查询
        m_query = 0; // 名字作废
        m_queryInFlight = false; // 没有在途结果
    } // 结束查询
    if (m_oceanVao) { // 有海面网格
        glDeleteVertexArrays(1, &m_oceanVao); // 顶点数组
        glDeleteBuffers(1, &m_oceanVbo); // 顶点缓冲
        glDeleteBuffers(1, &m_oceanIbo); // 索引缓冲
        m_oceanVao = m_oceanVbo = m_oceanIbo = 0; // 三个名字一起清掉
    } // 结束海面网格
    if (m_skyVao) { // 有天空三角形
        glDeleteVertexArrays(1, &m_skyVao); // 顶点数组
        glDeleteBuffers(1, &m_skyVbo); // 顶点缓冲
        m_skyVao = m_skyVbo = 0; // 两个名字清掉
    } // 结束天空
    if (m_videoTex) { // 有视频纹理
        glDeleteTextures(1, &m_videoTex); // 删纹理
        m_videoTex = 0; // 名字作废
        m_videoWidth = 0; // 宽度作废
        m_videoHeight = 0; // 下次上传会重新分配
    } // 结束视频纹理
    if (m_shaders) { // 海面程序
        m_shaders->release(); // 卸掉 GL 程序
        delete m_shaders; // 释放封装
        m_shaders = nullptr; // 清空
    } // 结束海面程序
    if (m_videoShaders) { // 视频程序
        m_videoShaders->release(); // 卸掉 GL 程序
        delete m_videoShaders; // 释放封装
        m_videoShaders = nullptr; // 清空
    } // 结束视频程序
    m_glReady = false; // 需要重新初始化
    m_stats.ready = false; // 统计也不再算就绪
    m_readyEmitted = false; // 恢复成功后可以再发就绪
} // 结束释放

void OceanSurface::rebuildMesh() { // 按画质重建海面，天空三角形只建一次
    int columns = 0; // 列数由画质填入
    int rows = 0; // 行数由画质填入
    OceanParams::meshSizeForQuality(m_params.quality, &columns, &rows); // 低中高密度不同
    m_columns = columns; // 记下列
    m_rows = rows; // 记下行

    if (m_oceanVao) { // 已有海面就先拆掉
        glDeleteVertexArrays(1, &m_oceanVao); // 顶点数组
        glDeleteBuffers(1, &m_oceanVbo); // 顶点缓冲
        glDeleteBuffers(1, &m_oceanIbo); // 索引缓冲
        m_oceanVao = m_oceanVbo = m_oceanIbo = 0; // 清掉名字再重建
    } // 结束拆除旧网格
    if (!m_skyVao) { // 天空不随画质变化
        const float sky[] = {-1.0f, -1.0f, 1.0f, 3.0f, -1.0f, 1.0f, -1.0f, 3.0f, 1.0f}; // 盖住裁剪空间，1 表示天空
        glGenVertexArrays(1, &m_skyVao); // 天空顶点数组
        glGenBuffers(1, &m_skyVbo); // 天空顶点缓冲
        glBindVertexArray(m_skyVao); // 属性记到这个数组上
        glBindBuffer(GL_ARRAY_BUFFER, m_skyVbo); // 绑定顶点
        glBufferData(GL_ARRAY_BUFFER, sizeof(sky), sky, GL_STATIC_DRAW); // 三角形不再改
        glEnableVertexAttribArray(0); // 位置
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr); // 前两个浮点
        glEnableVertexAttribArray(1); // 种类
        glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 3 * sizeof(float), reinterpret_cast<void*>(2 * sizeof(float))); // 第三分量
        glBindVertexArray(0); // 解绑
    } // 结束天空三角形

    std::vector<float> vertices; // 每顶点是坐标和种类
    vertices.reserve(static_cast<size_t>(columns * rows * 3)); // 三个浮点
    for (int y = 0; y < rows; ++y) { // 行，着色器里较小的更靠近相机
        for (int x = 0; x < columns; ++x) { // 列，沿海面横向铺开
            vertices.push_back(static_cast<float>(x) / static_cast<float>(columns - 1)); // 横向归一化到 0 到 1
            vertices.push_back(static_cast<float>(y) / static_cast<float>(rows - 1)); // 纵向归一化到 0 到 1
            vertices.push_back(0.0f); // 0 表示海面，天空才用 1
        } // 结束一列
    } // 结束一行
    std::vector<unsigned> indices; // 三角形索引
    indices.reserve(static_cast<size_t>((columns - 1) * (rows - 1) * 6)); // 每格六个索引
    for (int y = 0; y < rows - 1; ++y) { // 最后一行没有下一行
        for (int x = 0; x < columns - 1; ++x) { // 最后一列没有右邻
            const unsigned i = static_cast<unsigned>(y * columns + x); // 格子左上角
            indices.push_back(i); // 第一三角形的左上
            indices.push_back(i + static_cast<unsigned>(columns)); // 第一三角形的左下
            indices.push_back(i + 1); // 第一三角形的右上
            indices.push_back(i + 1); // 第二三角形的右上
            indices.push_back(i + static_cast<unsigned>(columns)); // 第二三角形的左下
            indices.push_back(i + static_cast<unsigned>(columns) + 1); // 第二三角形的右下
        } // 结束一格
    } // 结束格子行
    m_indexCount = static_cast<int>(indices.size()); // 绘制用的索引个数

    glGenVertexArrays(1, &m_oceanVao); // 海面顶点数组
    glGenBuffers(1, &m_oceanVbo); // 顶点缓冲
    glGenBuffers(1, &m_oceanIbo); // 索引缓冲
    glBindVertexArray(m_oceanVao); // 属性记到海面上
    glBindBuffer(GL_ARRAY_BUFFER, m_oceanVbo); // 绑定顶点
    // 海面顶点不再每帧修改
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(float)), vertices.data(), GL_STATIC_DRAW); // 静态
    glEnableVertexAttribArray(0); // 网格坐标
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr); // 前两个浮点
    glEnableVertexAttribArray(1); // 种类
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 3 * sizeof(float), reinterpret_cast<void*>(2 * sizeof(float))); // 海面标志
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_oceanIbo); // 索引附在这个顶点数组上
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices.size() * sizeof(unsigned)), indices.data(), // 索引数据
                 GL_STATIC_DRAW); // 静态绘制
    glBindVertexArray(0); // 解绑
    m_meshDirty = false; // 网格已跟上画质
} // 结束重建网格

QSize OceanSurface::framebufferSize() const { // 帧缓冲像素尺寸
    if (const HWND hwnd = reinterpret_cast<HWND>(winId())) { // 有原生窗口就以 Win32 客户区为准
        RECT rect{}; // 客户区矩形
        if (GetClientRect(hwnd, &rect) && rect.right > 1 && rect.bottom > 1) { // 0 或 1 像素视为无效
            return QSize(rect.right, rect.bottom); // 客户区宽高就是像素
        } // 结束客户区可用
    } // 结束原生窗口
    const qreal ratio = std::max(1.0, devicePixelRatio()); // 比例至少为 1
    return QSize(std::max(1, static_cast<int>(std::lround(width() * ratio))), // 宽至少 1 像素
                 std::max(1, static_cast<int>(std::lround(height() * ratio)))); // 高至少 1 像素
} // 结束帧缓冲尺寸

void OceanSurface::paintOcean(int width, int height) { // 画一帧海面或视频
    if (!m_glReady || !m_shaders || !m_shaders->isLinked() || width < 2 || height < 2) { // 资源或尺寸不够
        return; // 不增加帧号
    } // 结束提前返回
    QElapsedTimer cpu; // 量本帧 CPU
    cpu.start(); // 从这里计到统计为止

    const qint64 nsecs = m_clock.nsecsElapsed(); // 墙钟纳秒
    double dt = 0.0; // 本帧秒数
    if (m_lastNsecs > 0) { // 第一帧没有上一拍
        dt = std::clamp(static_cast<double>(nsecs - m_lastNsecs) / 1.0e9, 0.0, 0.1); // 卡顿最多推进 0.1 秒
    } // 结束计算间隔
    m_lastNsecs = nsecs; // 留给下一帧
    if (!m_paused && !m_useFixedTime) { // 暂停和导出都不走墙钟
        m_time += dt; // 着色器里的动画秒
    } // 结束推进时间
    if (!m_paused && m_lerp < 1.0f) { // 主题颜色还在过渡
        m_lerp = std::min(1.0f, m_lerp + static_cast<float>(std::max(dt, 0.016)) * 1.7f); // 至少按 16 毫秒走
        m_palette = lerpPalette(m_lerpFrom, m_lerpTo, m_lerp); // 混出当前调色板
    } // 结束颜色过渡

    if (m_meshDirty) { // 画质变过
        rebuildMesh(); // 此时上下文是当前的
    } // 结束重建网格

    const bool timeQuery = m_query != 0 && !m_queryInFlight; // 按进入本帧时是否在途决定，取回结果的这一帧不再重开
    if (m_query && m_queryInFlight) { // 上一记还没取回
        GLuint available = 0; // 0 表示 GPU 还没写完
        glGetQueryObjectuiv(m_query, GL_QUERY_RESULT_AVAILABLE, &available); // 先问就绪，避免堵住 CPU
        if (available) { // 结果属于本上下文
            GLuint64 elapsed = 0; // 纳秒
            glGetQueryObjectui64v(m_query, GL_QUERY_RESULT, &elapsed); // OpenGL 3.3 的 64 位结果
            m_stats.gpuFrameMs = static_cast<double>(elapsed) / 1.0e6; // 纳秒换成毫秒
            m_stats.gpuValid = true; // 计时有效，只代表本上下文
            m_queryInFlight = false; // 查询对象可以再用
        } // 结束结果已就绪
    } // 结束取回 GPU 时间

    glViewport(0, 0, width, height); // 画满帧缓冲
    ++m_frameSerial; // 通过检查的绘制记一帧
    if (m_params.renderMode == RenderMode::Video) { // 视频模式不画海面
        if (timeQuery) { // 这一帧可以开始计时
            glBeginQuery(GL_TIME_ELAPSED, m_query); // 只计本上下文接下来的命令
        } // 结束开始计时
        paintVideo(width, height); // 清屏并贴当前帧
        if (timeQuery) { // 与 begin 成对
            glEndQuery(GL_TIME_ELAPSED); // 结束本上下文计时
            m_queryInFlight = true; // 结果留给后面的帧读取
        } // 结束停止计时
        m_stats.cpuFrameMs = static_cast<double>(cpu.nsecsElapsed()) / 1.0e6; // CPU 毫秒，含上传
        m_stats.ready = true; // 已经能出图
        m_fpsAccum += dt > 0.0 ? dt : 0.016; // 没有间隔时按 16 毫秒计
        ++m_fpsFrames; // 统计窗口内的帧
        if (m_fpsAccum >= 0.5) { // 满半秒发布一次
            m_stats.fps = static_cast<double>(m_fpsFrames) / m_fpsAccum; // 用动画间隔，不是 CPU 耗时
            m_fpsFrames = 0; // 帧数清零
            m_fpsAccum = 0.0; // 秒数清零
            queryGpuMemory(); // 顺带读整卡显存
            publishStats(); // 发出统计
        } // 结束半秒统计
        if (!m_readyEmitted) { // 就绪只发一次
            m_readyEmitted = true; // 挡住重复信号
            emit renderReady(); // 通知外面可以显示
        } // 结束就绪信号
        return; // 不再画实时海面
    } // 结束视频分支

    glEnable(GL_DEPTH_TEST); // 让海面盖住天空
    glDepthFunc(GL_LESS); // 更近的片段留下
    glDisable(GL_CULL_FACE); // 不依赖三角形绕序
    glDisable(GL_BLEND); // 直接覆盖，不和清屏色混合
    const OceanPalette look = displayedPalette(); // 主题色，可被自定义覆盖
    glClearColor(look.skyHorizon.x(), look.skyHorizon.y(), look.skyHorizon.z(), 1.0f); // 清成地平线颜色
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); // 颜色和深度，没有模板

    const QVector3D camera(0.0f, kCameraHeight, kCameraZ); // 固定机位
    QMatrix4x4 projection; // 透视矩阵
    projection.perspective(kFov, static_cast<float>(width) / static_cast<float>(height), 0.25f, 1400.0f); // 近 0.25，远 1400
    QMatrix4x4 view; // 视图矩阵
    view.lookAt(camera, QVector3D(kLookX, kLookY, kLookZ), QVector3D(0.0f, 1.0f, 0.0f)); // 看向海面前方，上方为 Y
    const QMatrix4x4 viewProj = projection * view; // 先视图再投影
    bool invertible = false; // 求逆是否成功
    const QMatrix4x4 inverse = viewProj.inverted(&invertible); // 着色器用来反投影

    if (timeQuery) { // 没有在途查询
        glBeginQuery(GL_TIME_ELAPSED, m_query); // 从绘制命令起计本上下文时间
    } // 结束开始计时

    QOpenGLShaderProgram& program = m_shaders->program(); // 海面程序
    program.bind(); // 使用它
    const QVector3D sunDir = worldSunDirection(view, m_params.sunAzimuth, m_params.sunElevation); // 变到世界空间
    program.setUniformValue("uViewProj", viewProj); // 视图投影
    program.setUniformValue("uInvViewProj", invertible ? inverse : viewProj); // 求逆失败就退回原矩阵
    program.setUniformValue("uTime", static_cast<float>(m_time)); // 动画秒，导出时是固定值
    program.setUniformValue("uWaveAmp", m_params.waveStrength * look.waveScale); // 浪高再乘主题尺度
    program.setUniformValue("uWind", m_params.windStrength); // 风力
    program.setUniformValue("uAnimSpeed", m_params.animSpeed * look.speedScale); // 速度再乘主题倍率
    program.setUniformValue("uPatch", QVector2D(kPatchWidth, kPatchLength)); // 补丁的宽和长
    program.setUniformValue("uWaveCount", OceanParams::waveCountForQuality(m_params.quality)); // 该画质叠加的层数
    program.setUniformValue("uWindAngle", m_params.windAngle * 0.01745329252f); // 风向从度换成弧度
    program.setUniformValue("uResolution", QVector2D(static_cast<float>(width), static_cast<float>(height))); // 帧缓冲像素
    program.setUniformValue("uCamPos", camera); // 相机世界坐标
    program.setUniformValue("uBrightness", m_params.brightness); // 整体亮度
    program.setUniformValue("uSpecular", m_params.sunReflection); // 海面高光
    program.setUniformValue("uSunIntensity", m_params.sunIntensity * look.sunGain); // 强度再乘主题增益
    program.setUniformValue("uHighlightWidth", m_params.highlightWidth); // 高光宽度
    program.setUniformValue("uQuality", m_params.quality); // 画质档
    program.setUniformValue("uSkyZenith", look.skyZenith); // 天顶色
    program.setUniformValue("uSkyHorizon", look.skyHorizon); // 地平线色
    program.setUniformValue("uWaterDeep", look.waterDeep); // 深水色
    program.setUniformValue("uWaterShallow", look.waterShallow); // 浅水色
    program.setUniformValue("uSunColor", look.sunColor); // 太阳色
    program.setUniformValue("uSunDir", sunDir); // 世界空间太阳方向
    program.setUniformValue("uFoamAmount", look.foam); // 泡沫量
    program.setUniformValue("uSkySaturation", m_params.skySaturation); // 天空饱和
    program.setUniformValue("uSkyBrightness", m_params.skyBrightness); // 天空亮度
    program.setUniformValue("uHaze", m_params.atmosphereHaze); // 大气薄雾
    program.setUniformValue("uSunDisk", m_params.sunDisk); // 日盘，只出现在天上
    program.setUniformValue("uSunHalo", m_params.sunHalo); // 日晕
    program.setUniformValue("uSunHaloRadius", m_params.sunHaloRadius); // 日晕半径
    program.setUniformValue("uHorizonHaze", m_params.horizonHaze); // 地平线薄雾

    glBindVertexArray(m_skyVao); // 天空三角形
    glDrawArrays(GL_TRIANGLES, 0, 3); // 三个顶点盖住屏幕
    glBindVertexArray(m_oceanVao); // 海面网格
    glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, nullptr); // 索引是 32 位
    glBindVertexArray(0); // 解绑网格
    program.release(); // 卸下海面程序

    if (timeQuery) { // 与 begin 成对
        glEndQuery(GL_TIME_ELAPSED); // 结束本上下文计时
        m_queryInFlight = true; // 结果以后再读
    } // 结束停止计时

    m_stats.cpuFrameMs = static_cast<double>(cpu.nsecsElapsed()) / 1.0e6; // CPU 毫秒，含网格重建
    m_stats.ready = true; // 已经能出图
    m_fpsAccum += dt > 0.0 ? dt : 0.016; // 没有间隔时按 16 毫秒计
    ++m_fpsFrames; // 统计窗口内的帧
    if (m_fpsAccum >= 0.5) { // 满半秒发布一次
        m_stats.fps = static_cast<double>(m_fpsFrames) / m_fpsAccum; // 用动画间隔，不是 CPU 耗时
        m_fpsFrames = 0; // 帧数清零
        m_fpsAccum = 0.0; // 秒数清零
        queryGpuMemory(); // 顺带读整卡显存
        publishStats(); // 发出统计
    } // 结束半秒统计
    if (!m_readyEmitted) { // 就绪只发一次
        m_readyEmitted = true; // 挡住重复信号
        emit renderReady(); // 通知外面可以显示
    } // 结束就绪信号
} // 结束绘制一帧

OceanPalette OceanSurface::displayedPalette() const { // 本帧实际使用的颜色
    OceanPalette look = m_palette; // 主题色或正在混合的颜色
    if (m_params.customWater) { // 用户改过海水
        look.waterShallow = m_params.waterColor; // 浅水用自定义色
        look.waterDeep = m_params.waterColor * 0.28f; // 深水同色相，亮度压到 0.28
    } // 结束自定义海水
    if (m_params.customSky) { // 用户改过天空
        look.skyHorizon = m_params.skyColor; // 地平线用自定义色
        look.skyZenith = QVector3D(m_params.skyColor.x() * 0.32f, m_params.skyColor.y() * 0.48f, // 天顶更暗
                                   std::min(1.0f, m_params.skyColor.z() * 0.72f + 0.12f)); // 蓝通道抬高并封顶为 1
    } // 结束自定义天空
    return look; // 交给清屏和着色器
} // 结束显示颜色

void OceanSurface::uploadVideo() { // 把脏帧送进纹理
    if (!m_videoDirty || m_videoTex == 0) { // 没有新帧，或纹理还没建
        return; // 不碰 GL
    } // 结束无需上传
    m_videoDirty = false; // 空帧也消费掉脏标记
    if (m_videoFrame.isNull()) { // 没有像素
        return; // 纹理保持原样
    } // 结束空帧
    QImage image = m_videoFrame; // 本地副本，转换不影响源
    if (image.format() != QImage::Format_RGBA8888) { // 上传按 RGBA 字节
        image = image.convertToFormat(QImage::Format_RGBA8888); // 转成四通道
    } // 结束转换格式
    glBindTexture(GL_TEXTURE_2D, m_videoTex); // 上传目标
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1); // 行首不额外对齐
    glPixelStorei(GL_UNPACK_ROW_LENGTH, image.bytesPerLine() / 4); // 行距按像素，含 QImage 填充
    if (image.width() != m_videoWidth || image.height() != m_videoHeight) { // 尺寸变了必须重新分配
        // 整张上传，内部格式 RGBA8
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, image.width(), image.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, image.constBits()); // 分配
        m_videoWidth = image.width(); // 记下新宽度
        m_videoHeight = image.height(); // 记下新高度
    } else { // 尺寸相同
        // 尺寸未变，不重新分配存储
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, image.width(), image.height(), GL_RGBA, GL_UNSIGNED_BYTE, image.constBits()); // 替换
    } // 结束上传
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0); // 复位，避免影响后续上传
    glBindTexture(GL_TEXTURE_2D, 0); // 解绑纹理
} // 结束上传视频

void OceanSurface::paintVideo(int width, int height) { // 把当前帧铺满窗口
    glDisable(GL_DEPTH_TEST); // 全屏三角形不参与深度
    glDisable(GL_BLEND); // 不透明覆盖
    const OceanPalette look = displayedPalette(); // 没有帧时用深水当底
    glClearColor(look.waterDeep.x(), look.waterDeep.y(), look.waterDeep.z(), 1.0f); // 清成深水色
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); // 颜色和深度一起清
    if (!m_videoShaders || !m_videoShaders->isLinked() || m_videoFrame.isNull() || m_videoTex == 0) { // 没程序或没帧
        return; // 只留下清屏色
    } // 结束不能贴图
    uploadVideo(); // 有脏帧才上传
    // 帧的宽除以高，高为 0 时按 1
    const float frameAspect = m_videoHeight > 0 ? static_cast<float>(m_videoWidth) / static_cast<float>(m_videoHeight) : 1.0f; // 比例
    const float screenAspect = height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 1.0f; // 窗口宽高比
    float spanX = 1.0f; // 横向采样跨度
    float spanY = 1.0f; // 纵向采样跨度
    if (frameAspect > screenAspect) { // 帧比窗口宽
        spanX = screenAspect / frameAspect; // 裁掉左右，铺满高度
    } else if (frameAspect > 0.0f) { // 帧更窄，或宽高比相同
        spanY = frameAspect / screenAspect; // 裁掉上下，铺满宽度
    } // 结束按比例裁切
    const QVector2D span(spanX, spanY); // 采样跨度
    const QVector2D offset((1.0f - spanX) * 0.5f, (1.0f - spanY) * 0.5f); // 从画面中心裁

    glActiveTexture(GL_TEXTURE0); // 采样器使用 0 号单元
    glBindTexture(GL_TEXTURE_2D, m_videoTex); // 当前帧
    QOpenGLShaderProgram& program = m_videoShaders->program(); // 视频程序
    program.bind(); // 使用它
    program.setUniformValue("uFrame", 0); // 纹理单元 0
    program.setUniformValue("uUvSpan", span); // 采样跨度
    program.setUniformValue("uUvOffset", offset); // 采样起点
    glBindVertexArray(m_skyVao); // 复用盖住屏幕的三角形
    glDrawArrays(GL_TRIANGLES, 0, 3); // 三个顶点
    glBindVertexArray(0); // 解绑
    program.release(); // 卸下程序
    glBindTexture(GL_TEXTURE_2D, 0); // 解绑纹理
} // 结束视频画面

void OceanSurface::queryGpuMemory() { // 读 NVX 整卡显存，没有就标未知
    if (!m_nvMemory) { // 本上下文没有这个扩展
        m_stats.gpuMemoryUsedMb = -1.0; // 未知不用 0，以免看成没占用
        m_stats.gpuMemoryTotalMb = -1.0; // 总量同样未知
        return; // 不调用扩展入口
    } // 结束无扩展
    GLint totalKb = 0; // 整卡专用显存
    GLint availableKb = 0; // 整卡当前剩余
    glGetIntegerv(kGpuMemoryTotalNvx, &totalKb); // 报告的是整张适配器
    glGetIntegerv(kGpuMemoryAvailableNvx, &availableKb); // 剩余也是整卡，不是本进程
    if (totalKb <= 0) { // 驱动没有给出总量
        m_stats.gpuMemoryUsedMb = -1.0; // 标成未知
        m_stats.gpuMemoryTotalMb = -1.0; // 总量未知
        return; // 不算已用
    } // 结束总量无效
    m_stats.gpuMemoryTotalMb = static_cast<double>(totalKb) / 1024.0; // KB 换成 MB
    m_stats.gpuMemoryUsedMb = static_cast<double>(std::max(0, totalKb - availableKb)) / 1024.0; // 已用不超过总量
} // 结束显存查询

void OceanSurface::publishStats() { // 把当前统计交出去
    emit statsChanged(m_stats); // 由绘制路径大约每半秒调用
} // 结束发布统计
