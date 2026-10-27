// SPDX-License-Identifier: Apache-2.0
#include "ui/SettingsDialog.h" // 设置对话框声明

#include "platform/win32/Autostart.h" // 开机启动项注册
#include "platform/win32/DarkTitle.h" // 深色标题栏
#include "version.h" // 版本号

#include <QColorDialog> // 取色对话框
#include <QCoreApplication> // 翻译与程序路径
#include <QEvent> // 通用事件
#include <QFileDialog> // 选择视频文件
#include <QFileInfo> // 取视频所在目录
#include <QPushButton> // 颜色按钮类型

#include <algorithm> // 数值夹紧

#include "ui_SettingsDialog.h" // 生成的设置界面头

namespace { // 本文件私有辅助

QString factorText(float value) { // 把倍率格式化成两位小数
    return QString::number(value, 'f', 2); // 保留两位小数
} // 结束倍率文字

QString degreeText(int value) { // 把角度格式化成文字
    return QString::number(value) + QStringLiteral("°"); // 数字后加度符号
} // 结束角度文字

QColor colorFromVector(const QVector3D& color) { // 三维颜色转界面颜色
    return QColor::fromRgbF(std::clamp(color.x(), 0.0f, 1.0f), std::clamp(color.y(), 0.0f, 1.0f), // 红绿分量夹到 0 到 1
                            std::clamp(color.z(), 0.0f, 1.0f)); // 蓝分量夹到 0 到 1
} // 结束转界面颜色

QVector3D vectorFromColor(const QColor& color) { // 界面颜色转三维向量
    return QVector3D(static_cast<float>(color.redF()), static_cast<float>(color.greenF()), static_cast<float>(color.blueF())); // 取出红绿蓝浮点分量
} // 结束转三维颜色

void paintColorButton(QPushButton* button, const QColor& color, bool custom) { // 按是否自定义刷新颜色按钮
    button->setText(custom ? color.name(QColor::HexRgb) : QCoreApplication::translate("SettingsDialog", "主题颜色")); // 自定义显示色值，否则显示译文「主题颜色」
    button->setStyleSheet(QStringLiteral("background-color: %1; color: %2; border: 1px solid #8aa4b5;") // 按钮底色样式
                              .arg(color.name(QColor::HexRgb), color.lightness() > 150 ? QStringLiteral("#102028") : QStringLiteral("#f4f7f8"))); // 亮底用深字，暗底用浅字
    button->setProperty("rgb", color.name(QColor::HexRgb)); // 把色值存到按钮属性
} // 结束刷新颜色按钮

} // 结束匿名命名空间

SettingsDialog::SettingsDialog(SettingsManager* settings, DisplayManager* displays, QWidget* parent) // 构造设置对话框
    : QDialog(parent), ui(new Ui::SettingsDialog), m_settings(settings), m_displays(displays) { // 初始化基类、界面和依赖
    ui->setupUi(this); // 装入控件树
    ui->settingsFpsCombo->addItems({QStringLiteral("30 FPS"), QStringLiteral("60 FPS")}); // 填入 30 与 60 帧选项
    refillChoices(); // 填来源、主题、画质和语言
    ui->pageList->setCurrentRow(0); // 默认显示第一页
    ui->animationScroll->setWidgetResizable(true); // 动画页内容随滚动区伸缩
    m_snapshot = m_settings->current(); // 记下打开时的设置快照
    populateMonitors(); // 填显示器列表
    load(m_snapshot); // 用快照回填控件

    connect(ui->pageList, &QListWidget::currentRowChanged, ui->pageStack, &QStackedWidget::setCurrentIndex); // 左侧页签切换右侧页面
    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept); // 确定按钮接受对话框
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, [this] { // 取消按钮先恢复再拒绝
        revert(); // 恢复打开前的设置
        reject(); // 关闭对话框
    }); // 结束取消处理
    if (QPushButton* apply = ui->buttonBox->button(QDialogButtonBox::Apply)) { // 找到应用按钮才连接
        connect(apply, &QPushButton::clicked, this, [this] { m_snapshot = m_settings->current(); }); // 应用只更新快照，不关闭
    } // 结束应用按钮判断

    const auto live = [this] { pushLive(); }; // 多数控件共用的即时写回
    connect(ui->resumeOnLaunchCheck, &QCheckBox::toggled, this, live); // 启动时恢复壁纸的勾选即时生效
    connect(ui->startMinimizedCheck, &QCheckBox::toggled, this, live); // 启动后最小化的勾选即时生效
    connect(ui->showDiagnosticsCheck, &QCheckBox::toggled, this, live); // 显示诊断的勾选即时生效
    connect(ui->monitorAllRadio, &QRadioButton::toggled, this, live); // 所有显示器单选即时生效
    connect(ui->monitorPrimaryRadio, &QRadioButton::toggled, this, live); // 仅主显示器单选即时生效
    connect(ui->monitorSpecificRadio, &QRadioButton::toggled, this, [this](bool checked) { // 指定显示器单选还要开关列表
        ui->monitorList->setEnabled(checked); // 仅指定模式可点显示器列表
        pushLive(); // 立刻写回
    }); // 结束指定显示器处理
    connect(ui->monitorList, &QListWidget::currentRowChanged, this, [this](int) { pushLive(); }); // 列表换行后写回
    connect(ui->modeCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, live); // 画面来源变化后写回
    connect(ui->settingsThemeCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) { // 切换主题时套用主题参数
        if (m_loading) { // 回填中忽略主题切换
            return; // 直接返回
        } // 结束回填判断
        AppSettings settings = collect(); // 先收集当前控件
        settings.ocean = settings.ocean.withTheme(oceanThemeFromInt(index)); // 换成所选主题的海洋参数
        m_loading = true; // 回填前挡住写回
        load(settings); // 把主题参数填回控件
        m_loading = false; // 恢复写回
        pushLive(); // 把结果即时写入设置
    }); // 结束主题切换处理
    connect(ui->settingsQualityCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, live); // 画质变化后写回
    connect(ui->settingsFpsCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, live); // 帧率变化后写回
    connect(ui->pauseFullscreenCheck, &QCheckBox::toggled, this, live); // 全屏暂停勾选后写回
    connect(ui->batteryCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, live); // 电池策略变化后写回
    connect(ui->languageCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, live); // 语言变化后写回
    connect(ui->autostartCheck, &QCheckBox::toggled, this, live); // 开机启动勾选后写回
    connect(ui->animSpeedSlider, &QSlider::valueChanged, this, live); // 波浪速度滑条变化后写回
    connect(ui->settingsWaveSlider, &QSlider::valueChanged, this, live); // 波浪强度滑条变化后写回
    connect(ui->windSlider, &QSlider::valueChanged, this, live); // 风力滑条变化后写回
    connect(ui->windAngleSlider, &QSlider::valueChanged, this, live); // 风向滑条变化后写回
    connect(ui->sunAzimuthSlider, &QSlider::valueChanged, this, live); // 太阳水平位置变化后写回
    connect(ui->sunElevationSlider, &QSlider::valueChanged, this, live); // 太阳高度变化后写回
    connect(ui->sunIntensitySlider, &QSlider::valueChanged, this, live); // 直射阳光变化后写回
    connect(ui->brightnessSlider, &QSlider::valueChanged, this, live); // 海面亮度变化后写回
    connect(ui->sunSlider, &QSlider::valueChanged, this, live); // 海面反射变化后写回
    connect(ui->highlightSlider, &QSlider::valueChanged, this, live); // 高光宽度变化后写回
    connect(ui->skySaturationSlider, &QSlider::valueChanged, this, live); // 天空饱和度变化后写回
    connect(ui->skyBrightnessSlider, &QSlider::valueChanged, this, live); // 天空亮度变化后写回
    connect(ui->hazeSlider, &QSlider::valueChanged, this, live); // 大气雾化变化后写回
    connect(ui->sunDiskSlider, &QSlider::valueChanged, this, live); // 太阳盘亮度变化后写回
    connect(ui->sunHaloSlider, &QSlider::valueChanged, this, live); // 太阳光晕强度变化后写回
    connect(ui->sunHaloRadiusSlider, &QSlider::valueChanged, this, live); // 太阳光晕半径变化后写回
    connect(ui->horizonHazeSlider, &QSlider::valueChanged, this, live); // 海平线雾化变化后写回
    connect(ui->videoLoopCheck, &QCheckBox::toggled, this, live); // 循环播放勾选后写回
    connect(ui->videoMuteCheck, &QCheckBox::toggled, this, live); // 静音播放勾选后写回
    connect(ui->videoPathEdit, &QLineEdit::editingFinished, this, live); // 视频路径编辑结束后写回
    connect(ui->videoBrowseButton, &QPushButton::clicked, this, [this] { // 浏览按钮选择本地视频
        const QString path = QFileDialog::getOpenFileName( // 弹出视频文件选择框
            this, tr("选择海洋视频"), QFileInfo(ui->videoPathEdit->text()).absolutePath(), // 标题和起始目录
            tr("视频 (*.mp4 *.m4v *.mov *.avi *.mkv *.webm);;所有文件 (*.*)")); // 视频扩展名过滤器
        if (path.isEmpty()) { // 未选择文件则停止
            return; // 直接返回
        } // 结束空路径判断
        ui->videoPathEdit->setText(path); // 把所选路径填进编辑框
        if (ui->modeCombo->currentIndex() != static_cast<int>(RenderMode::Video)) { // 当前不是视频模式才切换
            ui->modeCombo->setCurrentIndex(static_cast<int>(RenderMode::Video)); // 切到视频画面来源
        } // 结束切换来源
        pushLive(); // 立刻写回新路径
    }); // 结束浏览处理
    connect(ui->waterColorButton, &QPushButton::clicked, this, [this] { // 海面颜色按钮打开取色
        const QColor picked = QColorDialog::getColor(colorFromVector(collect().ocean.waterColor), this, tr("海面颜色")); // 以当前海面色为初始颜色
        if (!picked.isValid()) { // 取消取色则停止
            return; // 直接返回
        } // 结束取消取色
        paintColorButton(ui->waterColorButton, picked, true); // 标成自定义海面色并上色
        pushLive(); // 立刻写回
    }); // 结束海面取色
    connect(ui->skyColorButton, &QPushButton::clicked, this, [this] { // 天空颜色按钮打开取色
        const QColor picked = QColorDialog::getColor(colorFromVector(collect().ocean.skyColor), this, tr("天空颜色")); // 以当前天空色为初始颜色
        if (!picked.isValid()) { // 取消取色则停止
            return; // 直接返回
        } // 结束取消取色
        paintColorButton(ui->skyColorButton, picked, true); // 标成自定义天空色并上色
        pushLive(); // 立刻写回
    }); // 结束天空取色
    connect(ui->waterResetButton, &QPushButton::clicked, this, [this] { // 海面色恢复为主题色
        const OceanPalette palette = OceanParams::paletteFor(oceanThemeFromInt(ui->settingsThemeCombo->currentIndex())); // 取当前主题的调色板
        paintColorButton(ui->waterColorButton, colorFromVector(palette.waterShallow), false); // 按钮改回浅水主题色，不再自定义
        pushLive(); // 立刻写回
    }); // 结束恢复海面色
    connect(ui->skyResetButton, &QPushButton::clicked, this, [this] { // 天空色恢复为主题色
        const OceanPalette palette = OceanParams::paletteFor(oceanThemeFromInt(ui->settingsThemeCombo->currentIndex())); // 取当前主题的调色板
        paintColorButton(ui->skyColorButton, colorFromVector(palette.skyHorizon), false); // 按钮改回地平线主题色，不再自定义
        pushLive(); // 立刻写回
    }); // 结束恢复天空色
    enableDarkTitleBar(static_cast<quintptr>(winId())); // 给对话框套深色标题栏
} // 结束构造

SettingsDialog::~SettingsDialog() { // 析构设置对话框
    delete ui; // 释放界面对象
} // 结束析构

void SettingsDialog::refillChoices() { // 重填随语言变化的下拉项
    const int mode = ui->modeCombo->currentIndex(); // 记住画面来源
    const int theme = ui->settingsThemeCombo->currentIndex(); // 记住主题
    const int quality = ui->settingsQualityCombo->currentIndex(); // 记住画质
    const int battery = ui->batteryCombo->currentIndex(); // 记住电池策略
    const QString language = ui->languageCombo->currentData().toString(); // 记住当前语言数据
    const QSignalBlocker blockMode(ui->modeCombo); // 重填来源时不发信号
    const QSignalBlocker blockTheme(ui->settingsThemeCombo); // 重填主题时不发信号
    const QSignalBlocker blockQuality(ui->settingsQualityCombo); // 重填画质时不发信号
    const QSignalBlocker blockBattery(ui->batteryCombo); // 重填电池策略时不发信号
    const QSignalBlocker blockLanguage(ui->languageCombo); // 重填语言时不发信号
    ui->modeCombo->clear(); // 清空画面来源
    ui->modeCombo->addItem(renderModeName(RenderMode::Realtime)); // 加入实时海洋
    ui->modeCombo->addItem(renderModeName(RenderMode::Video)); // 加入本地视频
    ui->settingsThemeCombo->clear(); // 清空主题
    ui->settingsThemeCombo->addItem(oceanThemeName(OceanTheme::Sunny)); // 加入晴天
    ui->settingsThemeCombo->addItem(oceanThemeName(OceanTheme::Sunset)); // 加入日落
    ui->settingsThemeCombo->addItem(oceanThemeName(OceanTheme::Deep)); // 加入深海
    ui->settingsQualityCombo->clear(); // 清空画质
    ui->settingsQualityCombo->addItem(tr("低")); // 加入低画质
    ui->settingsQualityCombo->addItem(tr("中")); // 加入中画质
    ui->settingsQualityCombo->addItem(tr("高")); // 加入高画质
    ui->batteryCombo->clear(); // 清空电池策略
    ui->batteryCombo->addItem(tr("不处理")); // 加入不处理
    ui->batteryCombo->addItem(tr("降至 30 FPS")); // 加入降至 30 帧
    ui->batteryCombo->addItem(tr("暂停动画")); // 加入暂停动画
    ui->languageCombo->clear(); // 清空语言
    ui->languageCombo->addItem(tr("跟随系统"), QStringLiteral("system")); // 加入跟随系统
    ui->languageCombo->addItem(tr("简体中文"), QStringLiteral("zh_CN")); // 加入简体中文
    ui->languageCombo->addItem(tr("English"), QStringLiteral("en")); // 加入英文
    ui->modeCombo->setCurrentIndex(mode < 0 ? 0 : mode); // 恢复来源，空则第一项
    ui->settingsThemeCombo->setCurrentIndex(theme < 0 ? 0 : theme); // 恢复主题，空则第一项
    ui->settingsQualityCombo->setCurrentIndex(quality < 0 ? 0 : quality); // 恢复画质，空则第一项
    ui->batteryCombo->setCurrentIndex(battery < 0 ? 0 : battery); // 恢复电池策略，空则第一项
    const int languageIndex = ui->languageCombo->findData(language.isEmpty() ? QStringLiteral("system") : language); // 按语言代码查找，空则跟随系统
    ui->languageCombo->setCurrentIndex(languageIndex < 0 ? 0 : languageIndex); // 找不到则选第一项
} // 结束重填下拉项

void SettingsDialog::changeEvent(QEvent* event) { // 处理语言切换
    QDialog::changeEvent(event); // 先交给基类
    if (event->type() != QEvent::LanguageChange || ui == nullptr || m_retranslating) { // 非语言切换、无界面或正在重译则忽略
        return; // 直接返回
    } // 结束忽略判断
    m_retranslating = true; // 标记正在重译，防止重入
    const bool wasLoading = m_loading; // 记住回填锁
    const int page = ui->pageList->currentRow(); // 记住当前页
    m_loading = true; // 重译期间挡住写回
    ui->retranslateUi(this); // 按新语言重译界面文字
    refillChoices(); // 重填下拉项
    populateMonitors(); // 重填显示器文字
    load(m_settings->current()); // 用当前设置再填一遍控件
    if (page >= 0) { // 原来有选中页才恢复
        ui->pageList->setCurrentRow(page); // 回到切换前的页
    } // 结束恢复页签
    m_loading = wasLoading; // 恢复回填锁
    m_retranslating = false; // 重译结束
} // 结束语言切换

void SettingsDialog::populateMonitors() { // 填充指定显示器列表
    ui->monitorList->clear(); // 先清空旧条目
    for (const MonitorDesc& monitor : m_displays->monitors()) { // 逐台加入物理显示器
        const QString label = QStringLiteral("%1\n%2x%3 @(%4,%5)  DPI %6%7") // 开始拼接设备名、分辨率、坐标和点密度
                                  .arg(monitor.deviceName, QString::number(monitor.physical.width()), // 填入设备名和宽度
                                       QString::number(monitor.physical.height()), QString::number(monitor.physical.x()), // 填入高度和横坐标
                                       QString::number(monitor.physical.y()), QString::number(monitor.dpiX)) // 填入纵坐标和水平点密度
                                  .arg(monitor.primary ? tr("  主显示器") : QString()); // 主屏附加主显示器标记
        auto* item = new QListWidgetItem(label, ui->monitorList); // 创建列表项
        item->setData(Qt::UserRole, monitor.deviceName); // 用用户角色记下设备名
    } // 结束显示器循环
} // 结束填充显示器

void SettingsDialog::load(const AppSettings& settings) { // 把一份设置填进控件
    m_loading = true; // 回填期间挡住即时写回
    ui->resumeOnLaunchCheck->setChecked(settings.resumeOnLaunch); // 填启动时恢复壁纸
    ui->startMinimizedCheck->setChecked(settings.startMinimized); // 填启动后最小化
    ui->showDiagnosticsCheck->setChecked(settings.showDiagnostics); // 填是否显示诊断
    ui->monitorAllRadio->setChecked(settings.monitorMode == 0); // 模式 0 选所有显示器
    ui->monitorPrimaryRadio->setChecked(settings.monitorMode == 1); // 模式 1 选仅主显示器
    ui->monitorSpecificRadio->setChecked(settings.monitorMode == 2); // 模式 2 选指定显示器
    ui->monitorList->setEnabled(settings.monitorMode == 2); // 仅指定模式启用列表
    const int monitorRow = [&] { // 查找与设置匹配的显示器行
        for (int i = 0; i < ui->monitorList->count(); ++i) { // 逐行比较设备名
            if (ui->monitorList->item(i)->data(Qt::UserRole).toString().compare(settings.monitorDevice, Qt::CaseInsensitive) == // 忽略大小写比较设备名
                0) { // 比较结果是否相等
                return i; // 找到则返回该行
            } // 结束相等判断
        } // 结束查找循环
        return 0; // 找不到则用第一行
    }(); // 结束查找表达式
    if (ui->monitorList->count() > 0) { // 列表非空才设置当前行
        ui->monitorList->setCurrentRow(monitorRow); // 选中匹配的显示器
    } // 结束选中行
    ui->modeCombo->setCurrentIndex(static_cast<int>(settings.ocean.renderMode)); // 同步画面来源
    ui->settingsThemeCombo->setCurrentIndex(static_cast<int>(settings.ocean.theme)); // 同步主题
    ui->animSpeedSlider->setValue(std::clamp(static_cast<int>(std::lround(settings.ocean.animSpeed * 100.0f)), 25, 200)); // 波浪速度夹进滑条范围
    ui->settingsWaveSlider->setValue(std::clamp(static_cast<int>(std::lround(settings.ocean.waveStrength * 100.0f)), 20, 200)); // 波浪强度夹进滑条范围
    ui->windSlider->setValue(std::clamp(static_cast<int>(std::lround(settings.ocean.windStrength * 100.0f)), 0, 200)); // 风力强度夹进滑条范围
    ui->windAngleSlider->setValue(std::clamp(static_cast<int>(std::lround(settings.ocean.windAngle)), -180, 180)); // 风向夹进正负 180 度
    ui->sunAzimuthSlider->setValue(std::clamp(static_cast<int>(std::lround(settings.ocean.sunAzimuth)), -70, 70)); // 太阳水平位置夹进滑条范围
    ui->sunElevationSlider->setValue(std::clamp(static_cast<int>(std::lround(settings.ocean.sunElevation)), 2, 35)); // 太阳高度夹进滑条范围
    ui->sunIntensitySlider->setValue(std::clamp(static_cast<int>(std::lround(settings.ocean.sunIntensity * 100.0f)), 20, 180)); // 直射阳光夹进滑条范围
    ui->brightnessSlider->setValue(std::clamp(static_cast<int>(std::lround(settings.ocean.brightness * 100.0f)), 40, 160)); // 海面亮度夹进滑条范围
    ui->sunSlider->setValue(std::clamp(static_cast<int>(std::lround(settings.ocean.sunReflection * 100.0f)), 0, 200)); // 海面反射夹进滑条范围
    ui->highlightSlider->setValue(std::clamp(static_cast<int>(std::lround(settings.ocean.highlightWidth * 100.0f)), 35, 220)); // 高光宽度夹进滑条范围
    ui->skySaturationSlider->setValue(std::clamp(static_cast<int>(std::lround(settings.ocean.skySaturation * 100.0f)), 40, 160)); // 天空饱和度夹进滑条范围
    ui->skyBrightnessSlider->setValue(std::clamp(static_cast<int>(std::lround(settings.ocean.skyBrightness * 100.0f)), 60, 140)); // 天空亮度夹进滑条范围
    ui->hazeSlider->setValue(std::clamp(static_cast<int>(std::lround(settings.ocean.atmosphereHaze * 100.0f)), 0, 160)); // 大气雾化夹进滑条范围
    ui->sunDiskSlider->setValue(std::clamp(static_cast<int>(std::lround(settings.ocean.sunDisk * 100.0f)), 0, 250)); // 太阳盘亮度夹进滑条范围
    ui->sunHaloSlider->setValue(std::clamp(static_cast<int>(std::lround(settings.ocean.sunHalo * 100.0f)), 0, 200)); // 光晕强度夹进滑条范围
    ui->sunHaloRadiusSlider->setValue(std::clamp(static_cast<int>(std::lround(settings.ocean.sunHaloRadius * 100.0f)), 15, 250)); // 光晕半径夹进滑条范围
    ui->horizonHazeSlider->setValue(std::clamp(static_cast<int>(std::lround(settings.ocean.horizonHaze * 100.0f)), 0, 160)); // 海平线雾化夹进滑条范围
    const OceanPalette palette = OceanParams::paletteFor(settings.ocean.theme); // 取该主题的默认调色板
    paintColorButton(ui->waterColorButton, colorFromVector(settings.ocean.customWater ? settings.ocean.waterColor : palette.waterShallow), // 海面按钮用自定义色或主题浅水色
                     settings.ocean.customWater); // 传入是否自定义海面色
    paintColorButton(ui->skyColorButton, colorFromVector(settings.ocean.customSky ? settings.ocean.skyColor : palette.skyHorizon), // 天空按钮用自定义色或主题地平线色
                     settings.ocean.customSky); // 传入是否自定义天空色
    ui->videoPathEdit->setText(settings.ocean.videoPath); // 填入视频路径
    ui->videoLoopCheck->setChecked(settings.ocean.videoLoop); // 填入是否循环
    ui->videoMuteCheck->setChecked(settings.ocean.videoMuted); // 填入是否静音
    ui->settingsQualityCombo->setCurrentIndex(settings.ocean.quality); // 同步画质
    ui->settingsFpsCombo->setCurrentIndex(settings.ocean.targetFps >= 60 ? 1 : 0); // 60 帧选第二项，否则第一项
    ui->pauseFullscreenCheck->setChecked(settings.pauseOnFullscreen); // 同步全屏暂停
    ui->batteryCombo->setCurrentIndex(settings.batteryPolicy); // 同步电池策略
    const int languageIndex = ui->languageCombo->findData(settings.language); // 按语言代码查找项
    ui->languageCombo->setCurrentIndex(languageIndex < 0 ? 0 : languageIndex); // 找不到则选第一项
    ui->autostartCheck->setChecked(settings.launchAtStartup); // 同步开机启动
    ui->autostartPathLabel->setText(tr("将写入的命令：\n%1").arg(commandLineForExecutable(QCoreApplication::applicationFilePath()))); // 显示将写入注册表的命令行
    ui->aboutText->setText(tr("OceanLive %1\n\n实时 Gerstner 海洋，或把本地视频画进同一个桌面壁纸窗口。\n" // 关于页正文的第一段
                              "Qt %2\n配置文件：\n%3\n\nOpenGL 供应商、渲染器和帧时间显示在主窗口诊断区。" // 关于页的界面库与诊断说明
                              "获取不到的指标会标记为不可用。显存只在驱动提供 NVX 查询时显示。") // 关于页的不可用指标与显存说明
                               .arg(QString::fromLatin1(kAppVersion), QString::fromLatin1(QT_VERSION_STR), m_settings->configPath())); // 填入版本、界面库版本和配置路径
    updateValueLabels(); // 按滑条刷新旁边的数值
    m_loading = false; // 回填结束，允许写回
} // 结束填入设置

void SettingsDialog::updateValueLabels() { // 按滑条刷新数值标签
    ui->animSpeedLabel->setText(factorText(ui->animSpeedSlider->value() / 100.0f) + QLatin1Char('x')); // 波浪速度显示为带后缀的倍率
    ui->settingsWaveLabel->setText(factorText(ui->settingsWaveSlider->value() / 100.0f)); // 显示波浪强度
    ui->windLabel->setText(factorText(ui->windSlider->value() / 100.0f)); // 显示风力强度
    ui->windAngleLabel->setText(degreeText(ui->windAngleSlider->value())); // 显示风向角度
    ui->sunAzimuthLabel->setText(degreeText(ui->sunAzimuthSlider->value())); // 显示太阳水平角度
    ui->sunElevationLabel->setText(degreeText(ui->sunElevationSlider->value())); // 显示太阳高度角度
    ui->sunIntensityLabel->setText(factorText(ui->sunIntensitySlider->value() / 100.0f)); // 显示直射阳光倍率
    ui->brightnessLabel->setText(factorText(ui->brightnessSlider->value() / 100.0f)); // 显示海面亮度
    ui->sunLabel->setText(factorText(ui->sunSlider->value() / 100.0f)); // 显示海面反射
    ui->highlightLabel->setText(factorText(ui->highlightSlider->value() / 100.0f)); // 显示高光宽度
    ui->skySaturationLabel->setText(factorText(ui->skySaturationSlider->value() / 100.0f)); // 显示天空饱和度
    ui->skyBrightnessLabel->setText(factorText(ui->skyBrightnessSlider->value() / 100.0f)); // 显示天空亮度
    ui->hazeLabel->setText(factorText(ui->hazeSlider->value() / 100.0f)); // 显示大气雾化
    ui->sunDiskLabel->setText(factorText(ui->sunDiskSlider->value() / 100.0f)); // 显示太阳盘亮度
    ui->sunHaloLabel->setText(factorText(ui->sunHaloSlider->value() / 100.0f)); // 显示光晕强度
    ui->sunHaloRadiusLabel->setText(factorText(ui->sunHaloRadiusSlider->value() / 100.0f)); // 显示光晕半径
    ui->horizonHazeLabel->setText(factorText(ui->horizonHazeSlider->value() / 100.0f)); // 显示海平线雾化
} // 结束刷新数值

AppSettings SettingsDialog::collect() const { // 从控件收集一份设置
    AppSettings settings = m_settings->current(); // 以当前已存设置为底
    settings.resumeOnLaunch = ui->resumeOnLaunchCheck->isChecked(); // 读取启动时恢复壁纸
    settings.startMinimized = ui->startMinimizedCheck->isChecked(); // 读取启动后最小化
    settings.showDiagnostics = ui->showDiagnosticsCheck->isChecked(); // 读取是否显示诊断
    if (ui->monitorSpecificRadio->isChecked()) { // 选了指定显示器
        settings.monitorMode = 2; // 监视模式设为指定设备
        if (QListWidgetItem* item = ui->monitorList->currentItem()) { // 有当前列表项才读设备
            settings.monitorDevice = item->data(Qt::UserRole).toString(); // 记下该显示器设备名
        } // 结束读取当前项
    } else if (ui->monitorPrimaryRadio->isChecked()) { // 否则若选了主显示器
        settings.monitorMode = 1; // 监视模式设为主显示器
    } else { // 其余情况
        settings.monitorMode = 0; // 监视模式设为全部显示器
    } // 结束显示器分支
    settings.ocean.renderMode = renderModeFromInt(ui->modeCombo->currentIndex()); // 读取画面来源
    settings.ocean.theme = oceanThemeFromInt(ui->settingsThemeCombo->currentIndex()); // 读取主题
    settings.ocean.animSpeed = ui->animSpeedSlider->value() / 100.0f; // 波浪速度滑条换成倍率
    settings.ocean.waveStrength = ui->settingsWaveSlider->value() / 100.0f; // 波浪强度滑条换成倍率
    settings.ocean.windStrength = ui->windSlider->value() / 100.0f; // 风力滑条换成倍率
    settings.ocean.windAngle = static_cast<float>(ui->windAngleSlider->value()); // 风向滑条换成角度
    settings.ocean.sunAzimuth = static_cast<float>(ui->sunAzimuthSlider->value()); // 太阳水平滑条换成角度
    settings.ocean.sunElevation = static_cast<float>(ui->sunElevationSlider->value()); // 太阳高度滑条换成角度
    settings.ocean.sunIntensity = ui->sunIntensitySlider->value() / 100.0f; // 直射阳光滑条换成倍率
    settings.ocean.brightness = ui->brightnessSlider->value() / 100.0f; // 海面亮度滑条换成倍率
    settings.ocean.sunReflection = ui->sunSlider->value() / 100.0f; // 海面反射滑条换成倍率
    settings.ocean.highlightWidth = ui->highlightSlider->value() / 100.0f; // 高光宽度滑条换成倍率
    settings.ocean.skySaturation = ui->skySaturationSlider->value() / 100.0f; // 天空饱和度滑条换成倍率
    settings.ocean.skyBrightness = ui->skyBrightnessSlider->value() / 100.0f; // 天空亮度滑条换成倍率
    settings.ocean.atmosphereHaze = ui->hazeSlider->value() / 100.0f; // 大气雾化滑条换成倍率
    settings.ocean.sunDisk = ui->sunDiskSlider->value() / 100.0f; // 太阳盘亮度滑条换成倍率
    settings.ocean.sunHalo = ui->sunHaloSlider->value() / 100.0f; // 光晕强度滑条换成倍率
    settings.ocean.sunHaloRadius = ui->sunHaloRadiusSlider->value() / 100.0f; // 光晕半径滑条换成倍率
    settings.ocean.horizonHaze = ui->horizonHazeSlider->value() / 100.0f; // 海平线雾化滑条换成倍率
    settings.ocean.customWater = ui->waterColorButton->text() != tr("主题颜色"); // 与译文「主题颜色」比较，不同则视为自定义海面色
    settings.ocean.customSky = ui->skyColorButton->text() != tr("主题颜色"); // 与译文「主题颜色」比较，不同则视为自定义天空色
    if (settings.ocean.customWater) { // 海面色是自定义时才读取色值
        settings.ocean.waterColor = vectorFromColor(QColor(ui->waterColorButton->property("rgb").toString())); // 从按钮属性还原海面颜色
    } // 结束自定义海面色
    if (settings.ocean.customSky) { // 天空色是自定义时才读取色值
        settings.ocean.skyColor = vectorFromColor(QColor(ui->skyColorButton->property("rgb").toString())); // 从按钮属性还原天空颜色
    } // 结束自定义天空色
    settings.ocean.videoPath = ui->videoPathEdit->text().trimmed(); // 读取去掉空白的视频路径
    settings.ocean.videoLoop = ui->videoLoopCheck->isChecked(); // 读取是否循环播放
    settings.ocean.videoMuted = ui->videoMuteCheck->isChecked(); // 读取是否静音
    settings.ocean.quality = ui->settingsQualityCombo->currentIndex(); // 读取画质档位
    settings.ocean.targetFps = ui->settingsFpsCombo->currentIndex() == 0 ? 30 : 60; // 第一项为 30 帧，否则 60 帧
    settings.pauseOnFullscreen = ui->pauseFullscreenCheck->isChecked(); // 读取全屏暂停开关
    settings.batteryPolicy = ui->batteryCombo->currentIndex(); // 读取电池策略
    settings.launchAtStartup = ui->autostartCheck->isChecked(); // 读取开机启动开关
    settings.language = ui->languageCombo->currentData().toString(); // 读取语言代码
    return settings; // 返回收集到的设置
} // 结束收集

void SettingsDialog::pushLive() { // 把控件即时写入设置
    if (m_loading) { // 回填中不写回
        return; // 直接返回
    } // 结束回填判断
    updateValueLabels(); // 先更新滑条旁数值
    QString error; // 接收写入失败信息
    m_settings->apply(collect(), &error); // 应用收集到的设置
} // 结束即时写回

void SettingsDialog::revert() { // 取消时恢复快照
    QString error; // 接收恢复失败信息
    m_settings->apply(m_snapshot, &error); // 把打开时的设置写回去
} // 结束恢复快照
