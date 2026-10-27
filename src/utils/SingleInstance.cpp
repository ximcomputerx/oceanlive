// SPDX-License-Identifier: Apache-2.0
#include "utils/SingleInstance.h" // 单实例声明

#include "utils/Logger.h" // 记录第二次启动

#include <QLocalServer> // 本进程监听
#include <QLocalSocket> // 探测已有进程

bool SingleInstance::acquire() { // 抢占本地套接字名字
    const QString name = QStringLiteral("OceanLive.single.instance"); // 固定实例名
    QLocalSocket socket; // 用来探测是否已有监听
    socket.connectToServer(name); // 尝试连上已运行的实例
    if (socket.waitForConnected(200)) { // 200 毫秒内连上说明已有实例
        socket.write("raise"); // 请对方把窗口带到前面
        socket.flush(); // 立刻送出
        socket.waitForBytesWritten(200); // 等待写完
        Logger::info(QStringLiteral("App"), QStringLiteral("检测到已运行的 OceanLive 实例")); // 日志保持中文原文
        return false; // 本进程不应继续启动
    } // 结束已有实例分支
    QLocalServer::removeServer(name); // 清掉崩溃留下的残留名字
    m_server = new QLocalServer(this); // 由本对象持有
    if (!m_server->listen(name)) { // 监听失败仍允许继续跑
        Logger::warn(QStringLiteral("App"), QStringLiteral("单实例监听失败: %1").arg(m_server->errorString())); // 记下系统错误
        return true; // 不因为监听失败而退出
    } // 结束监听失败
    connect(m_server, &QLocalServer::newConnection, this, [this] { // 后来的进程连进来
        while (QLocalSocket* client = m_server->nextPendingConnection()) { // 取走每个连接
            client->waitForReadyRead(100); // 读掉唤醒字节
            client->deleteLater(); // 下轮事件循环释放
        } // 结束连接循环
        emit raised(); // 通知主窗口到前面
    }); // 结束连接回调
    return true; // 本进程持有实例名
} // 结束抢占

SingleInstance::SingleInstance(QObject* parent) : QObject(parent) {} // 只转交父对象
