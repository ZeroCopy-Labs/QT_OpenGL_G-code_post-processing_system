#include "platform/pch.h"

#include "ui/UiStyleSpec.h"

namespace uistyle
{
    namespace
    {
        QFont metricFont(qreal pointSize, bool bold)
        {
            // 与主窗口一致的中文界面字体；字号来自统一规范。
            QFont font(QStringLiteral("Microsoft YaHei UI"));
            font.setPointSizeF(pointSize);
            font.setBold(bold);
            return font;
        }
    }

    QFont baseFont()
    {
        return metricFont(kFontSizeBase, false);
    }

    QFont secondaryFont()
    {
        return metricFont(kFontSizeSecondary, false);
    }

    QFont groupTitleFont()
    {
        return metricFont(kFontSizeGroupTitle, true);
    }

    QFont workspaceFont()
    {
        return metricFont(kFontSizeWorkspace, false);
    }

    QString metricsStyleSheet(const AppThemeColors& theme)
    {
        return QStringLiteral
        (
            // 通用控件：统一高度与内边距，避免按钮显得局促、文字被裁剪。
            "QPushButton { min-height: %1px; padding: 3px 14px; }"
            "QToolButton { min-height: %2px; }"
            "QComboBox, QLineEdit, QSpinBox, QDoubleSpinBox, QAbstractSpinBox {"
            " min-height: %2px; padding: 2px 8px; }"
            "QComboBox::drop-down { width: 22px; }"
            "QComboBox QAbstractItemView { padding: 2px; }"
            "QComboBox QAbstractItemView::item { min-height: 26px; }"
            "QCheckBox, QRadioButton { min-height: 24px; spacing: 6px; }"
            "QCheckBox::indicator, QRadioButton::indicator { width: 15px; height: 15px; }"
            // 禁用态：保持可读但明确不可用。
            "QPushButton:disabled, QToolButton:disabled { color: %3; background: %4; border-color: %5; }"
            "QLabel:disabled, QCheckBox:disabled, QRadioButton:disabled { color: %3; }"
            "QComboBox:disabled, QLineEdit:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled {"
            " color: %3; background: %4; border-color: %5; }"
            // 菜单与标签页留白。
            "QMenu::item { padding: %6px 30px %6px 24px; }"
            "QTabBar::tab { padding: 8px 16px; }"
            // 停靠面板：标题使用分组标题字号，面板边界清晰。
            "QDockWidget { titlebar-close-icon: none; titlebar-normal-icon: none; font-weight: 600; }"
            "QDockWidget::title { background: %7; padding: 7px 10px; border-bottom: 1px solid %5; }"
            "QDockWidget > QWidget { background: %8; }"
            // 分组框标题。
            "QGroupBox::title { font-weight: 600; }"
            // 工具提示：提高可读性。
            "QToolTip { padding: 6px 8px; }"
            // 右侧工作区轨道与切换按钮。
            "QWidget#workspaceSwitcherRoot { background: %7; border-left: 1px solid %5; }"
            "QLabel#workspaceSwitcherTitle {"
            " color: %3; font-size: 9.5pt; font-weight: 600; padding: 10px 0px 4px 0px; }"
            "QToolButton[workspaceButton=\"true\"] {"
            " border: 1px solid %5; border-radius: 6px; background: %8; color: %9;"
            " padding: 8px 6px; font-size: 11.5pt; }"
            "QToolButton[workspaceButton=\"true\"]:hover { border-color: %10; background: %11; }"
            "QToolButton[workspaceButton=\"true\"]:pressed { background: %12; }"
            "QToolButton[workspaceButton=\"true\"][workspaceActive=\"true\"] {"
            " border: 2px solid %10; background: %11; color: %9; font-weight: 600; }"
            "QToolButton[workspaceButton=\"true\"][workspaceActive=\"true\"]:hover { background: %12; }"
        )
        .arg(kButtonHeight)
        .arg(kControlHeight)
        .arg(theme.textSecondaryColor.name())
        .arg(theme.surfaceAltBackground.name())
        .arg(theme.borderColor.name())
        .arg(kMenuRowPaddingV)
        .arg(theme.panelBackground.name())
        .arg(theme.surfaceBackground.name())
        .arg(theme.textPrimaryColor.name())
        .arg(theme.accentColor.name())
        .arg(theme.hoverBackgroundColor.name())
        .arg(theme.pressedBackgroundColor.name());
    }
}
