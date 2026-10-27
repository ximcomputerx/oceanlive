// SPDX-License-Identifier: Apache-2.0
#pragma once // 头文件只包含一次

#include <QLocale> // 读取系统语言名
#include <QString> // 语言偏好用字符串保存

namespace AppLanguage { // 界面语言的规范化和安装

QString normalizePreference(const QString& stored); // 只接受 zh_CN 和 en，其余视为跟随系统
QString resolveCatalog(const QString& preference, const QLocale& system); // 算出实际加载 zh_CN 还是 en
void install(const QString& preference, const QLocale& system = QLocale::system()); // 安装对应翻译器

}  // 结束语言命名空间
