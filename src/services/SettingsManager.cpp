// SPDX-License-Identifier: Apache-2.0
#include "services/SettingsManager.h" // 设置管理声明

#include "platform/win32/Autostart.h" // 开机启动与 ini 对齐
#include "services/AppLanguage.h" // 语言值规范化和安装翻译器
#include "utils/Logger.h" // 记录配置路径和开机启动失败

#include <QColor> // 颜色在 ini 里存成十六进制
#include <QDir> // 创建配置目录
#include <QSettings> // ini 读写
#include <QStandardPaths> // 配置目录位置

#include <algorithm> // clamp

namespace { // 读整数和颜色的小函数

int readInt(QSettings& store, const char* key, int fallback, int lo, int hi) { // 读整数并夹在范围内
    bool ok = false; // 转换是否成功
    const int value = store.value(QLatin1String(key), fallback).toInt(&ok); // 缺键时用默认
    if (!ok) { // 不是整数
        return fallback; // 退回默认
    } // 结束非法值
    return std::clamp(value, lo, hi); // 夹到闭区间
} // 结束读整数

QVector3D readColor(QSettings& store, const char* key, const QVector3D& fallback) { // 读 #RRGGBB
    const QColor color(store.value(QLatin1String(key)).toString()); // 空串或坏串会无效
    if (!color.isValid()) { // 没有合法颜色
        return fallback; // 用调色板默认
    } // 结束无效颜色
    return QVector3D(static_cast<float>(color.redF()), static_cast<float>(color.greenF()), static_cast<float>(color.blueF())); // 转到 0 到 1
} // 结束读颜色

QString writeColor(const QVector3D& color) { // 写成 #RRGGBB
    const QColor value = QColor::fromRgbF(std::clamp(color.x(), 0.0f, 1.0f), std::clamp(color.y(), 0.0f, 1.0f), // 红和绿夹到 0 到 1
                                          std::clamp(color.z(), 0.0f, 1.0f)); // 蓝也夹住
    return value.name(QColor::HexRgb); // 十六进制，不翻译
} // 结束写颜色

}  // 结束匿名命名空间

SettingsManager::SettingsManager(QObject* parent) : QObject(parent) { // 打开 ini
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation); // 用户配置目录
    QDir().mkpath(directory); // 没有就创建
    m_store = new QSettings(directory + QStringLiteral("/OceanLive.ini"), QSettings::IniFormat, this); // 固定文件名
    read(); // 读入内存
    QString error; // 开机启动失败时的说明
    if (!Autostart::setEnabled(m_current.launchAtStartup, &error) && !error.isEmpty()) { // 让注册表跟上 ini
        Logger::warn(QStringLiteral("Settings"), error); // 失败只记日志
    } // 结束同步开机启动
    m_current.launchAtStartup = Autostart::isEnabled(); // 以注册表实际结果为准
    write(m_current); // 把校正后的值写回
    Logger::info(QStringLiteral("Settings"), QStringLiteral("配置文件 %1").arg(m_store->fileName())); // 日志不进翻译表
} // 结束构造

AppSettings SettingsManager::current() const { // 界面读取
    return m_current; // 返回副本
} // 结束取当前设置

QString SettingsManager::configPath() const { // 关于页显示
    return m_store->fileName(); // ini 完整路径
} // 结束取路径

AppSettings SettingsManager::sanitize(AppSettings settings) const { // 写入前收紧范围
    settings.ocean = OceanParams::clamped(settings.ocean); // 海洋各项夹紧
    settings.monitorMode = std::clamp(settings.monitorMode, 0, 2); // 三种显示器模式
    settings.batteryPolicy = std::clamp(settings.batteryPolicy, 0, 2); // 三种电池策略
    settings.monitorDevice = settings.monitorDevice.trimmed(); // 去掉设备名空白
    settings.language = AppLanguage::normalizePreference(settings.language); // 只留 system、zh_CN、en
    return settings; // 返回合法副本
} // 结束校验

void SettingsManager::read() { // 从 ini 填设置
    AppSettings settings; // 先用结构体默认值
    settings.ocean.theme = oceanThemeFromInt(readInt(*m_store, "ocean/theme", 0, 0, 2)); // 主题
    settings.ocean.animSpeed = m_store->value(QStringLiteral("ocean/animSpeed"), 1.0).toFloat(); // 动画速度
    settings.ocean.waveStrength = m_store->value(QStringLiteral("ocean/waveStrength"), 1.0).toFloat(); // 浪高
    settings.ocean.windStrength = m_store->value(QStringLiteral("ocean/windStrength"), 1.0).toFloat(); // 风力
    settings.ocean.brightness = m_store->value(QStringLiteral("ocean/brightness"), 1.0).toFloat(); // 亮度
    settings.ocean.sunReflection = m_store->value(QStringLiteral("ocean/sunReflection"), 1.0).toFloat(); // 海面高光
    settings.ocean.sunIntensity = m_store->value(QStringLiteral("ocean/sunIntensity"), 1.0).toFloat(); // 太阳强度
    settings.ocean.highlightWidth = m_store->value(QStringLiteral("ocean/highlightWidth"), 1.0).toFloat(); // 高光宽度
    settings.ocean.quality = readInt(*m_store, "ocean/quality", 1, 0, 2); // 画质
    settings.ocean.targetFps = readInt(*m_store, "ocean/targetFps", 60, 30, 60); // 目标帧率
    settings.ocean.renderMode = renderModeFromInt(readInt(*m_store, "ocean/renderMode", 0, 0, 1)); // 实时或视频
    settings.ocean.videoPath = m_store->value(QStringLiteral("ocean/videoPath")).toString(); // 视频路径
    settings.ocean.videoLoop = m_store->value(QStringLiteral("ocean/videoLoop"), true).toBool(); // 循环
    settings.ocean.videoMuted = m_store->value(QStringLiteral("ocean/videoMuted"), true).toBool(); // 静音
    settings.ocean.customWater = m_store->value(QStringLiteral("ocean/customWater"), false).toBool(); // 自定义海水
    settings.ocean.customSky = m_store->value(QStringLiteral("ocean/customSky"), false).toBool(); // 自定义天空
    const OceanPalette palette = OceanParams::paletteFor(settings.ocean.theme); // 缺键时用主题默认
    const bool hasClearSky = m_store->contains(QStringLiteral("ocean/atmosphereHaze")); // 有薄雾键才算新配置
    if (m_store->contains(QStringLiteral("ocean/sunAzimuth")) && hasClearSky) { // 新配置保留用户角度
        settings.ocean.sunAzimuth = m_store->value(QStringLiteral("ocean/sunAzimuth"), palette.sunAzimuth).toFloat(); // 方位角
        settings.ocean.sunElevation = m_store->value(QStringLiteral("ocean/sunElevation"), palette.sunElevation).toFloat(); // 高度角
        settings.ocean.windAngle = m_store->value(QStringLiteral("ocean/windAngle"), palette.windAngle).toFloat(); // 风向
    } else if (m_store->contains(QStringLiteral("ocean/sunAzimuth"))) { // 旧配置有方位角但没有薄雾键
        settings.ocean.windAngle = m_store->value(QStringLiteral("ocean/windAngle"), palette.windAngle).toFloat(); // 风向仍保留
        settings.ocean.sunAzimuth = palette.sunAzimuth; // 太阳角度改用主题默认
        settings.ocean.sunElevation = palette.sunElevation; // 高度角也用默认
    } else { // 全新配置
        settings.ocean.sunAzimuth = palette.sunAzimuth; // 默认方位角
        settings.ocean.sunElevation = palette.sunElevation; // 默认高度角
        settings.ocean.windAngle = palette.windAngle; // 默认风向
    } // 结束角度迁移
    auto readLook = [&](const char* key, float fallback) { // 没有该键就用调色板，不把 0 当成用户值
        return m_store->contains(QString::fromLatin1(key)) ? m_store->value(QString::fromLatin1(key), fallback).toFloat() : fallback; // 缺键用默认
    }; // 结束读取外观浮点
    settings.ocean.skySaturation = readLook("ocean/skySaturation", palette.skySaturation); // 天空饱和
    settings.ocean.skyBrightness = readLook("ocean/skyBrightness", palette.skyBrightness); // 天空亮度
    settings.ocean.atmosphereHaze = readLook("ocean/atmosphereHaze", palette.atmosphereHaze); // 大气薄雾
    settings.ocean.sunDisk = readLook("ocean/sunDisk", palette.sunDisk); // 日盘
    settings.ocean.sunHalo = readLook("ocean/sunHalo", palette.sunHalo); // 日晕
    settings.ocean.sunHaloRadius = readLook("ocean/sunHaloRadius", palette.sunHaloRadius); // 日晕半径
    settings.ocean.horizonHaze = readLook("ocean/horizonHaze", palette.horizonHaze); // 地平线薄雾
    settings.ocean.waterColor = readColor(*m_store, "ocean/waterColor", palette.waterShallow); // 海水颜色
    settings.ocean.skyColor = readColor(*m_store, "ocean/skyColor", palette.skyHorizon); // 天空颜色
    settings.monitorMode = readInt(*m_store, "display/mode", 0, 0, 2); // 显示器模式
    settings.monitorDevice = m_store->value(QStringLiteral("display/device")).toString(); // 指定设备
    settings.pauseOnFullscreen = m_store->value(QStringLiteral("performance/pauseOnFullscreen"), true).toBool(); // 全屏暂停
    settings.batteryPolicy = readInt(*m_store, "performance/batteryPolicy", 0, 0, 2); // 电池策略
    settings.launchAtStartup = m_store->value(QStringLiteral("startup/launchAtStartup"), false).toBool(); // 开机启动
    settings.startMinimized = m_store->value(QStringLiteral("startup/startMinimized"), false).toBool(); // 启动最小化
    settings.resumeOnLaunch = m_store->value(QStringLiteral("startup/resumeOnLaunch"), true).toBool(); // 启动时恢复壁纸
    settings.showDiagnostics = m_store->value(QStringLiteral("ui/showDiagnostics"), false).toBool(); // 显示诊断
    settings.language = AppLanguage::normalizePreference(m_store->value(QStringLiteral("ui/language")).toString()); // 缺键为跟随系统
    m_current = sanitize(settings); // 夹紧后成为当前设置
} // 结束读取

void SettingsManager::write(const AppSettings& settings) { // 整份写回 ini
    m_store->setValue(QStringLiteral("ocean/theme"), static_cast<int>(settings.ocean.theme)); // 主题序号
    m_store->setValue(QStringLiteral("ocean/animSpeed"), settings.ocean.animSpeed); // 动画速度
    m_store->setValue(QStringLiteral("ocean/waveStrength"), settings.ocean.waveStrength); // 浪高
    m_store->setValue(QStringLiteral("ocean/windStrength"), settings.ocean.windStrength); // 风力
    m_store->setValue(QStringLiteral("ocean/brightness"), settings.ocean.brightness); // 亮度
    m_store->setValue(QStringLiteral("ocean/sunReflection"), settings.ocean.sunReflection); // 高光
    m_store->setValue(QStringLiteral("ocean/sunAzimuth"), settings.ocean.sunAzimuth); // 方位角
    m_store->setValue(QStringLiteral("ocean/sunElevation"), settings.ocean.sunElevation); // 高度角
    m_store->setValue(QStringLiteral("ocean/sunIntensity"), settings.ocean.sunIntensity); // 太阳强度
    m_store->setValue(QStringLiteral("ocean/highlightWidth"), settings.ocean.highlightWidth); // 高光宽度
    m_store->setValue(QStringLiteral("ocean/windAngle"), settings.ocean.windAngle); // 风向
    m_store->remove(QStringLiteral("ocean/cloudStrength")); // 删掉已取消的云层键
    m_store->setValue(QStringLiteral("ocean/skySaturation"), settings.ocean.skySaturation); // 天空饱和
    m_store->setValue(QStringLiteral("ocean/skyBrightness"), settings.ocean.skyBrightness); // 天空亮度
    m_store->setValue(QStringLiteral("ocean/atmosphereHaze"), settings.ocean.atmosphereHaze); // 大气薄雾
    m_store->setValue(QStringLiteral("ocean/sunDisk"), settings.ocean.sunDisk); // 日盘
    m_store->setValue(QStringLiteral("ocean/sunHalo"), settings.ocean.sunHalo); // 日晕
    m_store->setValue(QStringLiteral("ocean/sunHaloRadius"), settings.ocean.sunHaloRadius); // 日晕半径
    m_store->setValue(QStringLiteral("ocean/horizonHaze"), settings.ocean.horizonHaze); // 地平线薄雾
    m_store->setValue(QStringLiteral("ocean/customWater"), settings.ocean.customWater); // 自定义海水
    m_store->setValue(QStringLiteral("ocean/customSky"), settings.ocean.customSky); // 自定义天空
    m_store->setValue(QStringLiteral("ocean/waterColor"), writeColor(settings.ocean.waterColor)); // 海水十六进制
    m_store->setValue(QStringLiteral("ocean/skyColor"), writeColor(settings.ocean.skyColor)); // 天空十六进制
    m_store->setValue(QStringLiteral("ocean/quality"), settings.ocean.quality); // 画质
    m_store->setValue(QStringLiteral("ocean/targetFps"), settings.ocean.targetFps); // 帧率
    m_store->setValue(QStringLiteral("ocean/renderMode"), static_cast<int>(settings.ocean.renderMode)); // 画面来源
    m_store->setValue(QStringLiteral("ocean/videoPath"), settings.ocean.videoPath); // 视频路径
    m_store->setValue(QStringLiteral("ocean/videoLoop"), settings.ocean.videoLoop); // 循环
    m_store->setValue(QStringLiteral("ocean/videoMuted"), settings.ocean.videoMuted); // 静音
    m_store->setValue(QStringLiteral("display/mode"), settings.monitorMode); // 显示器模式
    m_store->setValue(QStringLiteral("display/device"), settings.monitorDevice); // 设备名
    m_store->setValue(QStringLiteral("performance/pauseOnFullscreen"), settings.pauseOnFullscreen); // 全屏暂停
    m_store->setValue(QStringLiteral("performance/batteryPolicy"), settings.batteryPolicy); // 电池策略
    m_store->setValue(QStringLiteral("startup/launchAtStartup"), settings.launchAtStartup); // 开机启动
    m_store->setValue(QStringLiteral("startup/startMinimized"), settings.startMinimized); // 启动最小化
    m_store->setValue(QStringLiteral("startup/resumeOnLaunch"), settings.resumeOnLaunch); // 启动恢复
    m_store->setValue(QStringLiteral("ui/showDiagnostics"), settings.showDiagnostics); // 诊断开关
    m_store->setValue(QStringLiteral("ui/language"), settings.language); // 界面语言
    m_store->sync(); // 立刻写盘
} // 结束写入

bool SettingsManager::apply(const AppSettings& settings, QString* error) { // 应用一份新设置
    AppSettings next = sanitize(settings); // 先夹紧
    if (next.launchAtStartup != m_current.launchAtStartup) { // 开机启动有变化
        QString autostartError; // 注册表失败说明
        if (!Autostart::setEnabled(next.launchAtStartup, &autostartError)) { // 写 Run 键失败
            if (error) { // 调用方要文本
                *error = autostartError; // 交回去
            } // 结束填写错误
            emit errorOccurred(autostartError); // 界面也可以听
            next.launchAtStartup = m_current.launchAtStartup; // 保持原开关
        } else { // 注册表写成功
            next.launchAtStartup = Autostart::isEnabled(); // 再读一次实际值
        } // 结束成功
    } // 结束开机启动
    const QString previousLanguage = m_current.language; // 用来判断要不要换翻译器
    const bool same = oceanParamsEqual(next.ocean, m_current.ocean) && next.monitorMode == m_current.monitorMode && // 海洋和显示器模式
                      next.monitorDevice == m_current.monitorDevice && next.pauseOnFullscreen == m_current.pauseOnFullscreen && // 设备和全屏暂停
                      next.batteryPolicy == m_current.batteryPolicy && next.launchAtStartup == m_current.launchAtStartup && // 电池和开机启动
                      next.startMinimized == m_current.startMinimized && next.resumeOnLaunch == m_current.resumeOnLaunch && // 最小化和恢复
                      next.showDiagnostics == m_current.showDiagnostics && next.language == previousLanguage; // 诊断和语言也算变化
    m_current = next; // 先换成新值
    write(m_current); // 整份落盘
    if (next.language != previousLanguage) { // 只改语言也要换目录
        AppLanguage::install(m_current.language); // 在 changed 之前安装，窗口能收到语言事件
    } // 结束换语言
    if (!same) { // 至少有一项不同
        emit changed(); // 托盘和主窗口刷新
    } // 结束通知
    return !error || error->isEmpty(); // 没有错误指针，或错误仍为空
} // 结束应用
