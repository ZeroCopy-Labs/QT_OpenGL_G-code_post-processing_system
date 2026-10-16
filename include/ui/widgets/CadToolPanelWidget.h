#pragma once

#include "desktop/AppTheme.h"
#include "cad/editing/DrawStateMachine.h"

#include <QColor>
#include <QMap>
#include <QStringList>
#include <QVector>
#include <QWidget>

class QAction;
class QCheckBox;
class QComboBox;
class QFrame;
class QLabel;
class QMenu;
class QToolButton;

// CAD 工作区工具面板：绘图 / 修改 / 整理 / 图层 / 特性 / 画布显示。
// 只承载几何创建、编辑与整理入口；加工规划与导出入口位于 CAM 工作区面板。
class CadToolPanelWidget : public QWidget
{
    Q_OBJECT

public:
    explicit CadToolPanelWidget(QWidget* parent = nullptr);

    void setLayerNames(const QStringList& layerNames, const QMap<QString, QColor>& layerColors);
    void setLayerStatusText(const QString& text);
    void setPropertyStatusText(const QString& text);
    void setActiveLayerName(const QString& layerName);
    void setActiveColorState(const QColor& color, int colorIndex, const QColor& byLayerColor);
    void setMoveEnabled(bool enabled);
    void setModifyActionsEnabled(bool enabled);
    void setTheme(const AppThemeColors& theme);
    void setBackgroundGridVisible(bool enabled);

signals:
    void drawRequested(DrawType drawType);
    void moveRequested();
    void deleteRequested();
    void rotateRequested();
    void copyRequested();
    void scaleRequested();
    void arrayRequested();
    void circularArrayRequested();
    void mirrorRequested();
    void offsetRequested();
    void trimRequested();
    void extendRequested();
    void joinRequested();
    void filletRequested();
    void chamferRequested();
    void layerChangeRequested(const QString& layerName);
    void colorChangeRequested(int colorIndex);
    void importFileRequested();
    void deduplicateRequested();
    void backgroundGridVisibleChanged(bool enabled);

private:
    void buildUi();
    void applyTheme();
    QWidget* buildPanelFrame(const QString& title, QWidget* contentWidget, int preferredWidth = -1, QMenu* launcherMenu = nullptr, bool flexibleWidth = false);
    QWidget* buildDivider();
    QWidget* buildDrawPanel();
    QWidget* buildModifyPanel();
    QWidget* buildLayerPanel();
    QWidget* buildPropertyPanel();
    QWidget* buildCanvasDisplayPanel();
    void addDrawButton(QWidget* parent, const QString& text, DrawType drawType, int row, int column);
    void commitLayerChange(QComboBox* comboBox);
    void updateLayerComboIcons();
    void updateColorComboIcons(const QColor& activeColor, const QColor& byLayerColor);
    void setComboCurrentByData(QComboBox* comboBox, int value);

private:
    QToolButton* m_moveButton = nullptr;
    QToolButton* m_deleteButton = nullptr;
    QToolButton* m_rotateButton = nullptr;
    QToolButton* m_copyButton = nullptr;
    QToolButton* m_scaleButton = nullptr;
    QToolButton* m_arrayButton = nullptr;
    QLabel* m_layerStatusLabel = nullptr;
    QLabel* m_propertyStatusLabel = nullptr;
    QComboBox* m_layerComboBox = nullptr;
    QComboBox* m_propertyLayerComboBox = nullptr;
    QComboBox* m_colorComboBox = nullptr;
    QCheckBox* m_backgroundGridCheckBox = nullptr;
    QMenu* m_drawMoreMenu = nullptr;
    QAction* m_drawPointAction = nullptr;
    QAction* m_drawXlineAction = nullptr;
    QAction* m_drawRectangleAction = nullptr;
    QAction* m_drawPolygonAction = nullptr;
    QMenu* m_modifyMoreMenu = nullptr;
    QAction* m_rectangularArrayAction = nullptr;
    QAction* m_circularArrayAction = nullptr;
    QAction* m_mirrorAction = nullptr;
    QAction* m_offsetAction = nullptr;
    QAction* m_trimAction = nullptr;
    QAction* m_extendAction = nullptr;
    QAction* m_joinAction = nullptr;
    QAction* m_filletAction = nullptr;
    QAction* m_chamferAction = nullptr;
    QToolButton* m_importFileButton = nullptr;
    QToolButton* m_deduplicateButton = nullptr;
    QMap<QString, QColor> m_layerColors;
    QVector<QToolButton*> m_drawButtons;
    QVector<QFrame*> m_dividers;
    AppThemeColors m_theme = buildAppThemeColors(AppThemeMode::Light);
    bool m_updatingUi = false;
};
