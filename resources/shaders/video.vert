#version 330 core // OpenGL 3.3 核心，必须留在第一行
// SPDX-License-Identifier: Apache-2.0

layout(location = 0) in vec2 aGrid; // 全屏四边形的位置，范围约 -1 到 1
layout(location = 1) in float aKind; // 与海面网格共用的顶点格式，视频路径不用它

out vec2 vUv; // 交给片元的纹理坐标

void main() { // 视频四边形不做波浪
    gl_Position = vec4(aGrid, 0.0, 1.0); // 直接放到裁剪空间
    vUv = aGrid * 0.5 + 0.5; // 从 -1..1 变到 0..1
    vUv.y = 1.0 - vUv.y; // 图像行从上到下，翻转 V
} // 结束顶点主函数
