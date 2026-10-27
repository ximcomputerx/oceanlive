// SPDX-License-Identifier: Apache-2.0
#pragma once // 头文件只包含一次

#include <QStringList> // 命令行参数

class QApplication; // 事件循环，只前向声明
class MainWindow; // 自测会隐藏和重新显示它
class SettingsManager; // 视频自测改完设置要还原
class VideoPlayer; // 调用方传入的那一个播放器
class WallpaperManager; // 自测负责启动和停止壁纸

int runDumpPreview(QApplication& application, const QStringList& arguments); // 导出预览后返回，不进入正式界面
int runSelfTest(QApplication& application, MainWindow& window, WallpaperManager& wallpapers, const QStringList& arguments); // 桌面挂接自测
int runVideoDesktopTest(QApplication& application, MainWindow& window, WallpaperManager& wallpapers, SettingsManager* settings, // 视频壁纸自测的前半参数
                        VideoPlayer* video, const QStringList& arguments); // 继续使用这一个播放器，函数结束返回退出码
