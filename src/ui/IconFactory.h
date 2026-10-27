// SPDX-License-Identifier: Apache-2.0
#pragma once // 头文件只包含一次

#include <QIcon> // 返回多尺寸图标

class IconFactory { // 提供程序图标
public: // 唯一入口
    static QIcon appIcon(); // 生成托盘和窗口用的图标
}; // 结束图标工厂
