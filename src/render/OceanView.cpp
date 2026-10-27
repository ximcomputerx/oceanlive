// SPDX-License-Identifier: Apache-2.0
#include "render/OceanView.h" // 预览控件声明

#include "render/VideoPlayer.h" // 绑定视频帧

#include <QLabel> // 渲染失败时的文字
#include <QShowEvent> // 显示事件
#include <QVBoxLayout> // 海面和错误标签上下排

OceanView::OceanView(QWidget* parent) : QWidget(parent) { // 创建预览
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding); // 占满主窗口预览区
    setMinimumSize(320, 180); // 最小约 16:9
    m_surface = new OceanSurface(); // OpenGL 窗口，父对象稍后由容器持有
    m_container = QWidget::createWindowContainer(m_surface, this); // 嵌进普通控件
    m_container->setFocusPolicy(Qt::NoFocus); // 预览不抢键盘
    m_container->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding); // 容器跟随拉伸
    m_errorLabel = new QLabel(this); // 失败说明
    m_errorLabel->setWordWrap(true); // 长错误换行
    m_errorLabel->setAlignment(Qt::AlignCenter); // 居中
    m_errorLabel->hide(); // 平时不显示

    auto* layout = new QVBoxLayout(this); // 本控件的唯一布局
    layout->setContentsMargins(0, 0, 0, 0); // 预览贴边
    layout->addWidget(m_container); // 海面在上
    layout->addWidget(m_errorLabel); // 错误文字在下，默认隐藏

    connect(m_surface, &OceanSurface::renderFailed, this, [this](const QString& message) { // 海面报错
        m_errorLabel->setText(message); // 显示同一句错误
        m_errorLabel->show(); // 让用户看见
        m_container->hide(); // 黑窗口让位给文字
        emit renderFailed(message); // 主窗口也可以提示
    }); // 结束失败转发
    connect(m_surface, &OceanSurface::renderReady, this, &OceanView::renderReady); // 就绪原样转发
    connect(m_surface, &OceanSurface::statsChanged, this, &OceanView::statsChanged); // 统计原样转发
} // 结束构造

void OceanView::showEvent(QShowEvent* event) { // 控件变为可见
    QWidget::showEvent(event); // 先走基类
    m_surface->setPaused(false); // 显示预览时继续动画
} // 结束显示

OceanSurface* OceanView::surface() const { // 外部需要直接访问海面
    return m_surface; // 返回内部窗口
} // 结束取海面

void OceanView::setParams(const OceanParams& params) { // 主窗口改了主题或滑块
    m_surface->setParams(params); // 交给海面
} // 结束设参数

void OceanView::setPaused(bool paused) { // 用户暂停或全屏暂停
    m_surface->setPaused(paused); // 交给海面
} // 结束暂停

void OceanView::bindVideo(VideoPlayer* video) { // 本地视频模式把帧送进海面
    if (m_video == video) { // 同一个播放器又绑一次
        if (m_video) { // 已有播放器
            m_surface->setVideoFrame(m_video->currentFrame()); // 立刻刷新当前帧
        } // 结束补帧
        return; // 不重复连接信号
    } // 结束同一播放器
    if (m_video) { // 换掉旧播放器
        disconnect(m_video, nullptr, this, nullptr); // 断开它发到本控件的全部信号
    } // 结束断开旧源
    m_video = video; // 记下新源，可以为空
    if (!m_video) { // 回到实时海洋
        m_surface->setVideoFrame(QImage()); // 清掉上一帧视频
        return; // 不再连接
    } // 结束空源
    connect(m_video, &VideoPlayer::frameUpdated, this, [this] { // 新帧到达
        if (m_video) { // 回调时源仍在
            m_surface->setVideoFrame(m_video->currentFrame()); // 上传到海面
        } // 结束源仍在
    }); // 结束帧回调
    m_surface->setVideoFrame(m_video->currentFrame()); // 绑定当下就送一帧
} // 结束绑定视频

RenderStats OceanView::stats() const { // 诊断区读取
    return m_surface->stats(); // 转给海面
} // 结束取统计
