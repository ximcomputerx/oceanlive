// SPDX-License-Identifier: Apache-2.0
#pragma once // 头文件只包含一次

#include "render/OceanSurface.h" // 预览里嵌的 OpenGL 窗口

#include <QWidget> // 作为主窗口里的普通控件

class OceanView : public QWidget { // 把海面窗口装进控件，并显示渲染错误
    Q_OBJECT // 启用元对象，转发渲染信号

public: // 创建、改参数和绑视频
    explicit OceanView(QWidget* parent = nullptr); // 创建海面并放进布局

    OceanSurface* surface() const; // 取出内部海面，供外部直接调
    void setParams(const OceanParams& params); // 把海洋参数交给海面
    void setPaused(bool paused); // 暂停或继续预览动画
    void bindVideo(class VideoPlayer* video); // 把视频帧送进海面，空指针表示卸下
    RenderStats stats() const; // 读取最近一帧的统计

signals: // 向主窗口转发渲染状态
    void renderFailed(const QString& message); // 海面初始化或绘制失败
    void renderReady(); // 海面可以出图
    void statsChanged(const RenderStats& stats); // 帧时间和帧率更新

protected: // 窗口第一次显示时恢复动画
    void showEvent(QShowEvent* event) override; // 显示后取消暂停

private: // 海面容器、错误标签和视频源
    OceanSurface* m_surface = nullptr; // 实际绘制海面的 OpenGL 窗口
    QWidget* m_container = nullptr; // 把 OpenGL 窗口嵌进本控件
    class QLabel* m_errorLabel = nullptr; // 渲染失败时替换预览
    class VideoPlayer* m_video = nullptr; // 当前绑定的视频，可为空
}; // 结束预览控件
