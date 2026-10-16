#pragma once

#include "desktop/AppTheme.h"

#include <QString>

// CAD / CAM 工具面板共用的布局常量与样式表。
// 两个面板分别放在各自工作区的工具栏上，样式必须来自同一处，避免主题漂移。
namespace toolpanel
{
    constexpr int kPanelHeight = 112;
    constexpr int kComboHeight = 24;

    QString cssRgb(const QColor& color);

    // 面板、按钮、下拉框、子标签页的通用样式。
    QString toolPanelStyleSheet(const AppThemeColors& theme);

    // “更多”网格弹出菜单样式。
    QString toolPanelPopupMenuStyleSheet(const AppThemeColors& theme);
}
