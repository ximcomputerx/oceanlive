// SPDX-License-Identifier: Apache-2.0
#pragma once // 头文件只包含一次

#include <QtGlobal> // quintptr 表示窗口句柄

void enableDarkTitleBar(quintptr windowHandle); // 把标题栏改成深色，失败时静默
