// SPDX-License-Identifier: Apache-2.0
#include "render/OceanParams.h" // 海洋参数声明

#include <QCoreApplication> // 主题名走 translate，测试不装翻译器时仍是中文

#include <algorithm> // clamp
#include <cmath> // 角度转弧度和有限值判断

namespace { // 夹紧浮点和颜色

float sanitize(float value, float lo, float hi, float fallback) { // 非有限值用默认，其余夹紧
    if (!std::isfinite(value)) { // NaN 或无穷
        return fallback; // 不用坏值
    } // 结束非有限
    return std::clamp(value, lo, hi); // 收到闭区间
} // 结束夹紧浮点

QVector3D sanitizeColor(QVector3D color, const QVector3D& fallback) { // 颜色各分量要在 0 到 1
    if (!std::isfinite(color.x()) || !std::isfinite(color.y()) || !std::isfinite(color.z())) { // 任一通道坏了
        return fallback; // 整色退回默认
    } // 结束坏颜色
    return QVector3D(std::clamp(color.x(), 0.0f, 1.0f), std::clamp(color.y(), 0.0f, 1.0f), // 红和绿
                     std::clamp(color.z(), 0.0f, 1.0f)); // 蓝
} // 结束夹紧颜色

float radians(float degrees) { // 度转弧度
    return degrees * 0.01745329252f; // π/180 的近似
} // 结束转弧度

}  // 结束匿名命名空间

OceanParams OceanParams::clamped(OceanParams value) { // 返回夹紧后的副本
    const int theme = std::clamp(static_cast<int>(value.theme), 0, 2); // 主题只有三个
    value.theme = static_cast<OceanTheme>(theme); // 写回枚举
    value.animSpeed = sanitize(value.animSpeed, 0.25f, 2.0f, 1.0f); // 动画速度
    value.waveStrength = sanitize(value.waveStrength, 0.2f, 2.0f, 1.0f); // 浪高
    value.windStrength = sanitize(value.windStrength, 0.0f, 2.0f, 1.0f); // 风力
    value.brightness = sanitize(value.brightness, 0.4f, 1.6f, 1.0f); // 亮度
    value.sunReflection = sanitize(value.sunReflection, 0.0f, 2.0f, 1.0f); // 海面高光
    value.sunAzimuth = sanitize(value.sunAzimuth, -70.0f, 70.0f, 26.0f); // 方位角
    value.sunElevation = sanitize(value.sunElevation, 2.0f, 35.0f, 18.0f); // 高度角
    value.sunIntensity = sanitize(value.sunIntensity, 0.2f, 1.8f, 1.0f); // 太阳强度
    value.highlightWidth = sanitize(value.highlightWidth, 0.35f, 2.2f, 1.0f); // 高光宽度
    value.windAngle = sanitize(value.windAngle, -180.0f, 180.0f, 8.0f); // 风向
    value.skySaturation = sanitize(value.skySaturation, 0.4f, 1.6f, 1.08f); // 天空饱和
    value.skyBrightness = sanitize(value.skyBrightness, 0.6f, 1.4f, 1.02f); // 天空亮度
    value.atmosphereHaze = sanitize(value.atmosphereHaze, 0.0f, 1.6f, 0.08f); // 大气薄雾
    value.sunDisk = sanitize(value.sunDisk, 0.0f, 2.5f, 1.30f); // 日盘
    value.sunHalo = sanitize(value.sunHalo, 0.0f, 2.0f, 0.32f); // 日晕
    value.sunHaloRadius = sanitize(value.sunHaloRadius, 0.15f, 2.5f, 0.48f); // 日晕半径
    value.horizonHaze = sanitize(value.horizonHaze, 0.0f, 1.6f, 0.08f); // 地平线薄雾
    value.waterColor = sanitizeColor(value.waterColor, QVector3D(0.05f, 0.38f, 0.64f)); // 浅水默认色
    value.skyColor = sanitizeColor(value.skyColor, QVector3D(0.40f, 0.64f, 0.91f)); // 地平线默认色
    value.quality = std::clamp(value.quality, 0, 2); // 低中高
    if (value.targetFps != 30 && value.targetFps != 60) { // 只允许这两档
        value.targetFps = value.targetFps < 45 ? 30 : 60; // 靠近哪一档用哪一档
    } // 结束帧率
    value.renderMode = renderModeFromInt(static_cast<int>(value.renderMode)); // 非法来源变成实时
    value.videoPath = value.videoPath.trimmed(); // 去掉路径空白
    return value; // 夹紧后的参数
} // 结束夹紧

OceanPalette OceanParams::paletteFor(OceanTheme theme) { // 主题默认外观
    OceanPalette palette; // 先用结构体默认，再按主题覆盖
    switch (theme) { // 三个主题各一套
    case OceanTheme::Sunset: // 日落
        palette.skyZenith = QVector3D(0.16f, 0.10f, 0.32f); // 日落天顶
        palette.skyHorizon = QVector3D(0.86f, 0.42f, 0.24f); // 日落地平线
        palette.waterDeep = QVector3D(0.05f, 0.03f, 0.10f); // 日落深水
        palette.waterShallow = QVector3D(0.42f, 0.16f, 0.10f); // 日落浅水
        palette.sunColor = QVector3D(1.0f, 0.62f, 0.28f); // 暖色太阳
        palette.sunDir = QVector3D(0.12f, 0.08f, 0.99f); // 调色板参考方向
        palette.foam = 0.22f; // 泡沫
        palette.waveScale = 0.72f; // 浪尺度
        palette.speedScale = 0.70f; // 速度
        palette.sunGain = 0.85f; // 太阳增益
        palette.sunAzimuth = 10.0f; // 方位角
        palette.sunElevation = 6.0f; // 低高度角
        palette.windAngle = 2.0f; // 风向
        palette.skySaturation = 1.02f; // 饱和
        palette.skyBrightness = 0.98f; // 亮度
        palette.atmosphereHaze = 0.92f; // 日落雾更重
        palette.sunDisk = 1.15f; // 日盘
        palette.sunHalo = 1.05f; // 日晕
        palette.sunHaloRadius = 1.45f; // 日晕半径
        palette.horizonHaze = 0.82f; // 地平线雾
        break; // 结束日落
    case OceanTheme::Deep: // 深蓝
        palette.skyZenith = QVector3D(0.015f, 0.04f, 0.10f); // 夜空天顶
        palette.skyHorizon = QVector3D(0.05f, 0.12f, 0.22f); // 夜空地平线
        palette.waterDeep = QVector3D(0.006f, 0.02f, 0.055f); // 深水
        palette.waterShallow = QVector3D(0.015f, 0.08f, 0.16f); // 浅水
        palette.sunColor = QVector3D(0.70f, 0.78f, 0.92f); // 冷色光
        palette.sunDir = QVector3D(-0.35f, 0.42f, 0.84f); // 调色板参考方向
        palette.foam = 0.16f; // 泡沫
        palette.waveScale = 0.90f; // 浪尺度
        palette.speedScale = 0.82f; // 速度
        palette.sunGain = 0.48f; // 太阳增益偏低
        palette.sunAzimuth = -22.0f; // 方位角
        palette.sunElevation = 14.0f; // 高度角
        palette.windAngle = -12.0f; // 风向
        palette.skySaturation = 0.85f; // 饱和偏低
        palette.skyBrightness = 0.78f; // 亮度偏低
        palette.atmosphereHaze = 0.40f; // 薄雾
        palette.sunDisk = 0.45f; // 日盘较弱
        palette.sunHalo = 0.20f; // 日晕较弱
        palette.sunHaloRadius = 0.90f; // 日晕半径
        palette.horizonHaze = 0.38f; // 地平线雾
        break; // 结束深蓝
    case OceanTheme::Sunny: // 晴日
    default: // 未知主题也用晴日
        palette.skyZenith = QVector3D(0.18f, 0.45f, 0.86f); // 晴空天顶
        palette.skyHorizon = QVector3D(0.40f, 0.64f, 0.91f); // 晴空地平线
        palette.waterDeep = QVector3D(0.02f, 0.16f, 0.34f); // 深水
        palette.waterShallow = QVector3D(0.05f, 0.38f, 0.64f); // 浅水
        palette.sunColor = QVector3D(1.0f, 0.97f, 0.90f); // 近白的阳光
        palette.sunDir = QVector3D(0.32f, 0.28f, -0.91f); // 参考方向朝负 Z
        palette.foam = 0.10f; // 泡沫较少
        palette.waveScale = 1.0f; // 浪尺度
        palette.speedScale = 1.0f; // 速度
        palette.sunGain = 1.0f; // 太阳增益
        palette.sunAzimuth = 26.0f; // 方位角
        palette.sunElevation = 18.0f; // 高度角
        palette.windAngle = 8.0f; // 风向
        palette.skySaturation = 1.08f; // 饱和
        palette.skyBrightness = 1.02f; // 亮度
        palette.atmosphereHaze = 0.08f; // 薄雾很淡
        palette.sunDisk = 1.30f; // 日盘
        palette.sunHalo = 0.32f; // 日晕
        palette.sunHaloRadius = 0.48f; // 日晕半径
        palette.horizonHaze = 0.08f; // 地平线雾很淡
        break; // 结束晴日
    } // 结束主题分支
    return palette; // 该主题的默认
} // 结束取调色板

int OceanParams::waveCountForQuality(int quality) { // 画质决定叠加几层浪
    switch (std::clamp(quality, 0, 2)) { // 先收成 0 到 2
    case 0: // 低
        return 5; // 五层
    case 2: // 高
        return 12; // 十二层
    default: // 中，以及未列出的值
        return 8; // 八层
    } // 结束画质
} // 结束浪层数

void OceanParams::meshSizeForQuality(int quality, int* columns, int* rows) { // 画质决定网格
    switch (std::clamp(quality, 0, 2)) { // 收成三档
    case 0: // 低
        *columns = 120; // 列
        *rows = 72; // 行
        break; // 结束低
    case 2: // 高
        *columns = 320; // 列
        *rows = 190; // 行
        break; // 结束高
    default: // 中
        *columns = 200; // 列
        *rows = 120; // 行
        break; // 结束中
    } // 结束网格分支
} // 结束网格尺寸

void OceanParams::applyThemeDefaults(OceanTheme theme) { // 换主题时覆盖外观，不动滑块
    const OceanPalette palette = paletteFor(theme); // 取该主题默认
    this->theme = theme; // 记下主题
    sunAzimuth = palette.sunAzimuth; // 方位角
    sunElevation = palette.sunElevation; // 高度角
    windAngle = palette.windAngle; // 风向
    skySaturation = palette.skySaturation; // 饱和
    skyBrightness = palette.skyBrightness; // 天空亮度
    atmosphereHaze = palette.atmosphereHaze; // 薄雾
    sunDisk = palette.sunDisk; // 日盘
    sunHalo = palette.sunHalo; // 日晕
    sunHaloRadius = palette.sunHaloRadius; // 日晕半径
    horizonHaze = palette.horizonHaze; // 地平线雾
    customWater = false; // 取消自定义海水
    customSky = false; // 取消自定义天空
    waterColor = palette.waterShallow; // 海水回到浅水默认
    skyColor = palette.skyHorizon; // 天空回到地平线默认
} // 结束套用主题

OceanParams OceanParams::withTheme(OceanTheme theme) const { // 复制后再换主题
    OceanParams next = *this; // 先完整复制
    next.applyThemeDefaults(theme); // 覆盖外观字段
    next.animSpeed = animSpeed; // 保留动画速度
    next.waveStrength = waveStrength; // 保留浪高
    next.windStrength = windStrength; // 保留风力
    next.brightness = brightness; // 保留亮度
    next.sunReflection = sunReflection; // 保留高光
    next.sunIntensity = sunIntensity; // 保留太阳强度
    next.highlightWidth = highlightWidth; // 保留高光宽度
    next.quality = quality; // 保留画质
    next.targetFps = targetFps; // 保留帧率
    next.renderMode = renderMode; // 保留画面来源
    next.videoPath = videoPath; // 保留视频路径
    next.videoLoop = videoLoop; // 保留循环
    next.videoMuted = videoMuted; // 保留静音
    return next; // 新参数，原对象不变
} // 结束带主题复制

QString oceanThemeName(OceanTheme theme) { // 界面上的主题名
    switch (theme) { // 按枚举
    case OceanTheme::Sunset: // 日落
        return QCoreApplication::translate("OceanParams", "日落海洋"); // 源字符串保持中文
    case OceanTheme::Deep: // 深蓝
        return QCoreApplication::translate("OceanParams", "深蓝海洋"); // 源字符串保持中文
    case OceanTheme::Sunny: // 晴日
    default: // 其他值也显示晴日
        return QCoreApplication::translate("OceanParams", "晴日大海"); // 源字符串保持中文
    } // 结束名称
} // 结束主题名

OceanTheme oceanThemeFromInt(int value) { // ini 里的整数
    return static_cast<OceanTheme>(std::clamp(value, 0, 2)); // 越界夹到合法主题
} // 结束整数转主题

QString renderModeName(RenderMode mode) { // 界面上的画面来源
    return mode == RenderMode::Video ? QCoreApplication::translate("OceanParams", "本地视频") // 视频
                                     : QCoreApplication::translate("OceanParams", "实时海洋"); // 其余是实时
} // 结束来源名

RenderMode renderModeFromInt(int value) { // ini 里的整数
    return value == static_cast<int>(RenderMode::Video) ? RenderMode::Video : RenderMode::Realtime; // 只有 1 是视频
} // 结束整数转来源

OceanPalette lerpPalette(const OceanPalette& a, const OceanPalette& b, float t) { // a 到 b 的混合
    const float w = std::clamp(t, 0.0f, 1.0f); // 权重收在 0 到 1
    auto mix3 = [w](const QVector3D& x, const QVector3D& y) { return x * (1.0f - w) + y * w; }; // 三维线性混合
    auto mix1 = [w](float x, float y) { return x * (1.0f - w) + y * w; }; // 标量线性混合
    OceanPalette out; // 结果
    out.skyZenith = mix3(a.skyZenith, b.skyZenith); // 天顶
    out.skyHorizon = mix3(a.skyHorizon, b.skyHorizon); // 地平线
    out.waterDeep = mix3(a.waterDeep, b.waterDeep); // 深水
    out.waterShallow = mix3(a.waterShallow, b.waterShallow); // 浅水
    out.sunColor = mix3(a.sunColor, b.sunColor); // 太阳颜色
    out.sunDir = mix3(a.sunDir, b.sunDir); // 参考方向
    out.foam = mix1(a.foam, b.foam); // 泡沫
    out.waveScale = mix1(a.waveScale, b.waveScale); // 浪尺度
    out.speedScale = mix1(a.speedScale, b.speedScale); // 速度
    out.sunGain = mix1(a.sunGain, b.sunGain); // 太阳增益
    out.sunAzimuth = mix1(a.sunAzimuth, b.sunAzimuth); // 方位角
    out.sunElevation = mix1(a.sunElevation, b.sunElevation); // 高度角
    out.windAngle = mix1(a.windAngle, b.windAngle); // 风向
    out.skySaturation = mix1(a.skySaturation, b.skySaturation); // 饱和
    out.skyBrightness = mix1(a.skyBrightness, b.skyBrightness); // 天空亮度
    out.atmosphereHaze = mix1(a.atmosphereHaze, b.atmosphereHaze); // 薄雾
    out.sunDisk = mix1(a.sunDisk, b.sunDisk); // 日盘
    out.sunHalo = mix1(a.sunHalo, b.sunHalo); // 日晕
    out.sunHaloRadius = mix1(a.sunHaloRadius, b.sunHaloRadius); // 日晕半径
    out.horizonHaze = mix1(a.horizonHaze, b.horizonHaze); // 地平线雾
    return out; // 混合结果
} // 结束混合

QVector3D viewSunDirection(float azimuthDeg, float elevationDeg) { // 视线空间的太阳方向
    const float azimuth = radians(azimuthDeg); // 方位角弧度
    const float elevation = radians(elevationDeg); // 高度角弧度
    const float horizontal = std::cos(elevation); // 水平分量长度
    // OpenGL 视线沿负 Z。Z 为正会把太阳放到摄像机背后。
    QVector3D direction(std::sin(azimuth) * horizontal, std::sin(elevation), -std::cos(azimuth) * horizontal); // Z 取负
    if (direction.lengthSquared() < 1.0e-8f) { // 方向退化
        return QVector3D(0.0f, 0.2f, 1.0f).normalized(); // 过短时的备用向量
    } // 结束退化
    return direction.normalized(); // 单位方向
} // 结束视线太阳

QVector3D worldSunDirection(const QMatrix4x4& view, float azimuthDeg, float elevationDeg) { // 世界空间太阳方向
    bool invertible = false; // 视图矩阵能否求逆
    const QMatrix4x4 inverse = view.inverted(&invertible); // 逆矩阵
    const QVector3D viewDirection = viewSunDirection(azimuthDeg, elevationDeg); // 先在视线空间算
    if (!invertible) { // 求逆失败
        return viewDirection; // 退回视线方向
    } // 结束无法求逆
    const QVector3D world = inverse.mapVector(viewDirection); // 只转方向，不平移
    if (world.lengthSquared() < 1.0e-8f) { // 变过去后过短
        return viewDirection; // 仍用视线方向
    } // 结束过短
    return world.normalized(); // 世界空间单位方向
} // 结束世界太阳

bool oceanParamsEqual(const OceanParams& a, const OceanParams& b) { // 判断两份海洋参数是否相同
    return a.theme == b.theme && a.animSpeed == b.animSpeed && a.waveStrength == b.waveStrength && // 主题、速度、浪高
           a.windStrength == b.windStrength && a.brightness == b.brightness && a.sunReflection == b.sunReflection && // 风、亮度、高光
           a.sunAzimuth == b.sunAzimuth && a.sunElevation == b.sunElevation && a.sunIntensity == b.sunIntensity && // 太阳角度和强度
           a.highlightWidth == b.highlightWidth && a.windAngle == b.windAngle && // 高光宽度和风向
           a.skySaturation == b.skySaturation && a.skyBrightness == b.skyBrightness && // 天空饱和和亮度
           a.atmosphereHaze == b.atmosphereHaze && a.sunDisk == b.sunDisk && a.sunHalo == b.sunHalo && // 薄雾、日盘、日晕
           a.sunHaloRadius == b.sunHaloRadius && a.horizonHaze == b.horizonHaze && // 日晕半径和地平线雾
           a.customWater == b.customWater && a.customSky == b.customSky && a.waterColor == b.waterColor && // 自定义颜色
           a.skyColor == b.skyColor && a.quality == b.quality && a.targetFps == b.targetFps && a.renderMode == b.renderMode && // 画质、帧率、来源
           a.videoPath == b.videoPath && a.videoLoop == b.videoLoop && a.videoMuted == b.videoMuted; // 视频路径、循环、静音
} // 结束比较
