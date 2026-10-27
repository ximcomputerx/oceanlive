// SPDX-License-Identifier: Apache-2.0
#include "render/ShaderManager.h" // 着色器管理声明

#include <QCoreApplication> // 程序目录和可翻译的错误句
#include <QFile> // 读资源和磁盘文件
#include <QFileInfo> // 从路径取出文件名

QString ShaderManager::readSource(const QString& resourcePath, const QString& fileName, QString* error) { // 读一份着色器源码
    QFile resource(resourcePath); // 资源路径，例如 :/shaders/ocean.frag
    if (resource.open(QIODevice::ReadOnly)) { // 资源打进了可执行文件
        return QString::fromUtf8(resource.readAll()); // 按 UTF-8 读完全部
    } // 结束资源命中
    const QString leaf = fileName.isEmpty() ? QFileInfo(resourcePath).fileName() : fileName; // 磁盘上的文件名
    const QString disk = QCoreApplication::applicationDirPath() + QStringLiteral("/../resources/shaders/") + leaf; // 开发树里的着色器
    QFile file(disk); // 资源缺失时的后备
    if (file.open(QIODevice::ReadOnly)) { // 磁盘文件可读
        return QString::fromUtf8(file.readAll()); // 同样按 UTF-8
    } // 结束磁盘命中
    if (error) { // 调用方要错误文本
        *error = QCoreApplication::translate("ShaderManager", "无法读取着色器 %1").arg(resourcePath); // 用户可见，可翻译
    } // 结束填写错误
    return {}; // 两种来源都失败
} // 结束读源码

bool ShaderManager::load(const QString& vertexPath, const QString& fragmentPath, QString* error) { // 编译并链接一对着色器
    release(); // 先卸掉上一对
    const QString vertex = readSource(vertexPath, QFileInfo(vertexPath).fileName(), error); // 顶点着色器
    const QString fragment = readSource(fragmentPath, QFileInfo(fragmentPath).fileName(), error); // 片元着色器
    if (vertex.isEmpty() || fragment.isEmpty()) { // 任一读失败
        return false; // 错误已由 readSource 写入
    } // 结束空源码
    if (!m_program.addShaderFromSourceCode(QOpenGLShader::Vertex, vertex)) { // 编译顶点阶段
        if (error) { // 需要把驱动日志给用户
            *error = QCoreApplication::translate("ShaderManager", "顶点着色器编译失败:\n%1").arg(m_program.log()); // 前缀可翻译，日志原文附在后面
        } // 结束填写编译错误
        m_program.removeAllShaders(); // 不留下半成品
        return false; // 编译失败
    } // 结束顶点失败
    if (!m_program.addShaderFromSourceCode(QOpenGLShader::Fragment, fragment)) { // 编译片元阶段
        if (error) { // 需要驱动日志
            *error = QCoreApplication::translate("ShaderManager", "片元着色器编译失败:\n%1").arg(m_program.log()); // 前缀可翻译
        } // 结束填写片元错误
        m_program.removeAllShaders(); // 清掉已加入的顶点着色器
        return false; // 编译失败
    } // 结束片元失败
    if (!m_program.link()) { // 链接成程序
        if (error) { // 需要链接日志
            *error = QCoreApplication::translate("ShaderManager", "着色器链接失败:\n%1").arg(m_program.log()); // 前缀可翻译
        } // 结束填写链接错误
        m_program.removeAllShaders(); // 链接失败也不保留
        return false; // 链接失败
    } // 结束链接失败
    m_linked = true; // 可以绑定使用
    return true; // 加载成功
} // 结束加载

void ShaderManager::release() { // 卸下着色器，便于换主题或退出
    if (m_program.isLinked() || m_linked) { // 曾经链接过
        m_program.removeAllShaders(); // 移除顶点与片元
    } // 结束需要卸载
    m_linked = false; // 标记为未链接
} // 结束释放

QOpenGLShaderProgram& ShaderManager::program() { // 供海面设置 uniform
    return m_program; // 返回同一个程序
} // 结束取程序

bool ShaderManager::isLinked() const { // 绘制前检查
    return m_linked; // 最近一次 load 是否成功
} // 结束查询
