// SPDX-License-Identifier: Apache-2.0
#include "render/VideoPlayer.h" // 播放器声明

#include "utils/Logger.h" // 打开文件和失败时写日志

#include <QAudioOutput> // 静音输出
#include <QFileInfo> // 判断文件是否存在
#include <QMediaPlayer> // 播放控制
#include <QUrl> // 本地文件地址
#include <QVideoFrame> // 解码帧
#include <QVideoFrameFormat> // RGBA 格式判断
#include <QVideoSink> // 帧接收器

VideoPlayer::VideoPlayer(QObject* parent) : QObject(parent) { // 全进程共用的一个播放器
    m_audio = new QAudioOutput(this); // 必须挂音频输出，否则有的后端不出画
    m_audio->setMuted(true); // 默认静音
    m_audio->setVolume(0.0f); // 音量也归零
    m_sink = new QVideoSink(this); // 接收解码帧
    m_player = new QMediaPlayer(this); // 唯一播放器
    m_player->setAudioOutput(m_audio); // 接上音频
    m_player->setVideoSink(m_sink); // 接上画面
    m_player->setLoops(QMediaPlayer::Infinite); // 默认循环
    connect(m_sink, &QVideoSink::videoFrameChanged, this, &VideoPlayer::onFrame, Qt::QueuedConnection); // 排队回到本对象线程
    connect(m_player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString& message) { // 后端报错
        if (m_player->source().isEmpty()) { // 清空源时也会报错
            return; // 不当成用户可见失败
        } // 结束空源
        const QString text = message.isEmpty() ? tr("视频解码失败。") : message; // 无正文时用可翻译句子，有正文则保留后端原文
        reportFailure(text); // 去重后通知界面
    }); // 结束错误回调
} // 结束构造

VideoPlayer::~VideoPlayer() { // 析构
    stopRelease(); // 停播并丢掉源
} // 结束析构

void VideoPlayer::playFile(const QString& path, bool loop, bool muted) { // 按路径播放
    const QString cleaned = path.trimmed(); // 去掉首尾空白
    m_loop = loop; // 记住循环
    m_muted = muted; // 记住静音
    m_audio->setMuted(muted); // 应用到输出
    m_audio->setVolume(muted ? 0.0f : 1.0f); // 静音为 0，否则满音量
    m_player->setLoops(loop ? QMediaPlayer::Infinite : QMediaPlayer::Once); // 循环或单次
    if (cleaned.isEmpty()) { // 用户还没选文件
        m_player->stop(); // 停掉当前播放
        m_player->setSource(QUrl()); // 清空源，避免迟到的帧复活错误
        m_path = cleaned; // 路径记为空
        m_frame = QImage(); // 丢掉画面
        emit frameUpdated(); // 让海面清帧
        reportFailure(tr("还没有选择视频文件。")); // 可翻译的空路径错误
        return; // 不再尝试打开
    } // 结束空路径
    const QFileInfo info(cleaned); // 检查磁盘
    if (!info.exists() || !info.isFile()) { // 不存在或不是文件
        m_player->stop(); // 停播
        m_player->setSource(QUrl()); // 清空源
        m_path = cleaned; // 记下用户给的路径
        m_frame = QImage(); // 清画面
        emit frameUpdated(); // 通知海面
        reportFailure(tr("找不到视频文件：%1").arg(cleaned)); // 与桌面测试比较的同一句
        return; // 不设置媒体源
    } // 结束缺文件
    if (m_path != cleaned || m_player->source().isEmpty()) { // 换了文件，或源已被清空
        m_path = cleaned; // 更新路径
        m_error.clear(); // 新文件清掉旧错误
        m_reported.clear(); // 允许同一句错误再次出现
        m_player->setSource(QUrl::fromLocalFile(info.absoluteFilePath())); // 用绝对路径打开
        Logger::info(QStringLiteral("Video"), QStringLiteral("打开 %1").arg(info.absoluteFilePath())); // 日志不进翻译表
    } // 结束换源
    if (m_player->playbackState() != QMediaPlayer::PlayingState) { // 还没在播
        m_player->play(); // 开始播放
    } // 结束补播放
} // 结束打开文件

void VideoPlayer::pause() { // 用户或全屏要求暂停
    if (m_player->playbackState() == QMediaPlayer::PlayingState) { // 只在播放中暂停
        m_player->pause(); // 停在当前帧
    } // 结束播放中判断
} // 结束暂停

void VideoPlayer::stopRelease() { // 离开视频模式或退出
    m_player->stop(); // 停止解码
    m_player->setSource(QUrl()); // 释放文件
    m_path.clear(); // 路径清空
    m_frame = QImage(); // 画面清空
    emit frameUpdated(); // 海面不再显示旧帧
} // 结束释放

QImage VideoPlayer::currentFrame() const { // 海面来取
    return m_frame; // 可能是空图
} // 结束取帧

QSize VideoPlayer::frameSize() const { // 诊断用
    return m_frame.size(); // 空图尺寸为 0
} // 结束取尺寸

QString VideoPlayer::lastError() const { // 测试和诊断读取
    return m_error; // 创建错误时的那句译文
} // 结束取错误

QString VideoPlayer::statusText() const { // 每次调用都按当前语言翻译
    if (!m_error.isEmpty()) { // 有失败
        return m_error; // 显示已存下的错误，不在这里重译
    } // 结束错误优先
    if (m_path.isEmpty()) { // 没打开文件
        return tr("未打开视频"); // 可翻译
    } // 结束未打开
    const QString state = isPlaying() ? tr("播放中") : tr("已暂停或已停止"); // 两种状态句
    if (m_frame.isNull()) { // 已打开但还没有帧
        return tr("%1，等待画面").arg(state); // 带上状态
    } // 结束等待画面
    return QStringLiteral("%1 %2x%3").arg(state).arg(m_frame.width()).arg(m_frame.height()); // 状态加分辨率，格式不翻译
} // 结束状态文本

bool VideoPlayer::isPlaying() const { // 菜单和状态使用
    return m_player->playbackState() == QMediaPlayer::PlayingState; // 仅播放态为真
} // 结束查询播放

void VideoPlayer::onFrame(const QVideoFrame& frame) { // 解码器送来一帧
    if (m_player->source().isEmpty()) { // 源已清空后的迟到帧
        return; // 不能把已报告的缺文件错误清掉
    } // 结束空源守卫
    QVideoFrame copy = frame; // 可映射的副本
    QImage image; // 转成给海面的图
    if (copy.pixelFormat() == QVideoFrameFormat::Format_RGBA8888 && copy.map(QVideoFrame::ReadOnly)) { // 已是 RGBA 且能映射
        image = QImage(copy.bits(0), copy.width(), copy.height(), copy.bytesPerLine(0), QImage::Format_RGBA8888).copy(); // 拷贝出来，解除映射后仍有效
        copy.unmap(); // 释放映射
    } else { // 其他像素格式
        image = copy.toImage(); // 让 Qt 转换
        if (!image.isNull() && image.format() != QImage::Format_RGBA8888) { // 转成功但不是 RGBA
            image = image.convertToFormat(QImage::Format_RGBA8888); // 海面只收 RGBA
        } // 结束格式转换
    } // 结束取图像
    if (image.isNull()) { // 这一帧不可用
        return; // 保留上一帧
    } // 结束空图
    m_frame = image; // 更新当前帧
    m_error.clear(); // 有画面就清掉旧错误
    emit frameUpdated(); // 通知预览和壁纸
} // 结束收帧

void VideoPlayer::reportFailure(const QString& message) { // 记录并通知一次
    m_error = message; // 状态文本会显示它
    if (m_reported == message) { // 同一句已经发过
        return; // 不重复刷日志和信号
    } // 结束去重
    m_reported = message; // 记下已发送的句子
    Logger::error(QStringLiteral("Video"), message); // 日志类别保持英文
    emit failed(message); // 界面提示
} // 结束报告失败
