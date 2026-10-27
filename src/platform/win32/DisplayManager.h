// SPDX-License-Identifier: Apache-2.0
#pragma once // 头文件只包含一次

#include <QObject> // 显示器变化信号
#include <QRect> // 物理像素矩形
#include <QVector> // 显示器列表

struct MonitorDesc { // 一块显示器的事实
    QString deviceName; // 设备名，例如 \\.\DISPLAY1
    QString friendlyName; // 给人看的名称，没有则用设备名
    QRect physical; // 物理像素矩形，原点可以是负的
    int dpiX = 96; // 水平 DPI
    int dpiY = 96; // 垂直 DPI
    int refreshHz = 0; // 当前刷新率，读不到为 0
    bool primary = false; // 是否主显示器
}; // 结束显示器描述

QVector<MonitorDesc> selectMonitors(const QVector<MonitorDesc>& monitors, int mode, const QString& deviceName); // 按模式挑显示器

class DisplayManager : public QObject { // 枚举显示器并在变化时通知
    Q_OBJECT // 启用 displaysChanged

public: // 构造、刷新和选择
    explicit DisplayManager(QObject* parent = nullptr); // 构造时立刻枚举一次

    QVector<MonitorDesc> monitors() const; // 最近一次枚举结果
    void refresh(); // 重新枚举，有变化才发信号
    QVector<MonitorDesc> select(int mode, const QString& deviceName) const; // 0 全部，1 主，2 指定设备

signals: // 热插拔后通知壁纸重挂
    void displaysChanged(); // 数量、位置、主屏、DPI 变了

private: // 缓存
    QVector<MonitorDesc> m_monitors; // 当前列表，主屏在前
    bool m_loaded = false; // 第一次刷新不发信号
}; // 结束显示器管理
