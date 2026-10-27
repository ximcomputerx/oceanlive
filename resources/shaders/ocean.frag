#version 330 core // OpenGL 3.3 核心，必须留在第一行
// SPDX-License-Identifier: Apache-2.0

// 晴日海面，给 OpenGL 3.3。
// 天空薄雾、日盘和日晕是分开的 uniform。
// 海面闪光走 GGX。日盘不在水面再画一次，否则海面会带上和天空一样的白晕。

in vec3 vWorld; // 世界坐标，天空顶点是 0
in vec3 vNormal; // 顶点法线
in float vFoam; // 顶点阶段的泡沫
in float vKind; // 大于 0.5 是天空

uniform vec2 uResolution; // 帧缓冲像素尺寸
uniform mat4 uInvViewProj; // 视图投影的逆，用来还原视线
uniform vec3 uCamPos; // 摄像机位置
uniform float uTime; // 动画秒
uniform float uBrightness; // 整体亮度
uniform float uSpecular; // 海面高光倍率
uniform float uSunIntensity; // 太阳强度
uniform float uHighlightWidth; // 高光宽窄
uniform float uWind; // 风力
uniform float uAnimSpeed; // 动画速度
uniform float uWaveAmp; // 浪高
uniform int uQuality; // 0 低，1 中，2 高
uniform vec3 uSkyZenith; // 天顶色
uniform vec3 uSkyHorizon; // 地平线色
uniform vec3 uWaterDeep; // 深水色
uniform vec3 uWaterShallow; // 浅水色
uniform vec3 uSunColor; // 太阳色
uniform vec3 uSunDir; // 世界空间太阳方向，视线一侧朝负 Z
uniform float uFoamAmount; // 泡沫量
uniform float uWindAngle; // 风向弧度
uniform float uSkySaturation; // 天空饱和
uniform float uSkyBrightness; // 天空亮度
uniform float uHaze; // 大气薄雾
uniform float uSunDisk; // 日盘强度，只在天空使用
uniform float uSunHalo; // 日晕强度，只在天空使用
uniform float uSunHaloRadius; // 日晕半径
uniform float uHorizonHaze; // 地平线薄雾

out vec4 fragColor; // 输出颜色

float hash(vec2 p) { // 平面哈希
    vec3 p3 = fract(vec3(p.xyx) * 0.1031); // 打散
    p3 += dot(p3, p3.yzx + 33.33); // 再搅一次
    return fract((p3.x + p3.y) * p3.z); // 落到 0 到 1
} // 结束哈希

vec3 noiseDeriv(vec2 p) { // 值和两个方向的导数，水和泡沫都用它
    vec2 i = floor(p); // 格子整数
    vec2 f = fract(p); // 格内小数
    vec2 u = f * f * (3.0 - 2.0 * f); // 平滑权重
    vec2 du = 6.0 * f * (1.0 - f); // 平滑权重的导数
    float a = hash(i); // 左下
    float b = hash(i + vec2(1.0, 0.0)); // 右下
    float c = hash(i + vec2(0.0, 1.0)); // 左上
    float d = hash(i + vec2(1.0, 1.0)); // 右上
    float nx = mix(b - a, d - c, u.y); // x 方向差
    float ny = mix(c, d, u.x) - mix(a, b, u.x); // y 方向差
    float value = mix(mix(a, b, u.x), mix(c, d, u.x), u.y); // 双线性值
    return vec3(value, du.x * nx, du.y * ny); // 值、dx、dy
} // 结束噪声导数

vec2 rotate2(vec2 p, float a) { // 绕原点转一个角
    float c = cos(a); // 余弦
    float s = sin(a); // 正弦
    return vec2(c * p.x - s * p.y, s * p.x + c * p.y); // 旋转后的平面向量
} // 结束旋转

vec3 toneSat(vec3 color, float saturation) { // 按亮度拉饱和
    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722)); // Rec.709 亮度
    return mix(vec3(luma), color, clamp(saturation, 0.0, 1.8)); // 饱和可略超过 1
} // 结束饱和

vec3 atmosphere(vec3 rd, vec3 sunDir, float diskStrength) { // 天空颜色。水面传入的日盘强度必须是 0
    vec3 sun = normalize(sunDir); // 单位太阳方向
    float mu = clamp(dot(rd, sun), 0.0, 1.0); // 视线和太阳的夹角余弦
    float haze = clamp(uHaze, 0.0, 1.6); // 薄雾范围

    if (rd.y < 0.0) { // 视线朝下，仍可能擦到地平线
        // 靠近地平线的向下视线仍用天空色。直接掉进深水会在海天线画出一条暗边。
        float near = exp(clamp(rd.y, -1.5, 0.0) * 3.2); // 越靠近地平线越大，变量名保持 near
        vec3 skySide = mix(uSkyHorizon, uSkyZenith, 0.22); // 地平线上方一点的天空
        vec3 deepSide = mix(uWaterDeep, uWaterShallow, 0.40); // 朝下时的水色
        vec3 clearCol = mix(deepSide, skySide, near); // 晴天的过渡
        vec3 hazyCol = mix(deepSide, uSkyHorizon, near); // 有雾时更贴地平线色
        return mix(clearCol, hazyCol, clamp(haze, 0.0, 1.0)); // 按薄雾混合，这里不画日盘
    } // 结束朝下

    // 晴空大部分是蓝的，只在地平线附近有一条更浅的青带。雾越大这条带越宽，日落会用到。
    float band = mix(0.50, 0.78, clamp(haze, 0.0, 1.0)); // 天顶和地平线的分界高度
    float grad = smoothstep(0.0, band, rd.y); // 从地平线走向天顶
    grad = mix(smoothstep(0.0, 0.85, grad), grad, clamp(haze, 0.0, 1.0)); // 晴天把过渡再收一收
    vec3 col = mix(uSkyHorizon, uSkyZenith, grad); // 天空底色
    col = toneSat(col, uSkySaturation); // 饱和
    col *= max(uSkyBrightness, 0.0); // 亮度不为负

    // 日盘和日晕只属于天空这一趟。水面把 diskStrength 传 0，闪光改由 GGX 负责，不再复制这团白光。
    if (diskStrength > 0.0) { // 只有天空会进来
        float radius = clamp(uSunHaloRadius, 0.15, 2.5); // 日晕半径
        float haloExp = mix(420.0, 22.0, smoothstep(0.15, 2.5, radius)); // 半径越大晕越散
        float halo = pow(mu, haloExp) * max(uSunHalo, 0.0) * diskStrength; // 日晕
        col += uSunColor * halo * 0.34; // 叠上太阳色

        float limb = smoothstep(0.99960, 0.99982, mu) * diskStrength * max(uSunDisk, 0.0); // 日盘边缘
        float core = smoothstep(0.99984, 0.99994, mu) * diskStrength * max(uSunDisk, 0.0); // 日盘中心
        col += uSunColor * limb * 0.36; // 边缘用太阳色
        col += vec3(1.0, 0.99, 0.96) * core * 1.15; // 中心接近白
    } // 结束日盘
    return col; // 天空颜色
} // 结束大气

vec3 viewRay() { // 当前片元的世界视线
    vec2 ndc = vec2(gl_FragCoord.x / uResolution.x, gl_FragCoord.y / uResolution.y) * 2.0 - 1.0; // 像素到 NDC
    vec4 world = uInvViewProj * vec4(ndc, 1.0, 1.0); // 远平面上的点
    return normalize(world.xyz / world.w - uCamPos); // 从摄像机出发的方向
} // 结束视线

vec2 detailGradient(vec2 xz, float dist) { // 细浪法线，不移动顶点
    float lod = mix(1.0, 8.0, smoothstep(24.0, 320.0, dist)); // 远处格子更大
    vec2 uv = xz / lod; // 采样坐标
    float t = uTime * max(uAnimSpeed, 0.0); // 动画时间
    vec2 wind = vec2(sin(uWindAngle), cos(uWindAngle)); // 风向
    uv += wind * t * (0.22 + 0.18 * clamp(uWind, 0.0, 2.0)); // 细浪随风移动

    int octaves = uQuality <= 0 ? 1 : (uQuality == 1 ? 2 : 3); // 画质决定层数
    const float kRot[3] = float[3](0.17, 1.31, 2.47); // 每层旋转角
    vec2 grad = vec2(0.0); // 累加法线偏移
    float weight = 1.0; // 层权重
    float scale = 0.42; // 层尺度
    for (int i = 0; i < 3; ++i) { // 最多三层
        if (i >= octaves) { // 这一档画质不要
            break; // 停止
        } // 结束截断
        vec2 p = rotate2(uv * scale, kRot[i]); // 旋转后的采样点
        if (i == 0 && uQuality >= 1) { // 中高画质的第一层做域扭曲
            vec3 warp = noiseDeriv(p * 0.47); // 扭曲噪声
            p += vec2(warp.x - 0.5, noiseDeriv(p + vec2(13.2, 4.8)).x - 0.5) * 0.85; // 推开采样点
        } // 结束扭曲
        vec3 n = noiseDeriv(p); // 值和导数
        float octaveFade = i == 0 ? 1.0 : (1.0 - smoothstep(70.0, 260.0, dist)); // 远处丢掉更细的层
        grad += n.yz * weight * scale * octaveFade; // 导数累加
        scale *= 2.11; // 下一层更细
        weight *= 0.48; // 下一层更轻
    } // 结束细浪层
    float amount = mix(0.085, 0.015, smoothstep(18.0, 180.0, dist)); // 远处起伏更小
    amount *= 0.55 + 0.35 * clamp(uWaveAmp, 0.0, 2.0); // 浪高大则细浪也稍强
    return grad * amount; // 法线偏移
} // 结束细浪梯度

float ggxSpecular(vec3 N, vec3 V, vec3 L, float roughness) { // 海面高光，和天空用同一个太阳方向
    vec3 H = normalize(V + L); // 半程向量
    float NdotH = max(dot(N, H), 0.0); // 法线和半程
    float NdotV = max(dot(N, V), 0.04); // 法线和视线，留一点避免除零
    float NdotL = max(dot(N, L), 0.0); // 法线和太阳
    float alpha = clamp(roughness * roughness, 0.012, 1.0); // 粗糙度平方
    float a2 = alpha * alpha; // 再平方
    float denom = NdotH * NdotH * (a2 - 1.0) + 1.0; // 法线分布分母
    float D = a2 / (3.14159265 * denom * denom + 1e-5); // GGX 分布
    float k = (roughness + 1.0); // Smith 遮蔽的中间量
    k = k * k * 0.125; // 几何项参数
    float Gv = NdotV / (NdotV * (1.0 - k) + k); // 视线遮蔽
    float Gl = NdotL / (max(NdotL, 0.001) * (1.0 - k) + k); // 光线遮蔽
    float VdotH = max(dot(V, H), 0.0); // 视线和半程
    float fresnel = 0.02 + 0.98 * pow(1.0 - VdotH, 5.0); // 菲涅尔，水的基础反射约 0.02
    return D * Gv * Gl * fresnel * NdotL / (4.0 * NdotV + 0.05); // 高光
} // 结束 GGX

void main() { // 天空或海水
    vec3 sunDir = normalize(uSunDir); // 同一太阳方向
    vec3 col; // 最终颜色
    if (vKind > 0.5) { // 天空三角形
        col = atmosphere(viewRay(), sunDir, 1.0); // 日盘强度为 1，允许画太阳
    } else { // 海面
        float dist = distance(vWorld, uCamPos); // 到摄像机的距离
        vec2 grad = detailGradient(vWorld.xz, dist); // 细浪
        vec3 macroN = normalize(vNormal); // 宏观法线
        vec3 N = normalize(vec3(macroN.x + grad.x, macroN.y, macroN.z + grad.y)); // 叠上细浪
        vec3 V = normalize(uCamPos - vWorld); // 指向摄像机
        vec3 L = sunDir; // 光照方向就是太阳
        float NdotV = clamp(dot(N, V), 0.0, 1.0); // 朝向摄像机
        float NdotL = clamp(dot(N, L), 0.0, 1.0); // 朝向太阳
        float lightGain = clamp(uSunIntensity, 0.0, 1.8); // 太阳增益
        float sunLit = pow(NdotL, 0.85); // 受光，指数略小于 1

        float facing = pow(NdotV, 0.72); // 正面更浅
        vec3 body = mix(uWaterDeep, uWaterShallow, 0.28 + 0.72 * facing * exp(-dist * 0.0016)); // 水体本色
        vec3 farCol = mix(uWaterShallow, uSkyHorizon, 0.48); // 远处仍以水色为主，名字保持 farCol
        farCol = mix(farCol, uSkyZenith, 0.18); // 再掺一点天顶，不掺成白
        float farAmt = smoothstep(20.0, 200.0, dist) * mix(0.58, 0.86, clamp(uHorizonHaze, 0.0, 1.0)); // 远处混合量
        body = mix(body, farCol, farAmt); // 远处靠向这条水色
        body *= 0.94 + 0.18 * sunLit * lightGain; // 受光略亮
        float crest = smoothstep(0.08, 1.05, vWorld.y / max(uWaveAmp, 0.25)); // 波峰
        body += uWaterShallow * sunLit * 0.05 * lightGain; // 受光的浅水
        body += uSunColor * crest * sunLit * 0.035 * lightGain; // 波峰一点暖色

        float fresnel = 0.02 + 0.98 * pow(1.0 - NdotV, 5.0); // 掠射时反射变强
        float fresnelCap = mix(0.88, 0.96, clamp(uHorizonHaze, 0.0, 1.0)); // 反射上限，不让整片海变成镜子
        fresnel = clamp(fresnel, 0.02, fresnelCap); // 夹住
        vec3 reflection = atmosphere(reflect(-V, N), sunDir, 0.0); // 反射天空，日盘强度为 0
        col = mix(body, reflection, fresnel); // 水体和反射

        float rough = mix(0.20, 0.040, smoothstep(16.0, 320.0, dist)); // 远处高光更尖
        rough *= clamp(uHighlightWidth, 0.35, 2.2); // 用户拉宽
        rough *= mix(0.86, 1.32, clamp(length(grad) * 4.0, 0.0, 1.0)); // 细浪处更糙
        float broad = ggxSpecular(N, V, L, clamp(rough * 2.05, 0.06, 0.9)); // 宽高光
        float tight = ggxSpecular(N, V, L, clamp(rough * 0.46, 0.018, 0.26)); // 窄高光
        vec2 wind = vec2(sin(uWindAngle), cos(uWindAngle)); // 风向
        float t = uTime * max(uAnimSpeed, 0.0); // 时间
        float glint = noiseDeriv(vWorld.xz * vec2(0.011, 0.019) + wind * t * 0.04).x; // 闪点噪声
        float sparkle = smoothstep(0.30, 0.78, glint); // 只有较亮的噪声变成闪点
        float spec = broad * 0.62 + tight * (0.55 + 0.85 * sparkle); // 宽光加闪点
        float specDist = mix(1.0, 0.82, smoothstep(120.0, 520.0, dist)); // 很远处略收
        vec3 specTint = mix(vec3(1.0, 0.985, 0.93), uSunColor, 0.18); // 高光偏暖白
        vec3 specCol = specTint * spec * lightGain * uSpecular * specDist * 2.35; // 高光颜色
        specCol = specCol / (vec3(1.0) + specCol * 0.26); // 压一下过亮
        col += specCol; // 叠到海面

        float foamNoise = noiseDeriv(vWorld.xz * 0.31 + vec2(uTime * 0.18, -uTime * 0.11)).x; // 泡沫纹理
        float foam = smoothstep(0.55, 1.15, vFoam) * smoothstep(0.35, 0.8, foamNoise); // 挤在一起且噪声够高
        foam *= 1.0 - smoothstep(28.0, 150.0, dist); // 远处泡沫消失
        foam = clamp(foam * uFoamAmount, 0.0, 1.0); // 主题泡沫量
        col = mix(col, vec3(0.86, 0.91, 0.93), foam); // 混进浅灰白泡沫

        vec3 look = normalize(vWorld - uCamPos); // 从摄像机指向水面
        if (look.y > 0.0) { // 水面跑到地平线上面
            discard; // 丢掉，避免和天空叠出第二条地平线
        } // 结束丢弃

        // 晴天的空气透视保持蓝色而且很弱。不采样地平线大气，那会把远处海水冲白。
        float aerial = smoothstep(360.0, 1100.0, dist) * clamp(uHorizonHaze, 0.0, 1.0) * 0.22; // 很远才有一点
        vec3 aerialCol = mix(uWaterShallow, uSkyHorizon, 0.30); // 仍偏水色
        col = mix(col, aerialCol, aerial); // 轻轻混入
        float seam = smoothstep(-0.018, -0.001, look.y); // 紧贴海天线的一条
        seam *= 0.28 + 0.22 * clamp(uHorizonHaze, 0.0, 1.0); // 雾大时稍宽
        vec3 seamCol = toneSat(mix(uSkyZenith, uSkyHorizon, 0.78), uSkySaturation) * max(uSkyBrightness, 0.0); // 用天空色接上
        col = mix(col, seamCol, seam); // 接住海天线
    } // 结束海面

    col *= max(uBrightness, 0.0); // 整体亮度
    float peak = max(max(col.r, col.g), col.b); // 最亮通道
    col = col / (1.0 + max(peak - 1.12, 0.0) * 0.32); // 超过 1.12 的部分轻轻压下去
    fragColor = vec4(clamp(col, 0.0, 1.0), 1.0); // 不透明输出
} // 结束片元主函数
