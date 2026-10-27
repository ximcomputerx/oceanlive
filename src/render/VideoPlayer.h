// SPDX-License-Identifier: Apache-2.0
#pragma once // 头文件只包含一次

#include <QImage> // 送给海面的当前帧
#include <QObject> // 帧更新和失败信号
#include <QSize> // 帧尺寸
#include <QString> // 路径、错误和状态

class QAudioOutput; // 静音也要挂上音频输出
class QMediaPlayer; // 唯一的播放器
class QVideoFrame; // 解码回调里的帧
class QVideoSink; // 接收视频帧

class VideoPlayer : public QObject { // 一个播放器，同时给出预览和壁纸帧
    Q_OBJECT // 启用帧更新和失败信号

public: // 播放、暂停和查询
    explicit VideoPlayer(QObject* parent = nullptr); // 创建播放器、音频和接收器
    ~VideoPlayer() override; // 停止并释放源

    void playFile(const QString& path, bool loop, bool muted); // 打开本地文件，空路径报未选择
    void pause(); // 正在播放时暂停
    void stopRelease(); // 停止并清空源和当前帧

    QImage currentFrame() const; // 最近一帧，可能为空
    QSize frameSize() const; // 当前帧尺寸
    QString lastError() const; // 最近一次失败，创建时的语言
    QString statusText() const; // 每次调用按当前语言翻译
    bool isPlaying() const; // 是否处于播放状态

signals: // 新帧和失败
    void frameUpdated(); // 海面可以来取 currentFrame
    void failed(const QString& message); // 同一句失败只发一次

private: // 收帧和去重失败
    void onFrame(const QVideoFrame& frame); // 把视频帧转成 RGBA 图像
    void reportFailure(const QString& message); // 记下错误，重复内容不重复发信号

    QMediaPlayer* m_player = nullptr; // 唯一播放器
    QAudioOutput* m_audio = nullptr; // 静音时音量为 0
    QVideoSink* m_sink = nullptr; // 帧回调入口
    QImage m_frame; // 已拷贝的当前帧
    QString m_path; // 正在打开的路径
    QString m_error; // 最近错误
    QString m_reported; // 已经发出过的错误，用来去重
    bool m_loop = true; // 是否循环
    bool m_muted = true; // 是否静音
}; // 结束播放器
