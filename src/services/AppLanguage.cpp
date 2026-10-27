// SPDX-License-Identifier: Apache-2.0
#include "services/AppLanguage.h" // 语言模块声明

#include <QCoreApplication> // 安装和卸下翻译器
#include <QTranslator> // 加载 qm

namespace AppLanguage { // 界面语言实现

namespace { // 进程内只保留一套翻译器

QString g_catalog; // 当前已安装的目录名，zh_CN 或 en
QTranslator* g_app = nullptr; // 应用英文目录
QTranslator* g_qt = nullptr; // Qt 自带按钮目录

QTranslator* ownedTranslator(QTranslator*& slot) { // 第一次使用时创建并挂到应用上
    if (!slot) { // 尚未创建
        slot = new QTranslator(QCoreApplication::instance()); // 父对象是应用，活到进程结束
    } // 结束创建
    return slot; // 返回已有或新建的翻译器
} // 结束取得翻译器

}  // 结束匿名命名空间

QString normalizePreference(const QString& stored) { // 非法值都当成跟随系统
    const QString value = stored.trimmed(); // 去掉首尾空白
    if (value == QLatin1String("zh_CN") || value == QLatin1String("en")) { // 只接受这两个显式值
        return value; // 原样返回
    } // 结束显式值
    return QStringLiteral("system"); // 空串和其他文字都是跟随系统
} // 结束规范化

QString resolveCatalog(const QString& preference, const QLocale& system) { // 决定加载哪一份目录
    const QString value = normalizePreference(preference); // 先收成三个合法值之一
    if (value == QLatin1String("zh_CN") || value == QLatin1String("en")) { // 用户指定了语言
        return value; // 覆盖系统语言
    } // 结束显式选择
    if (system.name().startsWith(QLatin1String("zh"), Qt::CaseInsensitive)) { // 系统语言名以 zh 开头，含繁体
        return QStringLiteral("zh_CN"); // 应用文案仍用简体源字符串
    } // 结束中文系统
    return QStringLiteral("en"); // 其余系统语言用英文
} // 结束解析目录

void install(const QString& preference, const QLocale& system) { // 按偏好安装翻译器
    if (!QCoreApplication::instance()) { // 还没有应用对象
        return; // 不能安装，也不记录目录，避免下次被当成已安装
    } // 结束无应用
    const QString catalog = resolveCatalog(preference, system); // zh_CN 或 en
    if (catalog == g_catalog) { // 和当前相同
        return; // 避免卸下再装上引起语言事件循环
    } // 结束相同目录
    g_catalog = catalog; // 先记下，重入的 LanguageChange 不会再次安装
    QTranslator* app = ownedTranslator(g_app); // 应用翻译器
    QTranslator* qt = ownedTranslator(g_qt); // Qt 翻译器
    QCoreApplication::removeTranslator(app); // 卸下旧的应用目录
    QCoreApplication::removeTranslator(qt); // 卸下旧的 Qt 目录
    if (catalog == QLatin1String("en")) { // 英文需要应用目录和 Qt 目录
        if (app->load(QStringLiteral(":/i18n/OceanLive_en.qm"))) { // 资源里的英文应用译文
            QCoreApplication::installTranslator(app); // 加载成功才安装
        } // 结束应用目录
        if (qt->load(QStringLiteral(":/i18n/qtbase_en.qm"))) { // Qt 标准按钮的英文
            QCoreApplication::installTranslator(qt); // 加载成功才安装
        } // 结束 Qt 英文
        return; // 英文路径到此结束
    } // 结束英文
    if (qt->load(QStringLiteral(":/i18n/qtbase_zh_CN.qm"))) { // 中文只装 Qt 按钮，应用句子用源字符串
        QCoreApplication::installTranslator(qt); // 确定、取消、应用变为中文
    } // 结束 Qt 中文
} // 结束安装

}  // 结束语言命名空间
