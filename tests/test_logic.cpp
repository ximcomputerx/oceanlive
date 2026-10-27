// SPDX-License-Identifier: Apache-2.0
#include "platform/win32/Autostart.h" // 启动项读写，正式名字不在这里改
#include "platform/win32/DisplayManager.h" // 显示器选择
#include "render/OceanParams.h" // 参数夹取、主题和调色
#include "services/AppLanguage.h" // 语言偏好
#include "utils/FullscreenHeuristic.h" // 全屏窗口判断

#include <QCoreApplication> // 注册表路径需要组织名和应用名
#include <QMatrix4x4> // 太阳方向的视图变换
#include <QVector3D> // 方向向量

#include <cmath> // 雾气默认值的绝对误差
#include <iostream> // 打印 PASS、FAIL 和失败数
#include <limits> // 用 NaN 检查回退

namespace { // 失败计数只在本文件使用

int g_failures = 0; // 累计失败条数

void check(bool condition, const char* text) { // 打印一条结果
    if (!condition) { // 断言不成立
        std::cerr << "FAIL " << text << '\n'; // 失败走标准错误
        ++g_failures; // 记一笔
    } else { // 断言成立
        std::cout << "PASS " << text << '\n'; // 通过走标准输出
    } // 结束通过
} // 结束单条检查

}  // 结束匿名命名空间

int main(int argc, char* argv[]) { // 逻辑测试入口，不创建壁纸窗口
    QCoreApplication application(argc, argv); // 只要核心，不要界面
    QCoreApplication::setOrganizationName(QStringLiteral("OceanLive")); // 测试用的组织名
    QCoreApplication::setApplicationName(QStringLiteral("OceanLiveTests")); // 测试应用名，不是正式启动项

    OceanParams params; // 故意放进越界和 NaN
    params.animSpeed = 9.0f; // 高于动画速度上限
    params.waveStrength = -1.0f; // 低于浪高下限
    params.quality = 8; // 高于质量档
    params.targetFps = 10; // 低于允许的最低帧率
    params.brightness = std::numeric_limits<float>::quiet_NaN(); // 亮度不是数
    const OceanParams clamped = OceanParams::clamped(params); // 夹取后的结果
    check(clamped.animSpeed == 2.0f, "anim speed upper clamp"); // 动画速度上限为 2
    check(clamped.waveStrength == 0.2f, "wave strength lower clamp"); // 浪高下限为 0.2
    check(clamped.quality == 2, "quality clamp"); // 质量最高夹到 2
    check(clamped.targetFps == 30, "low fps snaps to 30"); // 过低帧率抬到 30
    check(clamped.brightness == 1.0f, "nan brightness falls back"); // 非法亮度回到 1
    check(oceanThemeFromInt(9) == OceanTheme::Deep, "theme int clamps"); // 未知主题编号落到深海
    check(OceanParams::waveCountForQuality(0) == 5, "low wave count"); // 低质量 5 条波
    check(OceanParams::waveCountForQuality(1) == 8, "medium wave count"); // 中质量 8 条波
    check(OceanParams::waveCountForQuality(2) == 12, "high wave count"); // 高质量 12 条波
    int columns = 0; // 网格列数
    int rows = 0; // 网格行数
    OceanParams::meshSizeForQuality(2, &columns, &rows); // 高质量网格
    check(columns == 320 && rows == 190, "high mesh size"); // 高质量是 320 乘 190

    OceanParams themed; // 用来确认主题不冲掉手动强度
    themed.waveStrength = 1.4f; // 自定义浪高
    themed.sunIntensity = 1.2f; // 自定义太阳强度
    const OceanParams sunsetParams = themed.withTheme(OceanTheme::Sunset); // 套上日落
    const OceanParams sunnyTheme = themed.withTheme(OceanTheme::Sunny); // 套上晴天
    check(sunsetParams.waveStrength == 1.4f, "theme keeps wave strength"); // 主题保留浪高
    check(sunsetParams.sunIntensity == 1.2f, "theme keeps sun intensity"); // 主题保留太阳强度
    check(sunsetParams.sunElevation < sunnyTheme.sunElevation, "sunset sun sits lower"); // 日落的太阳更低
    check(sunnyTheme.atmosphereHaze < sunsetParams.atmosphereHaze, "sunny haze is lower than sunset"); // 晴天大气雾更淡
    check(sunnyTheme.horizonHaze < sunsetParams.horizonHaze, "sunny horizon haze is lower than sunset"); // 晴天地平线雾更淡
    check(!sunsetParams.customWater, "theme clears custom water"); // 套主题后不再算自定义水色
    OceanParams hazy = OceanParams{}.withTheme(OceanTheme::Sunny); // 先取晴天默认
    hazy.atmosphereHaze = 1.5f; // 再把雾调得很浓
    hazy.sunIntensity = 1.2f; // 太阳强度仍自定义
    const OceanParams clearAgain = hazy.withTheme(OceanTheme::Sunny); // 再套一次晴天
    check(clearAgain.atmosphereHaze < 0.2f, "sunny theme restores low haze"); // 晴天把雾拉回低值
    check(std::abs(clearAgain.atmosphereHaze - 0.08f) < 0.001f, "sunny haze default"); // 晴天雾默认约 0.08
    check(clearAgain.sunIntensity == 1.2f, "theme keeps direct sunlight"); // 再次套主题仍保留阳光强度
    const OceanPalette sunnyLook = OceanParams::paletteFor(OceanTheme::Sunny); // 晴天调色
    check(sunnyLook.skyZenith.z() > sunnyLook.skyZenith.x() + 0.4f, "sunny zenith stays blue"); // 天顶蓝色分量明显更大
    check(sunnyLook.skyHorizon.z() > sunnyLook.skyHorizon.x() + 0.25f, "sunny horizon stays blue"); // 地平线也保持偏蓝
    params.sunElevation = std::numeric_limits<float>::quiet_NaN(); // 太阳高度不是数
    params.atmosphereHaze = std::numeric_limits<float>::quiet_NaN(); // 雾也不是数
    params.sunDisk = -4.0f; // 日盘小于 0
    params.renderMode = static_cast<RenderMode>(9); // 未知画面模式
    const OceanParams clampedAgain = OceanParams::clamped(params); // 再夹取一次
    check(clampedAgain.sunElevation == 18.0f, "nan sun elevation falls back"); // 非法高度回到 18
    check(std::abs(clampedAgain.atmosphereHaze - 0.08f) < 0.001f, "nan haze falls back"); // 非法雾回到晴天默认
    check(clampedAgain.sunDisk == 0.0f, "sun disk clamps at zero"); // 日盘最小为 0
    check(clampedAgain.renderMode == RenderMode::Realtime, "unknown render mode stays realtime"); // 未知模式回到实时海面
    params.renderMode = RenderMode::Video; // 合法的视频模式
    check(OceanParams::clamped(params).renderMode == RenderMode::Video, "video mode is preserved"); // 视频模式不被改掉

    QMatrix4x4 view; // 观察矩阵
    view.lookAt(QVector3D(0.0f, 5.0f, 0.0f), QVector3D(0.0f, 4.0f, 40.0f), QVector3D(0.0f, 1.0f, 0.0f)); // 从海上往前看
    const QVector3D lowSun = view.mapVector(worldSunDirection(view, 0.0f, 4.0f)); // 低太阳的视图方向
    const QVector3D highSun = view.mapVector(worldSunDirection(view, 0.0f, 24.0f)); // 高太阳的视图方向
    const QVector3D rightSun = viewSunDirection(20.0f, 10.0f); // 正方位角应出现在右侧
    check(highSun.y() > lowSun.y(), "higher elevation raises the sun in view"); // 高度更大则视图里更靠上
    check(rightSun.x() > 0.0f, "positive azimuth is to the right"); // 正方位角在右边

    const OceanPalette sunny = OceanParams::paletteFor(OceanTheme::Sunny); // 插值起点
    const OceanPalette sunset = OceanParams::paletteFor(OceanTheme::Sunset); // 插值终点
    const OceanPalette begin = lerpPalette(sunny, sunset, 0.0f); // 权重 0
    const OceanPalette end = lerpPalette(sunny, sunset, 1.0f); // 权重 1
    check(std::abs(begin.skyZenith.x() - sunny.skyZenith.x()) < 0.001f, "lerp start"); // 起点等于晴天天顶
    check(std::abs(end.skyHorizon.x() - sunset.skyHorizon.x()) < 0.001f, "lerp end"); // 终点等于日落地平线

    WindowMetrics metrics; // 全屏启发式的输入
    metrics.window = QRect(0, 0, 2560, 1440); // 窗口正好盖住显示器
    metrics.monitor = QRect(0, 0, 2560, 1440); // 显示器同样大小
    metrics.className = QStringLiteral("UnityWndClass"); // 普通游戏窗口类
    check(isLikelyFullscreenApp(metrics), "borderless fullscreen"); // 无边框铺满算全屏

    metrics.window = QRect(-7, -7, 2574, 1414); // 最大化窗口会超出一点但留着任务栏
    check(!isLikelyFullscreenApp(metrics), "maximized window leaves the taskbar"); // 这种窗口不是全屏应用

    metrics.window = QRect(0, 0, 2560, 1440); // 尺寸又盖住整屏
    metrics.className = QStringLiteral("Progman"); // 桌面自己
    check(!isLikelyFullscreenApp(metrics), "progman is not a fullscreen app"); // Progman 不能触发暂停

    metrics.className = QStringLiteral("Game"); // 类名本身像游戏
    metrics.title = QStringLiteral("OceanLiveWallpaper"); // 标题必须仍是壁纸窗口标题
    check(!isLikelyFullscreenApp(metrics), "our wallpaper window is ignored"); // 自己的壁纸窗口忽略

    metrics.title = QStringLiteral("OceanLiveFullscreenProbe"); // 自测用的全屏探测窗口
    metrics.ours = true; // 标记为自己的探测窗
    check(isLikelyFullscreenApp(metrics), "fullscreen probe is detected"); // 探测窗要被认成全屏

    metrics.iconic = true; // 最小化
    check(!isLikelyFullscreenApp(metrics), "iconic window is ignored"); // 最小化窗口忽略

    metrics.window = QRect(-1920, 0, 1920, 1080); // 副屏在原点左侧
    metrics.monitor = QRect(-1920, 0, 1920, 1080); // 显示器同样在负坐标
    metrics.iconic = false; // 不是最小化
    metrics.ours = false; // 不是自己的探测窗
    metrics.title = QStringLiteral("Game"); // 普通标题
    metrics.className = QStringLiteral("SDL_app"); // 另一个游戏类
    check(isLikelyFullscreenApp(metrics), "negative monitor origin still matches"); // 负坐标原点也能对齐

    MonitorDesc primary; // 主显示器
    primary.deviceName = QStringLiteral("\\\\.\\DISPLAY1"); // 设备名
    primary.primary = true; // 主屏
    primary.physical = QRect(0, 0, 2560, 1440); // 物理矩形
    MonitorDesc secondary; // 左侧副屏
    secondary.deviceName = QStringLiteral("\\\\.\\DISPLAY2"); // 第二块屏
    secondary.primary = false; // 不是主屏
    secondary.physical = QRect(-1920, 0, 1920, 1080); // 负坐标
    const QVector<MonitorDesc> monitors{primary, secondary}; // 两块屏
    const QVector<MonitorDesc> all = selectMonitors(monitors, 0, {}); // 模式 0 是全部
    const QVector<MonitorDesc> onlyPrimary = selectMonitors(monitors, 1, {}); // 模式 1 是主屏
    const QVector<MonitorDesc> specific = selectMonitors(monitors, 2, QStringLiteral("\\\\.\\DISPLAY2")); // 模式 2 按设备名
    const QVector<MonitorDesc> missing = selectMonitors(monitors, 2, QStringLiteral("missing")); // 指定的设备不存在
    check(all.size() == 2, "all monitors"); // 全部模式得到两块
    check(onlyPrimary.size() == 1 && onlyPrimary.front().primary, "primary monitor"); // 只剩主屏
    check(specific.size() == 1 && specific.front().physical.x() == -1920, "specific negative monitor"); // 选中左侧那块
    check(missing.size() == 1 && missing.front().primary, "unknown monitor falls back to primary"); // 找不到就退回主屏

    const QString command = commandLineForExecutable(QStringLiteral("C:/Program Files/OceanLive/OceanLive.exe")); // 带空格的安装路径
    check(command == QStringLiteral("\"C:\\Program Files\\OceanLive\\OceanLive.exe\" --minimized"), "quoted startup command"); // 加引号并带最小化参数

    const QString testValue = QStringLiteral("OceanLiveUnitTest"); // 临时测试值，不是正式启动项名 OceanLive
    QString error; // 注册表错误
    const bool wrote = Autostart::writeValue(testValue, command, &error); // 写入测试值
    check(wrote, "write test run value"); // 写入应当成功
    check(Autostart::readValue(testValue) == command, "read back test run value"); // 读回必须一致
    check(Autostart::removeValue(testValue, &error), "remove test run value"); // 删除测试值
    check(Autostart::readValue(testValue).isEmpty(), "test run value removed"); // 删除后应为空

    if (!Autostart::readValue(testValue).isEmpty()) { // 上面没删干净
        Autostart::removeValue(testValue, &error); // 再删一次，避免留下测试项
    } // 结束补删

    check(AppLanguage::normalizePreference({}) == QLatin1String("system"), "empty language is system"); // 空偏好视为跟随系统
    check(AppLanguage::normalizePreference(QStringLiteral("nope")) == QLatin1String("system"), "unknown language is system"); // 未知语言也跟随系统
    check(AppLanguage::normalizePreference(QStringLiteral("zh_CN")) == QLatin1String("zh_CN"), "zh_CN preference kept"); // 简体偏好保留
    check(AppLanguage::normalizePreference(QStringLiteral("en")) == QLatin1String("en"), "en preference kept"); // 英文偏好保留
    check(AppLanguage::resolveCatalog(QStringLiteral("system"), QLocale(QLocale::Chinese, QLocale::China)) == QLatin1String("zh_CN"), // 系统是简体中文
          "system zh_CN uses Chinese"); // 目录用中文
    check(AppLanguage::resolveCatalog(QStringLiteral("system"), QLocale(QLocale::Chinese, QLocale::Taiwan)) == QLatin1String("zh_CN"), // 系统是繁体地区
          "system zh_TW uses Chinese catalog"); // 仍然用简体目录
    check(AppLanguage::resolveCatalog(QStringLiteral("system"), QLocale(QLocale::English, QLocale::UnitedStates)) == QLatin1String("en"), // 系统是美式英语
          "system en_US uses English"); // 目录用英文
    check(AppLanguage::resolveCatalog(QStringLiteral("system"), QLocale(QLocale::Japanese, QLocale::Japan)) == QLatin1String("en"), // 没有日语目录
          "system ja uses English"); // 其他语言落到英文
    check(AppLanguage::resolveCatalog(QStringLiteral("zh_CN"), QLocale(QLocale::English, QLocale::UnitedStates)) == QLatin1String("zh_CN"), // 强制中文
          "forced Chinese ignores system"); // 不看系统语言
    check(AppLanguage::resolveCatalog(QStringLiteral("en"), QLocale(QLocale::Chinese, QLocale::China)) == QLatin1String("en"), // 强制英文
          "forced English ignores system"); // 系统是中文也不改

    std::cout << "failures=" << g_failures << '\n'; // 汇总失败数
    return g_failures == 0 ? 0 : 1; // 有失败就返回 1
} // 结束测试入口
