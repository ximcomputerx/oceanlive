// SPDX-License-Identifier: Apache-2.0
#pragma once // 头文件只包含一次

#include <QOpenGLShaderProgram> // 编译并链接着色器程序
#include <QString> // 路径和错误文本

class ShaderManager { // 从资源或磁盘加载一对着色器
public: // 加载、释放和取程序
    bool load(const QString& vertexPath, const QString& fragmentPath, QString* error); // 编译并链接，失败写入 error
    void release(); // 卸下已链上的着色器
    QOpenGLShaderProgram& program(); // 返回当前程序，供设置 uniform
    bool isLinked() const; // 最近一次加载是否成功

private: // 读源码
    static QString readSource(const QString& resourcePath, const QString& fileName, QString* error); // 先读资源，失败再读磁盘

    QOpenGLShaderProgram m_program; // 当前着色器程序
    bool m_linked = false; // 是否已经链接成功
}; // 结束着色器管理
