#pragma once

#include "desktop/AppTheme.h"

#include <QList>
#include <QPair>
#include <QString>
#include <QVector>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QFrame;
class QTabWidget;
class QToolButton;

// CAM 工作区工具面板。
// 按加工阶段分为“规划 / 仿真 / NC 与后处理”三个子页，只负责 UI 组织与信号转发，
// 加工算法仍由 Application / Core 承担。
class CamToolPanelWidget : public QWidget
{
    Q_OBJECT

public:
    enum class GCodeModeSelection
    {
        Auto,
        ThreeAxis,
        FourAxisAroundA
    };

    explicit CamToolPanelWidget(QWidget* parent = nullptr);

    void setTheme(const AppThemeColors& theme);
    void setGCodeModeSelection(GCodeModeSelection selection);
    void setAvailableProfiles(const QList<QPair<QString, QString>>& profiles);
    void setCurrentProfileSelection(const QString& profileId);
    void setProcessVisualsVisible(bool enabled);
    bool processVisualsVisible() const;
    void setProcessDirectionVisible(bool enabled);
    void setProcessOrderVisible(bool enabled);
    void setRotaryEndCutsVisible(bool enabled);
    void setExcludedEntitiesDimmed(bool enabled);

signals:
    void recognizeRotaryTubeSectionRequested();
    void recognizeRotaryEndCutsRequested();
    void removeInternalPathsRequested();
    void machiningSettingsRequested();
    void sortKeepDirectionRequested();
    void smartSortRequested();
    void exportGCodeRequested();
    void gcodeModeSelectionChanged(CamToolPanelWidget::GCodeModeSelection selection);
    void profileSelectionChanged(const QString& profileId);
    void profileManagerRequested();
    void profileSettingsRequested();
    void processVisualsVisibleChanged(bool enabled);
    void processDirectionVisibleChanged(bool enabled);
    void processOrderVisibleChanged(bool enabled);
    void rotaryEndCutsVisibleChanged(bool enabled);
    void excludedEntitiesDimmedChanged(bool enabled);

private:
    void buildUi();
    void applyTheme();
    QWidget* buildPanelFrame
    (
        const QString& title,
        QWidget* contentWidget,
        int preferredWidth = -1,
        bool flexibleWidth = false
    );
    QWidget* buildDivider();
    QToolButton* buildMachiningButton(QWidget* parent, const QString& text);
    QWidget* buildPlanningStage();
    QWidget* buildSimulationStage();
    QWidget* buildNcPostStage();
    QWidget* buildMachiningDisplayPanel();

private:
    QTabWidget* m_stageTabs = nullptr;
    QToolButton* m_exportGCodeButton = nullptr;
    QToolButton* m_sortKeepDirectionButton = nullptr;
    QToolButton* m_smartSortButton = nullptr;
    QToolButton* m_profileManagerButton = nullptr;
    QToolButton* m_profileSettingsButton = nullptr;
    QComboBox* m_gcodeModeComboBox = nullptr;
    QComboBox* m_profileComboBox = nullptr;
    QCheckBox* m_processVisualsCheckBox = nullptr;
    QCheckBox* m_processDirectionCheckBox = nullptr;
    QCheckBox* m_processOrderCheckBox = nullptr;
    QCheckBox* m_rotaryEndCutsCheckBox = nullptr;
    QCheckBox* m_excludedEntitiesDimmedCheckBox = nullptr;
    QVector<QFrame*> m_dividers;
    AppThemeColors m_theme = buildAppThemeColors(AppThemeMode::Light);
    bool m_updatingUi = false;
};
