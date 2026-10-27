// SPDX-License-Identifier: Apache-2.0
#include "wallpaper/WallpaperManager.h" // 壁纸管理声明

#include "render/VideoPlayer.h" // 唯一播放器的停止、暂停和播放
#include "utils/Logger.h" // 启停和挂接日志

#include <QApplication> // 挂接前泵一下事件
#include <QElapsedTimer> // 等待 OpenGL 就绪的时限
#include <QGuiApplication> // 按名字找 QScreen
#include <QScreen> // 用逻辑几何摆窗口
#include <QThread> // 初始化等待里短暂睡眠

#include <algorithm> // 电池降帧时取较小帧率

#ifndef NOMINMAX // 外面没定义时才定义
#define NOMINMAX // 避免 Windows 头里的 min/max 宏
#endif // 结束 NOMINMAX 守卫
#define WIN32_LEAN_AND_MEAN // 少包含无关声明
#include <Windows.h> // 销毁前隐藏并解除父子关系

WallpaperManager::WallpaperManager(SettingsManager* settings, DisplayManager* displays, QObject* parent) // 保存设置和显示器
    : QObject(parent), m_settings(settings), m_displays(displays), m_monitor(new SystemMonitor(this)) { // 监视器挂在本对象上
    m_poll = new QTimer(this); // 定时器由本对象回收
    m_poll->setInterval(1000); // 每秒看一次宿主和系统
    m_poll->setTimerType(Qt::CoarseTimer); // 不需要精确到毫秒
    connect(m_poll, &QTimer::timeout, this, &WallpaperManager::pollSystem); // 到点就轮询
    m_poll->start(); // 未启动壁纸时也先转着，poll 里会直接返回

    connect(m_settings, &SettingsManager::changed, this, &WallpaperManager::onSettingsChanged); // 设置变化后推参数
    connect(m_displays, &DisplayManager::displaysChanged, this, [this] { // 显示器热插拔
        if (m_started) { // 只有已经启动才重排
            relayout(); // 按新的显示器重新挂接
        } // 结束已启动才重排
    }); // 结束显示器变化
    connect(m_monitor, &SystemMonitor::displayChangeRequested, m_displays, [this] { m_displays->refresh(); }); // 系统要求刷新显示器列表
    connect(m_monitor, &SystemMonitor::fullscreenChanged, this, [this](bool active) { // 全屏应用出现或消失
        m_fullscreenPaused = active && m_settings->current().pauseOnFullscreen; // 只有打开了该选项才暂停
        updateAnimationState(); // 立刻同步动画和视频
    }); // 结束全屏变化
    connect(m_monitor, &SystemMonitor::batteryChanged, this, [this](bool onBattery) { applyBattery(onBattery); }); // 电源变化走电池策略
    connect(m_monitor, &SystemMonitor::sessionLockChanged, this, [this](bool locked) { // 锁屏或解锁
        m_sessionPaused = locked; // 锁屏期间暂停
        if (!locked && m_started) { // 解锁后宿主可能已变
            relayout(); // 重新挂一次
        } else { // 仍然锁着，或壁纸没启动
            updateAnimationState(); // 只更新暂停状态
        } // 结束解锁分支
    }); // 结束会话锁定
    connect(m_monitor, &SystemMonitor::systemSuspend, this, [this] { // 系统即将睡眠
        m_sessionPaused = true; // 先暂停，避免后台继续画
        updateAnimationState(); // 停掉动画和视频
    }); // 结束睡眠
    connect(m_monitor, &SystemMonitor::systemResume, this, [this] { // 从睡眠回来
        m_sessionPaused = m_monitor->sessionLocked(); // 若还锁着就继续暂停
        if (m_started) { // 壁纸应该还在
            relayout(); // 唤醒后重新挂接
        } // 结束已启动
    }); // 结束唤醒
} // 结束构造

void WallpaperManager::setVideoPlayer(VideoPlayer* video) { // 注入进程里那一个播放器
    m_video = video; // 不拥有，也不再创建一个
} // 结束设置播放器

WallpaperManager::~WallpaperManager() { // 对象销毁时不能留下桌面子窗口
    destroyWindows(); // 先脱离宿主再删除
    if (m_video) { // 播放器可能没注入
        m_video->stopRelease(); // 释放解码和当前帧
    } // 结束释放播放器
} // 结束析构

void WallpaperManager::start() { // 开始或在已有窗口上重排
    if (m_started && !m_windows.isEmpty()) { // 已经挂着窗口
        relayout(); // 重排会保留用户暂停
        return; // 不再清掉暂停标志
    } // 结束已启动分支
    m_error.clear(); // 新的一次启动先清错误
    m_userPaused = false; // 冷启动不继承用户暂停
    m_hostMisses = 0; // 连续丢失计数归零
    m_hostErrorReported = false; // 允许再次提示宿主丢失
    QString error; // 定位失败的原因
    m_host = WorkerWHost::locate(&error); // 发 0x052C 并找出壁纸层
    if (m_host.mode == DesktopHostMode::None) { // 没有可挂的宿主
        reportStartFailure(error.isEmpty() ? tr("未能定位桌面宿主，动态壁纸没有启动。") : error); // 可翻译，或沿用宿主给出的原因
        return; // 不创建窗口
    } // 结束没有宿主
    if (!createWindows()) { // 某个显示器挂接或初始化失败
        destroyWindows(); // 清掉已经建出来的窗口
        if (m_error.isEmpty()) { // createWindows 没填原因时补一句
            reportStartFailure(tr("壁纸窗口创建失败，动态壁纸没有启动。")); // 可翻译
        } // 结束补默认错误
        return; // 保持未启动
    } // 结束创建失败
    m_started = true; // 从此轮询和设置变化都会维护窗口
    m_appliedMonitorMode = m_settings->current().monitorMode; // 记下已经铺上的模式
    m_appliedMonitorDevice = m_settings->current().monitorDevice; // 记下已经铺上的设备
    applyBattery(m_monitor->onBattery()); // 按当前电源决定暂停或降帧
    pushParams(); // 把海面参数送进窗口
    updateAnimationState(); // 同步暂停和那一个播放器
    Logger::info(QStringLiteral("Wallpaper"), QStringLiteral("壁纸已启动。%1").arg(WorkerWHost::describe(m_host))); // 日志保持中文原文
} // 结束启动

void WallpaperManager::stop() { // 用户或退出时卸下壁纸
    if (m_video) { // 有共享播放器才停
        m_video->stopRelease(); // 停止解码并丢掉当前帧
    } // 结束停视频
    destroyWindows(); // 窗口数回到 0
    m_started = false; // 轮询不再重排
    m_userPaused = false; // 清掉用户暂停
    m_fullscreenPaused = false; // 清掉全屏暂停
    m_batteryPaused = false; // 清掉电池暂停
    m_batteryThrottle = false; // 清掉降帧
    m_sessionPaused = false; // 清掉会话暂停
    m_reattaching = false; // 不再处于重挂
    m_state = RunState::Stopped; // 对外状态为停止
    Logger::info(QStringLiteral("Wallpaper"), QStringLiteral("壁纸已停止")); // 日志保持中文原文
    emit stateChanged(); // 通知界面
} // 结束停止

void WallpaperManager::pauseByUser() { // 只记录用户暂停
    if (!m_started) { // 没启动就没有可暂停的画面
        return; // 不改变状态
    } // 结束未启动
    m_userPaused = true; // 这个标志不会被全屏退出清掉
    updateAnimationState(); // 暂停画面和视频
} // 结束用户暂停

void WallpaperManager::resumeByUser() { // 只取消用户暂停
    if (!m_started) { // 没启动则忽略
        return; // 不启动壁纸
    } // 结束未启动
    m_userPaused = false; // 其他暂停原因仍可能生效
    updateAnimationState(); // 按剩余原因决定是否继续
} // 结束用户恢复

void WallpaperManager::pollSystem() { // 定时器和自测都会进来
    m_monitor->poll(); // 更新全屏、电池和会话
    if (!m_started) { // 壁纸没启动
        return; // 不重排窗口
    } // 结束未启动
    if (m_windows.isEmpty() || !WorkerWHost::hostStillValid(m_host)) { // 窗口没了或宿主失效
        relayout(); // 整页重新挂接
        return; // 这一轮不再逐个检查
    } // 结束宿主失效
    for (WallpaperWindow* window : m_windows) { // 每个显示器的窗口
        if (window->needsReattach(m_host)) { // 父窗口不再是预期宿主
            reattach(); // 先试着重嵌入
            return; // 重嵌入自己会更新状态
        } // 结束需要重挂
    } // 结束窗口循环
    if (m_reattaching) { // 上一轮丢失，这一轮已经恢复
        m_reattaching = false; // 退出重挂状态
        m_hostMisses = 0; // 连续失败清零
        updateAnimationState(); // 恢复动画
    } // 结束恢复完成
} // 结束轮询

RunState WallpaperManager::state() const { // 给界面和自测
    return m_state; // 停止、运行或暂停
} // 结束状态

PauseReason WallpaperManager::pauseReason() const { // 多个原因同时存在时只报最高的
    if (!m_started) { // 没启动不算暂停
        return PauseReason::None; // 没有暂停原因
    } // 结束未启动
    if (m_reattaching) { // 正在找宿主，优先于其他原因
        return PauseReason::Reattaching; // 重新挂接
    } // 结束重挂
    if (m_sessionPaused) { // 锁屏或睡眠
        return PauseReason::Session; // 会话暂停
    } // 结束会话
    if (m_fullscreenPaused) { // 有全屏应用
        return PauseReason::Fullscreen; // 全屏暂停
    } // 结束全屏
    if (m_batteryPaused) { // 电池策略是暂停
        return PauseReason::Battery; // 电池暂停
    } // 结束电池
    if (m_userPaused) { // 用户暂停优先级最低
        return PauseReason::User; // 用户暂停
    } // 结束用户
    return PauseReason::None; // 正在运行
} // 结束暂停原因

bool WallpaperManager::userPaused() const { // 自测用来确认没被自动清掉
    return m_userPaused; // 只反映用户标志
} // 结束用户暂停查询

QString WallpaperManager::statusText() const { // 界面上的短状态
    if (!m_started && !m_error.isEmpty()) { // 启动失败时直接显示错误
        return m_error; // 最近一次失败
    } // 结束失败文本
    switch (pauseReason()) { // 按优先级选一句
    case PauseReason::Reattaching: // 宿主丢失
        return tr("桌面宿主丢失，正在重新挂接"); // 可翻译
    case PauseReason::Session: // 锁屏或睡眠
        return tr("已暂停（锁屏或睡眠）"); // 可翻译
    case PauseReason::Fullscreen: // 全屏应用
        return tr("已暂停（全屏应用）"); // 可翻译
    case PauseReason::Battery: // 电池策略
        return tr("已暂停（电池）"); // 可翻译
    case PauseReason::User: // 用户暂停
        return tr("已暂停（用户）"); // 可翻译
    case PauseReason::None: // 没有暂停原因
        break; // 再看是运行还是停止
    } // 结束暂停文案
    if (m_state == RunState::Running) { // 正在画
        return m_batteryThrottle ? tr("运行中（电池节能 30 FPS）") : tr("运行中"); // 降帧时注明 30 帧
    } // 结束运行中
    return tr("已停止"); // 没启动且没有错误
} // 结束状态文本

QString WallpaperManager::lastError() const { // 自测读取启动失败原因
    return m_error; // 可能为空
} // 结束最近错误

QString WallpaperManager::diagnosticsText() const { // 多行诊断，缺数据时写不可用
    const ProcessSample sample = m_monitor->processSample(); // 本进程的 CPU 和内存
    const QString unavailable = tr("不可用"); // 没有采样时的占位
    QStringList lines; // 逐行拼起来
    lines << tr("状态: %1").arg(statusText()); // 当前状态
    lines << tr("宿主: %1").arg(m_started ? WorkerWHost::describe(m_host) : tr("未挂接")); // 没启动就不描述旧宿主
    if (m_started) { // 只有挂着才检查图标层
        lines << tr("图标层在壁纸之上: %1").arg(WorkerWHost::iconsAboveWallpaper(m_host) ? tr("是") : tr("否")); // 图标必须留在上面
    } // 结束图标层
    lines << tr("壁纸窗口: %1").arg(m_windows.size()); // 窗口数量
    lines << tr("壁纸 FPS: %1").arg(m_stats.ready ? QString::number(m_stats.fps, 'f', 1) : unavailable); // 就绪后才有帧率
    lines << tr("帧时间 CPU: %1 ms").arg(m_stats.ready ? QString::number(m_stats.cpuFrameMs, 'f', 2) : unavailable); // CPU 帧时间
    lines << tr("帧时间 GPU: %1").arg(m_stats.gpuValid ? tr("%1 ms").arg(m_stats.gpuFrameMs, 0, 'f', 2) : unavailable); // GPU 时间可能无效
    lines << tr("进程 CPU: %1").arg(sample.cpuPercent >= 0.0 ? QStringLiteral("%1%").arg(sample.cpuPercent, 0, 'f', 1) : unavailable); // 负值表示没采到
    lines << tr("工作集: %1") // 工作集一行的标签
                 .arg(sample.workingSetMb >= 0.0 ? QStringLiteral("%1 MB").arg(sample.workingSetMb, 0, 'f', 1) : unavailable); // 有采样才格式化
    lines << tr("OpenGL: %1").arg(m_stats.version.isEmpty() ? unavailable : m_stats.version); // GL 版本字符串
    lines << tr("显卡: %1").arg(m_stats.renderer.isEmpty() ? unavailable : m_stats.renderer); // 渲染器名称
    lines << tr("供应商: %1").arg(m_stats.vendor.isEmpty() ? unavailable : m_stats.vendor); // 厂商
    lines << tr("显存: %1") // 显存一行的标签
                 .arg(m_stats.gpuMemoryTotalMb >= 0.0 // 总量有效才写用量
                          ? QStringLiteral("%1 / %2 MB").arg(m_stats.gpuMemoryUsedMb, 0, 'f', 0).arg(m_stats.gpuMemoryTotalMb, 0, 'f', 0) // 已用和总量
                          : unavailable); // 显存不可用
    lines << tr("画面: %1").arg(renderModeName(m_settings->current().ocean.renderMode)); // 实时海面或视频
    lines << tr("视频: %1").arg(m_video ? m_video->statusText() : unavailable); // 没有播放器时不可用
    if (m_monitor->fullscreenAppActive()) { // 当前有全屏应用
        lines << tr("全屏应用: %1").arg(m_monitor->fullscreenDescription()); // 附上说明
    } // 结束全屏应用
    if (!m_stats.error.isEmpty()) { // 渲染器留下了错误
        lines << tr("渲染错误: %1").arg(m_stats.error); // 附在诊断末尾
    } // 结束渲染错误
    return lines.join(QLatin1Char('\n')); // 合成多行文本
} // 结束诊断

DesktopHost WallpaperManager::host() const { // 自测读取当前宿主
    return m_host; // 按值返回
} // 结束宿主

int WallpaperManager::windowCount() const { // 自测确认没有残留窗口
    return m_windows.size(); // 当前窗口数
} // 结束窗口数

WallpaperWindow* WallpaperManager::windowAt(int index) const { // 取某个显示器的窗口
    return m_windows.value(index, nullptr); // 越界为空，不插入
} // 结束按下标取窗口

void WallpaperManager::onSettingsChanged() { // 任何设置写入都会进来
    const AppSettings settings = m_settings->current(); // 取一份当前设置
    if (!settings.pauseOnFullscreen) { // 关闭了全屏暂停
        m_fullscreenPaused = false; // 不再因为全屏停住
    } else { // 选项开着
        m_fullscreenPaused = m_monitor->fullscreenAppActive(); // 按当前是否有全屏应用
    } // 结束全屏选项
    applyBattery(m_monitor->onBattery()); // 电池策略可能也变了
    pushParams(); // 海面参数立即生效
    updateAnimationState(); // 同步暂停和视频
    if (m_started && (settings.monitorMode != m_appliedMonitorMode || settings.monitorDevice != m_appliedMonitorDevice)) { // 显示器选择变了
        relayout(); // 按新的屏幕重挂
    } // 结束显示器变化
} // 结束设置变化

void WallpaperManager::relayout() { // 销毁后重建，用户暂停要留下来
    if (!m_started) { // 没启动不创建窗口
        return; // 交给 start
    } // 结束未启动
    const bool userPaused = m_userPaused; // 重建前抄下用户暂停
    destroyWindows(); // 先离开旧宿主
    QString error; // 重新定位的失败原因
    m_host = WorkerWHost::locate(&error); // 再找一次桌面层
    if (m_host.mode == DesktopHostMode::None || !createWindows()) { // 宿主没有或窗口没建成
        m_reattaching = true; // 启动标志仍在，状态变为正在重挂
        m_error = error.isEmpty() ? tr("重新挂接失败。") : error; // 可翻译，或使用已有原因
        Logger::warn(QStringLiteral("Wallpaper"), m_error); // 警告日志
        updateAnimationState(); // 暂停画面
        return; // 下一轮 poll 会再试
    } // 结束重挂失败
    m_userPaused = userPaused; // 恢复用户暂停，不被重排清掉
    m_appliedMonitorMode = m_settings->current().monitorMode; // 更新已应用的模式
    m_appliedMonitorDevice = m_settings->current().monitorDevice; // 更新已应用的设备
    m_reattaching = false; // 已经挂上
    m_error.clear(); // 清掉旧的重挂错误
    pushParams(); // 新窗口需要参数
    updateAnimationState(); // 按暂停标志决定是否出画
} // 结束重排

bool WallpaperManager::createWindows() { // 为每个选中的显示器建一个窗口
    const AppSettings settings = m_settings->current(); // 用当前的显示器选择
    const QVector<MonitorDesc> monitors = m_displays->select(settings.monitorMode, settings.monitorDevice); // 全部、主屏或指定屏
    if (monitors.isEmpty()) { // 没有任何可用显示器
        reportStartFailure(tr("没有可用显示器，动态壁纸没有启动。")); // 可翻译
        return false; // 创建失败
    } // 结束没有显示器
    for (const MonitorDesc& monitor : monitors) { // 一块屏幕一个窗口
        auto* window = new WallpaperWindow(); // 尚无父对象，标题保持 OceanLiveWallpaper
        QScreen* screen = nullptr; // 对应的 Qt 屏幕
        for (QScreen* candidate : QGuiApplication::screens()) { // 在逻辑屏幕里找同名设备
            if (candidate->name().compare(monitor.deviceName, Qt::CaseInsensitive) == 0) { // 设备名不区分大小写
                screen = candidate; // 用它的逻辑几何
                break; // 找到就停
            } // 结束名字匹配
        } // 结束找屏幕
        if (screen) { // Qt 知道这块屏
            window->setGeometry(screen->geometry()); // 用逻辑坐标，避免缩放错位
        } else { // 没有同名 QScreen
            window->setGeometry(monitor.physical); // 退回物理矩形
        } // 结束摆放
        window->show(); // 让原生句柄真正建出来
        QApplication::processEvents(QEventLoop::AllEvents, 50); // 最多处理 50 毫秒，便于随后嵌入
        QString error; // 这一块屏的挂接错误
        if (!window->embed(m_host, monitor.physical, &error)) { // 物理矩形用于嵌进宿主
            window->close(); // 关掉这个失败窗口
            delete window; // 不留在桌面上
            reportStartFailure(error.isEmpty() ? tr("壁纸窗口挂接失败，动态壁纸没有启动。") : error); // 可翻译或沿用原因
            return false; // 调用方会销毁已经成功的窗口
        } // 结束挂接失败
        if (m_video) { // 有共享播放器才绑定
            window->view()->bindVideo(m_video); // 每个视图共用这一个播放器
        } // 结束绑定
        connect(window->view(), &OceanView::statsChanged, this, [this](const RenderStats& stats) { m_stats = stats; }); // 记下最近一帧统计
        m_windows.push_back(window); // 纳入管理
    } // 结束每块屏幕

    QElapsedTimer timer; // 等待渲染器就绪
    timer.start(); // 从现在计时
    bool ready = false; // 全部表面都就绪才算成功
    QString failure; // 初始化期间的渲染错误
    while (timer.elapsed() < 4000) { // 最多等 4 秒
        QApplication::processEvents(QEventLoop::AllEvents, 30); // 让 GL 初始化往前走
        ready = !m_windows.isEmpty(); // 至少要有一个窗口
        for (WallpaperWindow* window : m_windows) { // 每个视图都要就绪
            if (!window->view()->surface()->isReady()) { // 表面还没好
                ready = false; // 继续等
            } // 结束未就绪
            if (!window->view()->stats().error.isEmpty() && !window->view()->surface()->isReady()) { // 失败且没有恢复
                failure = window->view()->stats().error; // 留下渲染器的错误
            } // 结束记录失败
        } // 结束检查窗口
        if (ready || !failure.isEmpty()) { // 都好了，或者已经失败
            break; // 不再空等
        } // 结束提前跳出
        QThread::msleep(15); // 短暂让出 CPU
    } // 结束等待
    if (!failure.isEmpty()) { // 初始化报错
        reportStartFailure(failure); // 使用渲染器的原文
        return false; // 启动失败
    } // 结束初始化错误
    if (!ready) { // 4 秒内没就绪
        reportStartFailure(tr("OpenGL 渲染器没有在限定时间内完成初始化，动态壁纸没有启动。")); // 可翻译
        return false; // 超时
    } // 结束超时
    for (WallpaperWindow* window : m_windows) { // 就绪之后才监听运行中的失败
        connect(window->view(), &OceanView::renderFailed, this, [this](const QString& message) { // 避免和启动失败重复处理
            reportStartFailure(message); // 记下运行中的渲染错误
            stop(); // 卸下壁纸，不留下坏窗口
        }); // 结束运行失败回调
    } // 结束连接失败信号
    return true; // 窗口都已挂上并且渲染器就绪
} // 结束创建窗口

void WallpaperManager::destroyWindows() { // 删除前先离开桌面宿主
    for (WallpaperWindow* window : m_windows) { // 每个壁纸窗口
        const HWND hwnd = reinterpret_cast<HWND>(window->winId()); // 原生句柄
        if (IsWindow(hwnd)) { // 句柄仍然有效
            ShowWindow(hwnd, SW_HIDE); // 先隐藏
            SetParent(hwnd, nullptr); // 解除父子关系，避免删掉时影响宿主
        } // 结束句柄还在
        window->hide(); // Qt 侧也隐藏
        delete window; // 销毁视图和窗口
    } // 结束逐个销毁
    m_windows.clear(); // 窗口数变为 0
    m_stats = {}; // 清掉旧的帧统计
} // 结束销毁

void WallpaperManager::pushParams() { // 把当前海面参数推进去
    const OceanParams params = m_settings->current().ocean; // 设置里的一份拷贝
    int fps = params.targetFps; // 用户选择的目标帧率
    if (m_batteryThrottle) { // 电池策略是节能
        fps = std::min(fps, 30); // 不超过 30 帧
    } // 结束降帧
    OceanParams applied = params; // 不改设置里的原值
    applied.targetFps = fps; // 使用可能降过的帧率
    for (WallpaperWindow* window : m_windows) { // 每个视图同一套参数
        window->view()->setParams(applied); // 推给海面
    } // 结束推送
} // 结束推参数

void WallpaperManager::updateAnimationState() { // 汇总所有暂停来源
    if (!m_started || m_windows.isEmpty()) { // 没启动或窗口已经拆了
        m_state = RunState::Stopped; // 对外是停止
        emit stateChanged(); // 通知界面
        return; // 没有视图可暂停
    } // 结束没有窗口
    const bool paused = m_userPaused || m_fullscreenPaused || m_batteryPaused || m_sessionPaused || m_reattaching; // 任一原因都暂停
    m_state = paused ? RunState::Paused : RunState::Running; // 只在完全没有原因时运行
    pushParams(); // 暂停或降帧后的帧率一并送出
    for (WallpaperWindow* window : m_windows) { // 所有视图一起停或一起走
        window->view()->setPaused(paused); // 停掉海面计时
    } // 结束设置暂停
    syncVideoPlayback(); // 那一个播放器跟着暂停或播放
    emit stateChanged(); // 通知界面
} // 结束更新动画

void WallpaperManager::syncVideoPlayback() { // 实时海面时不能留着解码器
    if (!m_video) { // 还没注入播放器
        return; // 无事可做
    } // 结束没有播放器
    const OceanParams ocean = m_settings->current().ocean; // 当前画面模式和视频路径
    const bool wallpaperVideo = m_started && ocean.renderMode == RenderMode::Video; // 只有启动后的视频模式才出画
    if (!wallpaperVideo) { // 实时海面，或已经停止
        if (m_started) { // 停止流程自己会释放，这里只处理切回海面
            m_video->stopRelease(); // 关掉唯一播放器，避免后台解码
        } // 结束仍处于启动
        return; // 不再 play
    } // 结束非视频模式
    const bool paused = m_userPaused || m_fullscreenPaused || m_batteryPaused || m_sessionPaused || m_reattaching; // 与画面使用同一组原因
    if (paused) { // 暂停时停止出帧，但不换文件
        m_video->pause(); // 暂停这一个播放器
        return; // 不调用播放
    } // 结束暂停
    m_video->playFile(ocean.videoPath, ocean.videoLoop, ocean.videoMuted); // 按设置循环和静音播放
} // 结束同步视频

void WallpaperManager::applyBattery(bool onBattery) { // 按三条策略处理电源
    const int policy = m_settings->current().batteryPolicy; // 0 忽略，1 降帧，2 暂停
    m_batteryThrottle = onBattery && policy == 1; // 只用电池且策略为 1 时限到 30 帧
    m_batteryPaused = onBattery && policy == 2; // 只用电池且策略为 2 时暂停
    updateAnimationState(); // 让帧率和播放器跟上
} // 结束电池策略

void WallpaperManager::reattach() { // 窗口还在，只换父级
    m_reattaching = true; // 先进入重挂，失败时状态可读
    QString error; // 定位失败原因
    const DesktopHost host = WorkerWHost::locate(&error); // 重新找宿主
    if (host.mode == DesktopHostMode::None) { // 这一轮还是没有
        ++m_hostMisses; // 连续失败加一
        Logger::warn(QStringLiteral("Wallpaper"), // 日志分类
                     QStringLiteral("重新定位宿主失败（%1）：%2").arg(m_hostMisses).arg(error)); // 日志保持中文原文
        if (m_hostMisses >= 5 && !m_hostErrorReported) { // 连续 5 次才提示，并且只提示一次
            m_hostErrorReported = true; // 挡住重复提示
            m_error = tr("Explorer 重启后仍无法定位桌面宿主。"); // 可翻译
            emit errorOccurred(m_error); // 通知界面
        } // 结束达到阈值
        updateAnimationState(); // 保持暂停
        return; // 下一秒再试
    } // 结束暂时找不到
    m_host = host; // 用新的宿主
    bool ok = true; // 所有窗口都要重嵌入成功
    QString attachError; // 最后一个失败原因
    for (WallpaperWindow* window : m_windows) { // 逐个放回新宿主
        if (!window->reembed(m_host, &attachError)) { // 矩形无效或 SetParent 失败
            ok = false; // 标记失败，继续记下错误
        } // 结束单个失败
    } // 结束重嵌入
    if (!ok) { // 至少有一个窗口没挂上
        Logger::warn(QStringLiteral("Wallpaper"), attachError); // 记下原因
        relayout(); // 退回整页重建
        return; // relayout 会更新状态
    } // 结束重嵌入失败
    m_hostMisses = 0; // 已经恢复
    m_hostErrorReported = false; // 下次再连续失败还可以提示
    m_reattaching = false; // 退出重挂
    m_error.clear(); // 清掉旧错误
    pushParams(); // 新的父窗口下再推一次参数
    updateAnimationState(); // 恢复或继续暂停
} // 结束重嵌入

void WallpaperManager::reportStartFailure(const QString& message) { // 启动、重排和运行中失败都走这里
    m_error = message; // 界面和自测读取它
    m_state = RunState::Stopped; // 对外先视为停止
    Logger::error(QStringLiteral("Wallpaper"), message); // 错误日志
    emit errorOccurred(message); // 通知界面
    emit stateChanged(); // 刷新状态文本
} // 结束报告失败
