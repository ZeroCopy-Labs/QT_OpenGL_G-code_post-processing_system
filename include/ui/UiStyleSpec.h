#pragma once

#include "desktop/AppTheme.h"

#include <QFont>
#include <QString>

// 全应用统一的界面尺寸与字号规范。
//
// 面板、工具条、停靠面板与状态区的字号、控件高度、留白都从这一处取值，
// 避免逐个控件硬编码之后出现视觉漂移；数值是设计起始值，控件仍通过
// minimumHeight 与布局伸缩适应 Windows 显示缩放和字体度量。
namespace uistyle
{
    // 字号（pt）
    constexpr qreal kFontSizeBase = 10.5;          // 全局基础字号
    constexpr qreal kFontSizeSecondary = 9.5;      // 次要状态、辅助信息
    constexpr qreal kFontSizeGroupTitle = 11.5;    // 分组标题、强调文字
    constexpr qreal kFontSizeWorkspace = 11.5;     // 工作区切换按钮

    // 控件高度（px）
    constexpr int kButtonHeight = 36;              // 常用操作按钮
    constexpr int kButtonHeightCompact = 32;       // 面板内紧凑按钮
    constexpr int kControlHeight = 32;             // 下拉框、输入框
    constexpr int kStatusRowHeight = 20;           // 面板内状态文字行
    constexpr int kFooterHeight = 22;              // 面板页脚提示行
    constexpr int kWorkspaceButtonHeight = 72;     // CAD / CAM 工作区按钮
    constexpr int kPanelHeight = 140;              // CAD / CAM 工具面板最小高度
    constexpr int kWorkspaceTrackWidth = 120;      // 右侧工作区轨道宽度

    // 留白（px）
    constexpr int kGroupSpacing = 10;              // 分组间距
    constexpr int kPanelPadding = 10;              // 面板内边距
    constexpr int kMenuRowPaddingV = 7;            // 菜单行上下留白

    // 停靠面板
    constexpr int kDockMinimumWidth = 300;
    constexpr int kDockDefaultWidth = 344;

    // 视口下方的命令输入区
    constexpr int kCommandLineCollapsedHeight = 66;
    constexpr int kCommandLineExpandedHeight = 214;

    // 视口下方的状态信息区
    constexpr int kStatusPaneHeight = 46;

    QFont baseFont();
    QFont secondaryFont();
    QFont groupTitleFont();
    QFont workspaceFont();

    // 统一的控件高度、禁用态、选中态与停靠面板标题规则。
    // 在主题样式表之后追加，保证颜色仍由主题决定。
    QString metricsStyleSheet(const AppThemeColors& theme);
}
