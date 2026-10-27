#version 330 core // OpenGL 3.3 核心，必须留在第一行
// SPDX-License-Identifier: Apache-2.0

in vec2 vUv; // 顶点阶段给出的纹理坐标
uniform sampler2D uFrame; // 当前视频帧
uniform vec2 uUvSpan; // 采样跨度，用来裁切或铺满
uniform vec2 uUvOffset; // 采样起点
out vec4 fragColor; // 输出颜色

void main() { // 贴一帧视频
    vec2 uv = vUv * uUvSpan + uUvOffset; // 映射到帧上的位置
    fragColor = vec4(texture(uFrame, uv).rgb, 1.0); // 不透明，不要视频的 alpha
} // 结束片元主函数
