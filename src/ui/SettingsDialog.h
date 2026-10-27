// SPDX-License-Identifier: Apache-2.0
#pragma once // 头文件只包含一次

#include "platform/win32/DisplayManager.h" // 显示器列表
#include "services/SettingsManager.h" // 设置读写

#include <QDialog> // 对话框基类

namespace Ui { // 界面生成类所在命名空间
class SettingsDialog; // 前置声明生成的设置界面
} // 结束界面命名空间

class SettingsDialog : public QDialog { // 设置对话框，改动即时生效
    Q_OBJECT // 启用信号槽元对象

public: // 对外接口
    SettingsDialog(SettingsManager* settings, DisplayManager* displays, QWidget* parent = nullptr); // 构造并装入当前设置
    ~SettingsDialog() override; // 析构并释放界面对象

protected: // 窗口事件
    void changeEvent(QEvent* event) override; // 语言切换时重译并回填

private: // 内部实现
    void refillChoices(); // 按当前语言重填下拉项
    void load(const AppSettings& settings); // 把设置填进控件
    AppSettings collect() const; // 从控件收集一份设置
    void pushLive(); // 把当前控件即时写回
    void populateMonitors(); // 填充显示器列表
    void updateValueLabels(); // 刷新滑条旁的数值
    void revert(); // 取消时恢复打开前的设置

    Ui::SettingsDialog* ui = nullptr; // 生成的界面对象
    SettingsManager* m_settings = nullptr; // 设置管理器，不拥有
    DisplayManager* m_displays = nullptr; // 显示器管理器，不拥有
    AppSettings m_snapshot; // 打开对话框时的设置快照
    bool m_loading = false; // 回填时挡住即时写回
    bool m_retranslating = false; // 防止语言切换重入
}; // 结束设置对话框
