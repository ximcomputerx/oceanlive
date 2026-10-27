// SPDX-License-Identifier: Apache-2.0
#include "ui/MainWindow.h" // 主窗口声明

#include "platform/win32/DarkTitle.h" // 深色标题栏
#include "render/VideoPlayer.h" // 视频播放器
#include "ui/IconFactory.h" // 程序图标
#include "ui/SettingsDialog.h" // 设置对话框
#include "version.h" // 版本号

#include <QApplication> // 退出整个程序
#include <QCoreApplication> // 读取启动参数
#include <QCloseEvent> // 关闭事件类型
#include <QEvent> // 通用事件类型
#include <QMessageBox> // 警告对话框
#include <QShowEvent> // 显示事件类型
#include <QSystemTrayIcon> // 判断系统托盘是否可用
#include <QTimer> // 诊断刷新定时器

#include <algorithm> // 数值夹紧

#include "ui_MainWindow.h" // 生成的主界面头

namespace { // 本文件私有辅助

QString factorText(float value, bool speed) { // 把倍率格式化成文字
    return speed ? QString::number(value, 'f', 2) + QLatin1Char('x') : QString::number(value, 'f', 2); // 速度带倍率后缀，其余保留两位
} // 结束格式化

} // 结束匿名命名空间

MainWindow::MainWindow(SettingsManager* settings, WallpaperManager* wallpapers, DisplayManager* displays, QWidget* parent) // 构造主窗口
    : QMainWindow(parent), // 初始化基类
      ui(new Ui::MainWindow), // 创建生成的界面
      m_settings(settings), // 记下设置管理器
      m_wallpapers(wallpapers), // 记下壁纸管理器
      m_displays(displays) { // 记下显示器管理器
    ui->setupUi(this); // 装入控件树
    setWindowIcon(IconFactory::appIcon()); // 设置窗口图标
    ui->fpsCombo->addItems({QStringLiteral("30 FPS"), QStringLiteral("60 FPS")}); // 填入 30 与 60 帧选项
    refillChoiceCombos(); // 填来源、主题和画质
    ui->versionLabel->setText(tr("版本 %1").arg(QString::fromLatin1(kAppVersion))); // 在版本标签显示程序版本
    ui->diagText->setPlaceholderText(tr("诊断信息会在这里更新。")); // 诊断区占位提示
    bind(); // 连接信号
    reloadMonitors(); // 填显示器列表
    reloadFromSettings(); // 按已存设置回填
    refreshState(); // 刷新运行状态
    m_diagTimer = new QTimer(this); // 创建诊断定时器
    m_diagTimer->setInterval(500); // 每 500 毫秒刷新一次
    connect(m_diagTimer, &QTimer::timeout, this, &MainWindow::refreshDiagnostics); // 超时调用诊断刷新
    m_diagTimer->start(); // 启动诊断定时器
} // 结束构造

MainWindow::~MainWindow() { // 析构主窗口
    delete ui; // 释放界面对象
} // 结束析构

void MainWindow::refillChoiceCombos() { // 重填随语言变化的下拉项
    const int mode = ui->modeCombo->currentIndex(); // 记住当前画面来源
    const int theme = ui->themeCombo->currentIndex(); // 记住当前主题
    const int quality = ui->qualityCombo->currentIndex(); // 记住当前画质
    const QSignalBlocker blockMode(ui->modeCombo); // 重填来源时不发信号
    const QSignalBlocker blockTheme(ui->themeCombo); // 重填主题时不发信号
    const QSignalBlocker blockQuality(ui->qualityCombo); // 重填画质时不发信号
    ui->modeCombo->clear(); // 清空画面来源
    ui->modeCombo->addItem(renderModeName(RenderMode::Realtime)); // 加入实时海洋
    ui->modeCombo->addItem(renderModeName(RenderMode::Video)); // 加入本地视频
    ui->themeCombo->clear(); // 清空主题
    ui->themeCombo->addItem(oceanThemeName(OceanTheme::Sunny)); // 加入晴天主题
    ui->themeCombo->addItem(oceanThemeName(OceanTheme::Sunset)); // 加入日落主题
    ui->themeCombo->addItem(oceanThemeName(OceanTheme::Deep)); // 加入深海主题
    ui->qualityCombo->clear(); // 清空画质
    ui->qualityCombo->addItem(tr("低")); // 加入低画质
    ui->qualityCombo->addItem(tr("中")); // 加入中画质
    ui->qualityCombo->addItem(tr("高")); // 加入高画质
    ui->modeCombo->setCurrentIndex(mode < 0 ? 0 : mode); // 恢复来源选择，空则选第一项
    ui->themeCombo->setCurrentIndex(theme < 0 ? 0 : theme); // 恢复主题选择，空则选第一项
    ui->qualityCombo->setCurrentIndex(quality < 0 ? 0 : quality); // 恢复画质选择，空则选第一项
} // 结束重填下拉项

void MainWindow::bind() { // 连接控件与后台信号
    connect(ui->settingsButton, &QPushButton::clicked, this, &MainWindow::openSettings); // 设置按钮打开设置
    connect(ui->startButton, &QPushButton::clicked, m_wallpapers, &WallpaperManager::start); // 启动按钮开始壁纸
    connect(ui->pauseButton, &QPushButton::clicked, m_wallpapers, &WallpaperManager::pauseByUser); // 暂停按钮按用户暂停
    connect(ui->resumeButton, &QPushButton::clicked, m_wallpapers, &WallpaperManager::resumeByUser); // 恢复按钮按用户继续
    connect(ui->stopButton, &QPushButton::clicked, m_wallpapers, &WallpaperManager::stop); // 停止按钮停止壁纸
    connect(m_wallpapers, &WallpaperManager::stateChanged, this, &MainWindow::refreshState); // 壁纸状态变化后刷新界面
    connect(m_wallpapers, &WallpaperManager::errorOccurred, this, [this](const QString& message) { // 壁纸出错时处理
        if (QCoreApplication::arguments().contains(QStringLiteral("--self-test"))) { // 自测模式不弹窗
            return; // 直接返回
        } // 结束自测判断
        QMessageBox::warning(this, QStringLiteral("OceanLive"), message); // 弹出壁纸错误
    }); // 结束壁纸错误处理
    connect(m_settings, &SettingsManager::changed, this, &MainWindow::reloadFromSettings); // 设置变化后回填界面
    connect(m_settings, &SettingsManager::errorOccurred, this, [this](const QString& message) { // 设置保存出错时处理
        QMessageBox::warning(this, QStringLiteral("OceanLive"), message); // 弹出设置错误
    }); // 结束设置错误处理
    connect(m_displays, &DisplayManager::displaysChanged, this, &MainWindow::reloadMonitors); // 显示器变化后重填列表
    connect(ui->previewView, &OceanView::renderFailed, this, [this](const QString& message) { // 预览渲染失败时处理
        ui->statusLabel->setText(message); // 把失败原因写到状态标签
    }); // 结束预览失败处理

    connect(ui->themeCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) { // 切换主题时套用主题色
        if (m_loading) { // 回填过程中忽略这次切换
            return; // 直接返回
        } // 结束回填判断
        AppSettings settings = m_settings->current(); // 取出当前设置作底稿
        settings.ocean = settings.ocean.withTheme(oceanThemeFromInt(index)); // 换成所选主题的海洋参数
        settings.ocean.animSpeed = ui->speedSlider->value() / 100.0f; // 保留滑条上的动画速度
        settings.ocean.waveStrength = ui->waveSlider->value() / 100.0f; // 保留滑条上的波浪强度
        settings.ocean.quality = ui->qualityCombo->currentIndex(); // 保留当前画质
        settings.ocean.targetFps = ui->fpsCombo->currentIndex() == 0 ? 30 : 60; // 按帧率下拉写成 30 或 60
        settings.ocean.renderMode = renderModeFromInt(ui->modeCombo->currentIndex()); // 保留当前画面来源
        QString error; // 接收写入失败信息
        m_loading = true; // 写回前挡住界面回填
        m_settings->apply(settings, &error); // 把主题设置写入
        m_loading = false; // 恢复回填
        reloadFromSettings(); // 用新设置刷新控件
    }); // 结束主题切换处理
    connect(ui->modeCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) { pushQuickSettings(); }); // 画面来源变化后写回快捷设置
    connect(ui->monitorCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) { pushQuickSettings(); }); // 显示器变化后写回快捷设置
    connect(ui->qualityCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) { pushQuickSettings(); }); // 画质变化后写回快捷设置
    connect(ui->fpsCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) { pushQuickSettings(); }); // 帧率变化后写回快捷设置
    connect(ui->speedSlider, &QSlider::valueChanged, this, [this](int value) { // 动画速度滑条变化
        ui->speedValueLabel->setText(factorText(value / 100.0f, true)); // 更新速度文字
        pushQuickSettings(); // 写回快捷设置
    }); // 结束速度滑条处理
    connect(ui->waveSlider, &QSlider::valueChanged, this, [this](int value) { // 波浪强度滑条变化
        ui->waveValueLabel->setText(factorText(value / 100.0f, false)); // 更新波浪文字
        pushQuickSettings(); // 写回快捷设置
    }); // 结束波浪滑条处理
    connect(ui->startupCheck, &QCheckBox::toggled, this, [this](bool) { pushQuickSettings(); }); // 开机启动勾选后写回
    connect(ui->fullscreenPauseCheck, &QCheckBox::toggled, this, [this](bool) { pushQuickSettings(); }); // 全屏暂停勾选后写回
} // 结束信号连接

void MainWindow::reloadFromSettings() { // 用当前设置回填主界面
    if (m_loading) { // 回填中则避免重入
        return; // 直接返回
    } // 结束重入判断
    m_loading = true; // 开始回填并挡住信号
    const AppSettings settings = m_settings->current(); // 读取当前设置
    ui->themeCombo->setCurrentIndex(static_cast<int>(settings.ocean.theme)); // 同步主题下拉
    ui->modeCombo->setCurrentIndex(static_cast<int>(settings.ocean.renderMode)); // 同步画面来源
    const bool videoPathChanged = settings.ocean.videoPath != m_lastVideoPath; // 判断视频路径是否变化
    ui->previewHint->setText(settings.ocean.renderMode == RenderMode::Video // 按模式准备预览说明
                                 ? tr("视频模式和桌面壁纸共用一个播放器。停止壁纸后会释放解码器。") // 视频模式的说明文字
                                 : tr("预览使用与桌面壁纸相同的实时海洋渲染器。")); // 实时海洋的说明文字
    ui->speedSlider->setValue(std::clamp(static_cast<int>(std::lround(settings.ocean.animSpeed * 100.0f)), 25, 200)); // 把动画速度夹进滑条范围
    ui->waveSlider->setValue(std::clamp(static_cast<int>(std::lround(settings.ocean.waveStrength * 100.0f)), 20, 200)); // 把波浪强度夹进滑条范围
    ui->speedValueLabel->setText(factorText(settings.ocean.animSpeed, true)); // 显示动画速度
    ui->waveValueLabel->setText(factorText(settings.ocean.waveStrength, false)); // 显示波浪强度
    ui->qualityCombo->setCurrentIndex(settings.ocean.quality); // 同步画质下拉
    ui->fpsCombo->setCurrentIndex(settings.ocean.targetFps >= 60 ? 1 : 0); // 60 帧选第二项，否则第一项
    ui->startupCheck->setChecked(settings.launchAtStartup); // 同步开机启动勾选
    ui->fullscreenPauseCheck->setChecked(settings.pauseOnFullscreen); // 同步全屏暂停勾选
    reloadMonitors(); // 按设置重选显示器
    ui->previewView->setParams(settings.ocean); // 把海洋参数交给预览
    ui->diagGroup->setVisible(settings.showDiagnostics); // 按开关显示或隐藏诊断区
    m_loading = false; // 回填结束
    refreshState(); // 刷新按钮和标题
    if (videoPathChanged) { // 仅路径变化时重开预览
        m_lastVideoPath = settings.ocean.videoPath; // 记下新的视频路径
        m_suppressVideoPreview = false; // 允许再次预览视频
        syncVideoPreview(); // 同步视频预览
    } // 结束路径变化处理
} // 结束回填设置

void MainWindow::reloadMonitors() { // 重填显示器下拉
    const AppSettings settings = m_settings->current(); // 读取当前显示器设置
    const QSignalBlocker blocker(ui->monitorCombo); // 重填时不触发选择信号
    ui->monitorCombo->clear(); // 清空显示器列表
    ui->monitorCombo->addItem(tr("所有显示器"), QStringLiteral("*")); // 第一项表示全部显示器
    ui->monitorCombo->addItem(tr("主显示器"), QStringLiteral("@primary")); // 第二项表示主显示器
    for (const MonitorDesc& monitor : m_displays->monitors()) { // 逐台追加物理显示器
        const QString label = QStringLiteral("%1  %2x%3 @(%4,%5)%6") // 开始拼接名称、分辨率和坐标
                                  .arg(monitor.friendlyName) // 填入友好名称
                                  .arg(monitor.physical.width()) // 填入宽度
                                  .arg(monitor.physical.height()) // 填入高度
                                  .arg(monitor.physical.x()) // 填入横坐标
                                  .arg(monitor.physical.y()) // 填入纵坐标
                                  .arg(monitor.primary ? tr("  主") : QString()); // 主屏附加主标记
        ui->monitorCombo->addItem(label, monitor.deviceName); // 项的附加数据是设备名
    } // 结束显示器循环
    int index = 0; // 默认选全部显示器
    if (settings.monitorMode == 1) { // 模式 1 表示仅主显示器
        index = 1; // 选中第二项
    } else if (settings.monitorMode == 2) { // 模式 2 表示指定设备
        const int found = ui->monitorCombo->findData(settings.monitorDevice); // 按设备名查找下拉项
        index = found >= 0 ? found : 1; // 找不到则退回主显示器
    } // 结束显示器模式判断
    ui->monitorCombo->setCurrentIndex(index); // 选中对应显示器
} // 结束重填显示器

void MainWindow::pushQuickSettings() { // 把快捷控件写回设置
    if (m_loading) { // 回填过程中不写回
        return; // 直接返回
    } // 结束回填判断
    AppSettings settings = m_settings->current(); // 以当前设置为底稿
    settings.ocean.theme = oceanThemeFromInt(ui->themeCombo->currentIndex()); // 写入所选主题
    settings.ocean.renderMode = renderModeFromInt(ui->modeCombo->currentIndex()); // 写入画面来源
    settings.ocean.animSpeed = ui->speedSlider->value() / 100.0f; // 滑条百分数换成动画速度
    settings.ocean.waveStrength = ui->waveSlider->value() / 100.0f; // 滑条百分数换成波浪强度
    settings.ocean.quality = ui->qualityCombo->currentIndex(); // 写入画质档位
    settings.ocean.targetFps = ui->fpsCombo->currentIndex() == 0 ? 30 : 60; // 第一项 30 帧，否则 60 帧
    settings.launchAtStartup = ui->startupCheck->isChecked(); // 写入开机启动开关
    settings.pauseOnFullscreen = ui->fullscreenPauseCheck->isChecked(); // 写入全屏暂停开关
    const int monitorIndex = ui->monitorCombo->currentIndex(); // 读取显示器下拉索引
    if (monitorIndex <= 0) { // 第一项或空选表示全部显示器
        settings.monitorMode = 0; // 监视模式设为全部
    } else if (monitorIndex == 1) { // 第二项表示主显示器
        settings.monitorMode = 1; // 监视模式设为主显示器
    } else { // 其余项表示指定显示器
        settings.monitorMode = 2; // 监视模式设为指定设备
        settings.monitorDevice = ui->monitorCombo->currentData().toString(); // 记下所选设备名
    } // 结束显示器模式分支
    QString error; // 接收写入失败信息
    m_loading = true; // 写回前挡住回填
    m_settings->apply(settings, &error); // 应用快捷设置
    m_loading = false; // 恢复回填
    reloadFromSettings(); // 用结果刷新界面
} // 结束写回快捷设置

void MainWindow::setVideoPlayer(VideoPlayer* video) { // 接入预览播放器
    m_video = video; // 保存播放器指针
    ui->previewView->bindVideo(video); // 预览控件绑定同一播放器
    if (!m_video) { // 没有播放器就结束
        return; // 直接返回
    } // 结束空播放器判断
    connect(m_video, &VideoPlayer::failed, this, [this](const QString& message) { // 播放失败时处理
        const QStringList arguments = QCoreApplication::arguments(); // 读取启动参数
        if (arguments.contains(QStringLiteral("--self-test")) || arguments.contains(QStringLiteral("--video-desktop-test"))) { // 自测或桌面视频测试时不弹窗
            return; // 直接返回
        } // 结束测试判断
        QMessageBox::warning(this, QStringLiteral("OceanLive"), message); // 弹出播放错误
    }); // 结束播放失败处理
} // 结束接入播放器

void MainWindow::syncVideoPreview() { // 按模式同步预览播放
    if (!m_video) { // 没有播放器则不做
        return; // 直接返回
    } // 结束空播放器判断
    const AppSettings settings = m_settings->current(); // 读取当前设置
    if (settings.ocean.renderMode != RenderMode::Video) { // 不是视频模式时处理
        if (m_wallpapers->state() == RunState::Stopped) { // 壁纸已停止才释放解码器
            m_video->stopRelease(); // 停止并释放播放器
        } // 结束停止判断
        return; // 实时模式到此返回
    } // 结束非视频模式
    if (m_wallpapers->state() != RunState::Stopped) { // 壁纸仍在跑时不抢播放器
        return; // 直接返回
    } // 结束运行中判断
    if (m_suppressVideoPreview || !isVisible() || settings.ocean.videoPath.isEmpty()) { // 被抑制、窗口不可见或没有路径则不播
        m_video->stopRelease(); // 停止并释放播放器
        return; // 直接返回
    } // 结束抑制判断
    m_video->playFile(settings.ocean.videoPath, settings.ocean.videoLoop, settings.ocean.videoMuted); // 按循环和静音播放所选文件
} // 结束同步预览

void MainWindow::refreshState() { // 按运行状态刷新按钮和标题
    const RunState state = m_wallpapers->state(); // 读取壁纸运行状态
    if (state == RunState::Stopped && m_seenRunning) { // 从运行回到停止后抑制预览
        m_suppressVideoPreview = true; // 停止后不再自动预览视频
    } // 结束停止抑制
    if (state != RunState::Stopped) { // 当前不处于停止
        m_seenRunning = true; // 记下曾经运行过
        m_suppressVideoPreview = false; // 运行中允许预览
    } // 结束运行标记
    const bool started = state != RunState::Stopped; // 是否已经启动壁纸
    ui->startButton->setEnabled(!started); // 未启动时才能点启动
    ui->stopButton->setEnabled(started); // 已启动时才能点停止
    ui->pauseButton->setEnabled(m_wallpapers->state() == RunState::Running); // 仅运行中可暂停
    ui->resumeButton->setEnabled(m_wallpapers->userPaused()); // 仅用户暂停时可恢复
    ui->statusLabel->setText(m_wallpapers->statusText()); // 状态标签显示壁纸状态
    setWindowTitle(tr("OceanLive — %1").arg(m_wallpapers->statusText())); // 窗口标题带上状态文字
    syncVideoPreview(); // 状态变化后同步预览
} // 结束刷新状态

void MainWindow::refreshDiagnostics() { // 刷新诊断文本
    if (!ui->diagGroup->isVisible()) { // 诊断区隐藏时不统计
        return; // 直接返回
    } // 结束隐藏判断
    const RenderStats preview = ui->previewView->stats(); // 读取预览渲染统计
    const QString unavailable = tr("不可用"); // 缺少数据时的占位词
    QString text = m_wallpapers->diagnosticsText(); // 先取壁纸侧诊断
    text += tr("\n预览 FPS: %1").arg(preview.ready ? QString::number(preview.fps, 'f', 1) : unavailable); // 追加预览帧率，未就绪则不可用
    text += tr("\n预览 CPU: %1").arg(preview.ready ? tr("%1 ms").arg(preview.cpuFrameMs, 0, 'f', 2) : unavailable); // 追加预览帧耗时，未就绪则不可用
    text += tr("\n预览 GPU: %1").arg(preview.gpuValid ? tr("%1 ms").arg(preview.gpuFrameMs, 0, 'f', 2) : unavailable); // 追加预览显卡耗时，无效则不可用
    if (preview.gpuMemoryTotalMb >= 0.0) { // 驱动提供了显存总量
        text += tr("\n预览显存: %1 / %2 MB").arg(preview.gpuMemoryUsedMb, 0, 'f', 0).arg(preview.gpuMemoryTotalMb, 0, 'f', 0); // 追加已用与总显存
    } // 结束显存行
    if (preview.version.isEmpty() && m_wallpapers->state() == RunState::Stopped) { // 停止且还没有版本字符串
        text += tr("\nOpenGL 预览: %1 / %2").arg(preview.version.isEmpty() ? tr("等待首帧") : preview.version, preview.renderer); // 追加等待首帧或版本与渲染器
    } // 结束版本行
    ui->diagText->setPlainText(text); // 把诊断写入文本框
} // 结束刷新诊断

void MainWindow::openSettings() { // 打开设置对话框
    SettingsDialog dialog(m_settings, m_displays, this); // 创建设置对话框
    dialog.exec(); // 模态显示直到关闭
} // 结束打开设置

void MainWindow::bringToFront() { // 把主窗口提到前台
    showNormal(); // 从最小化还原
    raise(); // 提升到同级窗口之上
    activateWindow(); // 取得键盘焦点
} // 结束提到前台

void MainWindow::requestQuit() { // 请求真正退出
    m_quit = true; // 关闭时不再缩进托盘
    QApplication::quit(); // 退出应用程序
} // 结束请求退出

void MainWindow::changeEvent(QEvent* event) { // 处理语言等窗口事件
    QMainWindow::changeEvent(event); // 先交给基类
    if (event->type() != QEvent::LanguageChange || ui == nullptr) { // 不是语言切换或界面已释放则忽略
        return; // 直接返回
    } // 结束语言判断
    const bool wasLoading = m_loading; // 记住回填锁原来的状态
    m_loading = true; // 重译期间挡住写回
    ui->retranslateUi(this); // 按新语言重译界面文字
    ui->versionLabel->setText(tr("版本 %1").arg(QString::fromLatin1(kAppVersion))); // 重新写入版本号
    ui->diagText->setPlaceholderText(tr("诊断信息会在这里更新。")); // 重新写入诊断占位文字
    refillChoiceCombos(); // 按新语言重填下拉项
    reloadMonitors(); // 按新语言重填显示器
    m_loading = wasLoading; // 恢复原来的回填锁
    if (!m_loading) { // 不在回填中才刷新数据
        reloadFromSettings(); // 重新回填设置
        refreshDiagnostics(); // 立即刷新诊断
    } // 结束刷新判断
} // 结束语言切换处理

void MainWindow::closeEvent(QCloseEvent* event) { // 处理关闭按钮
    if (m_quit || !QSystemTrayIcon::isSystemTrayAvailable()) { // 已要求退出或没有托盘
        m_quit = true; // 标记为真正退出
        event->accept(); // 接受关闭
        return; // 直接返回
    } // 结束退出判断
    event->ignore(); // 忽略关闭，不销毁窗口
    hide(); // 隐藏主窗口
    syncVideoPreview(); // 隐藏后停掉不该播的预览
    emit minimizedToTray(); // 通知已缩进托盘
} // 结束关闭处理

void MainWindow::showEvent(QShowEvent* event) { // 处理窗口显示
    QMainWindow::showEvent(event); // 先交给基类
    enableDarkTitleBar(static_cast<quintptr>(winId())); // 给本窗口套深色标题栏
    syncVideoPreview(); // 显示后按规则同步预览
} // 结束显示处理
