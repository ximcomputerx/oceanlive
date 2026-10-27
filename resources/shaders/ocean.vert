#version 330 core // OpenGL 3.3 核心，必须留在第一行
// SPDX-License-Identifier: Apache-2.0

// 三层尺度的 Gerstner 波，给 OpenGL 3.3。
// 长涌和中浪移动顶点。细涟漪留在片元着色器。
// 频率不是整数倍，方向靠近同一条风轴。

layout(location = 0) in vec2 aGrid; // 网格坐标，约 0 到 1；天空三角形用 -1 到 1
layout(location = 1) in float aKind; // 大于 0.5 表示天空，否则是海面

uniform mat4 uViewProj; // 视图投影
uniform float uTime; // 动画秒
uniform float uWaveAmp; // 浪高倍率
uniform float uWind; // 风力
uniform float uAnimSpeed; // 动画速度
uniform vec2 uPatch; // 海面补丁的宽和长
uniform int uWaveCount; // 实际叠加的层数
uniform float uWindAngle; // 风向弧度

out vec3 vWorld; // 世界坐标
out vec3 vNormal; // 宏观法线
out float vFoam; // 由雅可比挤出来的泡沫
out float vKind; // 原样传给片元

const int kMaxWaves = 12; // 数组长度，实际层数由 uWaveCount 截断

// 振幅、频率（弧度每米）、相对风向的偏角、陡度、相位
const float kAmp[12] = float[12](1.15, 0.72, 0.42, 0.18, 0.11, 0.07, 0.04, 0.025, 0.016, 0.010, 0.006, 0.004); // 从长浪到短浪
const float kFreq[12] = float[12](0.028, 0.047, 0.079, 0.131, 0.214, 0.347, 0.530, 0.810, 1.190, 1.730, 2.410, 3.200); // 不是整数倍
const float kSpread[12] = float[12](0.02, 0.16, -0.22, 0.34, -0.41, 0.12, -0.18, 0.48, -0.36, 0.22, -0.15, 0.07); // 相对风轴的偏角
const float kSteep[12] = float[12](0.18, 0.20, 0.24, 0.32, 0.34, 0.30, 0.26, 0.22, 0.18, 0.16, 0.14, 0.12); // 陡度
const float kPhase[12] = float[12](0.6, 1.9, 2.8, 0.4, 3.7, 5.2, 1.3, 4.6, 2.4, 0.8, 5.5, 3.1); // 初始相位

void main() { // 天空直接出裁剪坐标，海面走 Gerstner
    if (aKind > 0.5) { // 天空三角形
        vKind = 1.0; // 告诉片元这是天空
        vWorld = vec3(0.0); // 天空不用世界坐标
        vNormal = vec3(0.0, 1.0, 0.0); // 占位法线
        vFoam = 0.0; // 天空没有泡沫
        gl_Position = vec4(aGrid, 0.999, 1.0); // 贴在远裁剪面附近，盖住背景
        return; // 不参与波浪
    } // 结束天空顶点

    float x = (aGrid.x - 0.5) * uPatch.x; // 水平居中铺开
    // 几何间距让屏幕上的三角形大小接近，包括靠近地平线的地方。
    float z = -18.0 + exp(mix(log(8.0), log(max(uPatch.y, 9.0)), clamp(aGrid.y, 0.0, 1.0))); // 近处密、远处稀
    vec2 xz = vec2(x, z); // 海面平面坐标

    float t = uTime * max(uAnimSpeed, 0.0); // 动画时间不为负
    float windScale = mix(0.78, 1.35, clamp(uWind, 0.0, 2.0) * 0.5); // 风力改波速
    vec2 wind = vec2(sin(uWindAngle), cos(uWindAngle)); // 风向
    vec2 side = vec2(wind.y, -wind.x); // 风向的侧向

    vec3 pos = vec3(xz.x, 0.0, xz.y); // 静止海面
    vec3 normal = vec3(0.0, 1.0, 0.0); // 从向上开始累加法线
    float jxx = 1.0; // 雅可比 xx
    float jzz = 1.0; // 雅可比 zz
    float jxz = 0.0; // 雅可比 xz
    float ampScale = max(uWaveAmp, 0.0); // 浪高不为负

    for (int i = 0; i < kMaxWaves; ++i) { // 最多十二层
        if (i >= uWaveCount) { // 画质没要这么多
            break; // 停止
        } // 结束层数截断
        float amp = kAmp[i] * ampScale; // 这一层振幅
        float freq = kFreq[i]; // 这一层频率
        float spread = kSpread[i]; // 相对风向的偏角
        vec2 dir = normalize(wind * cos(spread) + side * sin(spread)); // 传播方向

        // 短波在地平线前收掉，避免重复条纹。
        float shortWave = smoothstep(0.16, 0.70, freq); // 频率越高越像短波
        float farFade = smoothstep(28.0, 120.0, z); // 越远越该收短波，名字保持 farFade
        amp *= mix(1.0, 0.0, shortWave * farFade); // 远处短波振幅归零
        if (amp <= 0.0008) { // 小到可以不算
            continue; // 下一层
        } // 结束跳过短波

        float omega = sqrt(9.81 * freq) * windScale; // 深水重力波角频率
        float Q = kSteep[i] / (freq * amp * float(max(uWaveCount, 1)) + 0.02); // 陡度分到各层
        Q = clamp(Q, 0.0, 0.85); // 避免波峰打结
        float transverse = dot(xz, vec2(-dir.y, dir.x)); // 沿波峰方向的坐标
        float warp = sin(transverse * (0.021 + freq * 0.016) + kPhase[i]); // 沿波峰轻轻扭一下
        amp *= 0.78 + 0.22 * sin(dot(xz, vec2(0.0062, 0.0034)) * (1.0 + freq * 0.35) + kPhase[i] * 2.0); // 振幅沿海面缓变
        float theta = freq * dot(dir, xz) + omega * t + warp * 1.25 + kPhase[i]; // 相位
        float s = sin(theta); // 垂直位移用正弦
        float c = cos(theta); // 水平位移用余弦

        pos.x += Q * amp * dir.x * c; // 沿传播方向挤水平
        pos.z += Q * amp * dir.y * c; // 另一水平分量
        pos.y += amp * s; // 抬高波峰

        float wa = freq * amp; // 法线和雅可比的公共系数
        normal.x -= dir.x * wa * c; // 法线 x
        normal.z -= dir.y * wa * c; // 法线 z
        normal.y -= Q * wa * s; // 法线 y

        jxx -= Q * dir.x * dir.x * wa * s; // 雅可比 xx
        jzz -= Q * dir.y * dir.y * wa * s; // 雅可比 zz
        jxz -= Q * dir.x * dir.y * wa * s; // 雅可比 xz
    } // 结束各层浪

    float jacob = jxx * jzz - jxz * jxz; // 雅可比行列式
    vFoam = clamp(1.0 - jacob, 0.0, 1.0); // 挤在一起时泡沫变多
    vWorld = pos; // 位移后的位置
    vNormal = normalize(normal); // 单位法线
    vKind = 0.0; // 这是海面
    gl_Position = uViewProj * vec4(pos, 1.0); // 投影到裁剪空间
} // 结束顶点主函数
