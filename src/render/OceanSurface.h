// SPDX-License-Identifier: Apache-2.0
#pragma once // 头文件只包含一次

#include "render/OceanParams.h" // 参数、调色板和统计里用到的类型

#include <QElapsedTimer> // 动画时间和帧率
#include <QMetaType> // 让统计能放进信号
#include <QImage> // 视频帧和截图
#include <QOpenGLFunctions_3_3_Core> // OpenGL 3.3 函数
#include <QOpenGLWindow> // 独立的 OpenGL 窗口，可嵌进桌面
#include <QString> // 驱动字符串和错误

struct RenderStats { // 一帧的诊断数字
    double fps = 0.0; // 最近统计窗口的帧率
    double cpuFrameMs = 0.0; // 本帧 CPU 耗时，含别的工作
    double gpuFrameMs = -1.0; // 本上下文的 GPU 计时，没有则为 -1
    double gpuMemoryUsedMb = -1.0; // 整张显卡已用显存，不是本程序独占
    double gpuMemoryTotalMb = -1.0; // 整张显卡专用显存
    bool gpuValid = false; // GPU 计时或显存至少有一项可用
    bool ready = false; // 着色器和网格已就绪
    QString vendor; // 厂商
    QString renderer; // 显卡名
    QString version; // OpenGL 版本串
    QString error; // 最近的渲染错误
}; // 结束统计

Q_DECLARE_METATYPE(RenderStats) // 信号可以携带统计

class OceanSurface : public QOpenGLWindow, protected QOpenGLFunctions_3_3_Core { // 海面和视频的绘制窗口
    Q_OBJECT // 统计、失败和就绪信号

public: // 参数、暂停、截图
    OceanSurface(); // 创建定时器，还没建 GL 资源
    ~OceanSurface() override; // 释放 GL 对象

    void setParams(const OceanParams& params); // 更新参数，主题变化时混合调色板
    void setPaused(bool paused); // 暂停动画但窗口还在
    void setUnderDesktop(bool underDesktop); // 嵌进桌面时换垂直同步策略
    void setVideoFrame(const QImage& frame); // 记下新帧，下一帧绘制再上传
    void setFixedTime(double seconds); // 只给预览导出用的固定时间
    RenderStats stats() const; // 最近一次发布的统计
    QImage capture(); // 先画再读像素
    bool isReady() const; // GL 是否初始化成功
    quint64 frameCount() const; // 已绘制帧数
    bool timerActive() const; // 动画定时器是否在跑

signals: // 向外报告
    void statsChanged(const RenderStats& stats); // 帧率或错误变了
    void renderFailed(const QString& message); // 上下文或着色器失败
    void renderReady(); // 可以出图

protected: // Qt 的 GL 回调
    void initializeGL() override; // 建着色器、网格和计时查询
    void paintGL() override; // 画海面或视频
    bool event(QEvent* event) override; // 更新请求时补画

private: // 网格、上传和统计
    void onTick(); // 定时器推进时间并请求重绘
    void forceFrame(); // 立刻画一帧，给截图和固定时间用
    bool initResources(); // 编译两套着色器并建缓冲
    void destroyGl(); // 删掉本上下文的 GL 对象
    void rebuildMesh(); // 按画质重建海面网格
    void paintOcean(int width, int height); // 画天空和海水
    void paintVideo(int width, int height); // 把视频帧画满窗口
    void uploadVideo(); // 脏帧上传到纹理
    void queryGpuMemory(); // 读 NVX 整卡显存，没有就跳过
    QSize framebufferSize() const; // 设备像素尺寸
    void publishStats(); // 约每秒发一次统计
    OceanPalette displayedPalette() const; // 主题过渡中的调色板

    class QTimer* m_timer = nullptr; // 按目标帧率触发
    class ShaderManager* m_shaders = nullptr; // 海面着色器
    class ShaderManager* m_videoShaders = nullptr; // 视频着色器
    OceanParams m_params; // 当前参数
    OceanPalette m_palette; // 正在显示的调色板
    OceanPalette m_lerpFrom; // 过渡起点
    OceanPalette m_lerpTo; // 过渡终点
    float m_lerp = 1.0f; // 1 表示过渡结束
    bool m_paletteReady = false; // 是否已有调色板
    bool m_glReady = false; // 资源是否可用
    bool m_paused = false; // 用户或全屏暂停
    bool m_underDesktop = false; // 是否嵌在桌面里
    bool m_failedEmitted = false; // 失败信号只发一次，直到恢复
    bool m_readyEmitted = false; // 就绪信号只发一次
    bool m_meshDirty = true; // 画质变了要重建网格
    int m_columns = 0; // 网格列数
    int m_rows = 0; // 网格行数
    int m_indexCount = 0; // 三角形索引数
    unsigned m_oceanVao = 0; // 海面顶点数组
    unsigned m_oceanVbo = 0; // 海面顶点
    unsigned m_oceanIbo = 0; // 海面索引
    unsigned m_skyVao = 0; // 天空全屏三角形
    unsigned m_skyVbo = 0; // 天空顶点
    unsigned m_query = 0; // GPU 时间查询
    unsigned m_videoTex = 0; // 视频纹理
    int m_videoWidth = 0; // 已上传纹理的宽
    int m_videoHeight = 0; // 已上传纹理的高
    bool m_queryInFlight = false; // 有一个查询还没取回
    bool m_nvMemory = false; // 驱动有 NVX 显存扩展
    bool m_useFixedTime = false; // 导出预览时不走墙钟
    bool m_videoDirty = false; // 有新帧还没上传
    QImage m_videoFrame; // 待上传的帧
    quint64 m_frameSerial = 0; // 绘制次数
    QElapsedTimer m_clock; // 墙钟
    qint64 m_lastNsecs = 0; // 上一帧的纳秒
    double m_time = 0.0; // 着色器里的动画秒
    double m_fpsAccum = 0.0; // 统计窗口内的秒数
    int m_fpsFrames = 0; // 统计窗口内的帧数
    RenderStats m_stats; // 最近发布的统计
}; // 结束海面窗口
