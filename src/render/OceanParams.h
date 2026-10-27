// SPDX-License-Identifier: Apache-2.0
#pragma once // 头文件只包含一次

#include <QMatrix4x4> // 把视线方向变到世界方向
#include <QString> // 视频路径和主题名
#include <QVector3D> // 颜色和太阳方向

enum class OceanTheme { // 三个内置外观
    Sunny = 0, // 晴日大海
    Sunset = 1, // 日落海洋
    Deep = 2 // 深蓝海洋
}; // 结束主题枚举

enum class RenderMode { // 画面来源
    Realtime = 0, // 实时生成的海面
    Video = 1 // 本地视频贴到海面上
}; // 结束画面来源

struct OceanPalette { // 一个主题的天空、海水和太阳
    QVector3D skyZenith; // 天顶颜色
    QVector3D skyHorizon; // 地平线颜色
    QVector3D waterDeep; // 深处海水
    QVector3D waterShallow; // 浅处海水
    QVector3D sunColor; // 太阳颜色
    QVector3D sunDir; // 调色板里的参考方向，绘制仍用角度算出的方向
    float foam = 0.35f; // 浪尖泡沫
    float waveScale = 1.0f; // 波浪尺度
    float speedScale = 1.0f; // 动画快慢倍率
    float sunGain = 1.0f; // 太阳增益
    float sunAzimuth = 26.0f; // 方位角，度
    float sunElevation = 18.0f; // 高度角，度
    float windAngle = 8.0f; // 风向，度
    float skySaturation = 1.08f; // 天空饱和
    float skyBrightness = 1.02f; // 天空亮度
    float atmosphereHaze = 0.08f; // 大气薄雾
    float sunDisk = 1.30f; // 日盘强度，只画在天上
    float sunHalo = 0.32f; // 日晕强度，只画在天上
    float sunHaloRadius = 0.48f; // 日晕半径
    float horizonHaze = 0.08f; // 地平线薄雾
}; // 结束调色板

struct OceanParams { // 用户可调的海洋参数，不含界面语言
    OceanTheme theme = OceanTheme::Sunny; // 当前主题
    float animSpeed = 1.0f; // 动画速度
    float waveStrength = 1.0f; // 浪高
    float windStrength = 1.0f; // 风强度
    float brightness = 1.0f; // 整体亮度
    float sunReflection = 1.0f; // 海面高光
    float sunAzimuth = 26.0f; // 太阳方位角
    float sunElevation = 18.0f; // 太阳高度角
    float sunIntensity = 1.0f; // 太阳强度
    float highlightWidth = 1.0f; // 高光宽度
    float windAngle = 8.0f; // 风向
    float skySaturation = 1.08f; // 天空饱和
    float skyBrightness = 1.02f; // 天空亮度
    float atmosphereHaze = 0.08f; // 大气薄雾
    float sunDisk = 1.30f; // 日盘
    float sunHalo = 0.32f; // 日晕
    float sunHaloRadius = 0.48f; // 日晕半径
    float horizonHaze = 0.08f; // 地平线薄雾
    bool customWater = false; // 是否改过海水颜色
    bool customSky = false; // 是否改过天空颜色
    QVector3D waterColor = QVector3D(0.05f, 0.38f, 0.64f); // 自定义浅水颜色
    QVector3D skyColor = QVector3D(0.40f, 0.64f, 0.91f); // 自定义地平线颜色
    int quality = 1; // 0 低，1 中，2 高
    int targetFps = 60; // 只使用 30 或 60
    RenderMode renderMode = RenderMode::Realtime; // 实时或本地视频
    QString videoPath; // 本地视频路径
    bool videoLoop = true; // 视频循环
    bool videoMuted = true; // 视频静音

    static OceanParams clamped(OceanParams value); // 把越界值夹回合法范围
    static OceanPalette paletteFor(OceanTheme theme); // 取主题的默认调色板
    static int waveCountForQuality(int quality); // 该画质叠加几层浪
    static void meshSizeForQuality(int quality, int* columns, int* rows); // 该画质的网格列数和行数

    void applyThemeDefaults(OceanTheme theme); // 换主题时写回该主题的天空和太阳
    OceanParams withTheme(OceanTheme theme) const; // 返回换了主题、但保留滑块和视频的副本
}; // 结束海洋参数

QString oceanThemeName(OceanTheme theme); // 主题的界面名称
OceanTheme oceanThemeFromInt(int value); // 整数收成主题，越界夹到 0 至 2
QString renderModeName(RenderMode mode); // 画面来源的界面名称
RenderMode renderModeFromInt(int value); // 非视频都当成实时海洋
OceanPalette lerpPalette(const OceanPalette& a, const OceanPalette& b, float t); // 两套调色板按 t 混合
QVector3D viewSunDirection(float azimuthDeg, float elevationDeg); // 视线空间的太阳方向，朝负 Z
QVector3D worldSunDirection(const QMatrix4x4& view, float azimuthDeg, float elevationDeg); // 变到世界空间
bool oceanParamsEqual(const OceanParams& a, const OceanParams& b); // 比较海洋参数，不读界面语言
