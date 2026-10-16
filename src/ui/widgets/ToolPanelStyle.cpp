#include "platform/pch.h"

#include "ui/widgets/ToolPanelStyle.h"

#include <QColor>

namespace toolpanel
{
    QString cssRgb(const QColor& color)
    {
        return QStringLiteral("rgb(%1, %2, %3)").arg(color.red()).arg(color.green()).arg(color.blue());
    }

    QString toolPanelStyleSheet(const AppThemeColors& theme)
    {
        return QStringLiteral
        (
            "#cadToolPanelRoot { background: transparent; }"
            "QTabWidget::pane { border: none; border-top: 1px solid %4; background: transparent; margin-top: 0px; }"
            "QTabBar::tab { background: transparent; color: %2; border: none; border-bottom: 2px solid transparent; padding: 6px 14px 5px 14px; min-width: 50px; font-size: 11px; }"
            "QTabBar::tab:hover { background: %5; color: %1; }"
            "QTabBar::tab:selected { background: %7; color: %1; border-bottom-color: %8; font-weight: 600; }"
            "QWidget[ribbonPanel=\"true\"] { background: %7; border: 1px solid %4; border-radius: 4px; }"
            "QLabel { color: %1; font-size: 10px; }"
            "QLabel[panelTitle=\"true\"] {"
            " color: %2;"
            " font-size: 10px;"
            " padding-bottom: 1px;"
            "}"
            "QLabel[machiningFieldLabel=\"true\"] {"
            " color: %2;"
            " font-size: 11px;"
            " font-weight: 500;"
            "}"
            "QLabel[sectionStatus=\"true\"] { font-size: 11px; font-weight: 600; }"
            "QLabel[sectionStatus=\"true\"][sectionRecognized=\"false\"][sectionBlinkOn=\"true\"] { color: rgb(220, 48, 48); }"
            "QLabel[sectionStatus=\"true\"][sectionRecognized=\"false\"][sectionBlinkOn=\"false\"] { color: rgba(220, 48, 48, 45); }"
            "QLabel[sectionStatus=\"true\"][sectionRecognized=\"true\"] { color: %8; }"
            "QLabel[sectionValue=\"true\"] { color: %1; font-size: 10px; }"
            "QCheckBox[machiningOption=\"true\"] {"
            " color: %2;"
            " font-size: 10px;"
            " spacing: 4px;"
            " padding: 0px 3px 0px 0px;"
            "}"
            "QCheckBox[machiningOption=\"true\"]::indicator {"
            " width: 13px;"
            " height: 13px;"
            "}"
            "QToolButton[panelLauncher=\"true\"] {"
            " border: none;"
            " padding: 0px;"
            " margin: 0px;"
            " color: %2;"
            " background: transparent;"
            "}"
            "QToolButton[panelLauncher=\"true\"]:hover {"
            " background: %3;"
            " color: %1;"
            "}"
            "QToolButton[ribbonButton=\"true\"] {"
            " border: 1px solid transparent;"
            " border-radius: 4px;"
            " padding: 2px 3px 2px 3px;"
            " background: transparent;"
            " color: %1;"
            " font-size: 9px;"
            "}"
            "QToolButton[ribbonButton=\"true\"]:hover {"
            " border-color: %4;"
            " background: %5;"
            "}"
            "QToolButton[ribbonButton=\"true\"]:pressed {"
            " border-color: %6;"
            " background: %3;"
            "}"
            "QToolButton[machiningButton=\"true\"] {"
            " border: 1px solid %4;"
            " border-radius: 4px;"
            " padding: 3px 10px;"
            " background: %7;"
            " color: %1;"
            " font-size: 11px;"
            " text-align: left;"
            "}"
            "QToolButton[machiningButton=\"true\"]:hover {"
            " border-color: %6;"
            " background: %5;"
            "}"
            "QToolButton[machiningButton=\"true\"]:pressed {"
            " border-color: %6;"
            " background: %3;"
            "}"
            "QComboBox {"
            " background-color: %7;"
            " color: %1;"
            " border: 1px solid %4;"
            " border-radius: 4px;"
            " padding: 1px 22px 1px 6px;"
            " font-size: 11px;"
            "}"
            "QComboBox:hover {"
            " border-color: %6;"
            "}"
            "QComboBox::drop-down {"
            " subcontrol-origin: padding;"
            " subcontrol-position: top right;"
            " width: 18px;"
            " border: none;"
            " background: transparent;"
            "}"
            "QComboBox QAbstractItemView {"
            " background-color: %7;"
            " color: %1;"
            " border: 1px solid %4;"
            " selection-background-color: %8;"
            " selection-color: %9;"
            "}"
        )
        .arg(cssRgb(theme.textPrimaryColor))
        .arg(cssRgb(theme.textSecondaryColor))
        .arg(cssRgb(theme.pressedBackgroundColor))
        .arg(cssRgb(theme.borderColor))
        .arg(cssRgb(theme.hoverBackgroundColor))
        .arg(cssRgb(theme.borderStrongColor))
        .arg(cssRgb(theme.surfaceBackground))
        .arg(cssRgb(theme.accentColor))
        .arg(cssRgb(theme.accentTextColor));
    }

    QString toolPanelPopupMenuStyleSheet(const AppThemeColors& theme)
    {
        return QStringLiteral
        (
            "QMenu[gridPopupMenu=\"true\"] {"
            " background-color: %1;"
            " border: 1px solid %3;"
            " padding: 0px;"
            "}"
            "QWidget[popupGridPanel=\"true\"] {"
            " background-color: %1;"
            "}"
            "QToolButton[popupGridButton=\"true\"] {"
            " border: 1px solid transparent;"
            " border-radius: 3px;"
            " padding: 3px;"
            " background: transparent;"
            " color: %2;"
            " font-size: 10px;"
            "}"
            "QToolButton[popupGridButton=\"true\"]:hover {"
            " border-color: %4;"
            " background: %5;"
            "}"
            "QToolButton[popupGridButton=\"true\"]:pressed {"
            " border-color: %6;"
            " background: %7;"
            "}"
            "QToolButton[popupGridButton=\"true\"]:disabled {"
            " color: %8;"
            "}"
        )
        .arg(cssRgb(theme.surfaceBackground))
        .arg(cssRgb(theme.textPrimaryColor))
        .arg(cssRgb(theme.borderColor))
        .arg(cssRgb(theme.borderStrongColor))
        .arg(cssRgb(theme.hoverBackgroundColor))
        .arg(cssRgb(theme.accentColor))
        .arg(cssRgb(theme.pressedBackgroundColor))
        .arg(cssRgb(theme.textSecondaryColor));
    }
}
