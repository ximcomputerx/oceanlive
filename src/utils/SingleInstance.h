// SPDX-License-Identifier: Apache-2.0
#pragma once // 头文件只包含一次

#include <QObject> // 用信号通知已有实例

class SingleInstance : public QObject { // 保证同时只跑一个 OceanLive
    Q_OBJECT // 启用元对象，才能发信号

public: // 构造和抢占实例名
    explicit SingleInstance(QObject* parent = nullptr); // 挂到父对象上
    bool acquire(); // 抢到监听权返回真，已有实例则返回假

signals: // 第二个进程要求把窗口带到前面
    void raised(); // 收到唤醒字节时发出

private: // 本进程的本地套接字服务
    class QLocalServer* m_server = nullptr; // 监听单实例名字的服务端
}; // 结束单实例类
