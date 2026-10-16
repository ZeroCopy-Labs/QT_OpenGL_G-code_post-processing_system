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
class QLabel;
class QSlider;
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

    // 仿真页显示状态；控件启用与状态文字由桌面层按仿真会话状态设置。
    void setSimulationControlsEnabled(bool enabled);
    void setSimulationPlaying(bool playing);
    void setSimulationStateText(const QString& text);
    void setSimulationMotionText(const QString& text);
    void setSimulationMoveText(const QString& text);
    void setSimulationAxisAlignmentText(const QString& text);
    void setSimulationProgress(double progress);
    void setSimulationSpeed(double speed);

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
    void prepareSimulationRequested();
    void playSimulationRequested();
    void pauseSimulationRequested();
    void stepSimulationRequested();
    void resetSimulationRequested();
    void simulationSpeedChanged(double speed);
    void simulationSeekRequested(double progress);

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
    QWidget* buildSimulationControlPanel();

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
    QToolButton* m_prepareSimulationButton = nullptr;
    QToolButton* m_playSimulationButton = nullptr;
    QToolButton* m_pauseSimulationButton = nullptr;
    QToolButton* m_stepSimulationButton = nullptr;
    QToolButton* m_resetSimulationButton = nullptr;
    QComboBox* m_simulationSpeedComboBox = nullptr;
    QSlider* m_simulationProgressSlider = nullptr;
    QLabel* m_simulationStateLabel = nullptr;
    QLabel* m_simulationMotionLabel = nullptr;
    QLabel* m_simulationMoveLabel = nullptr;
    QLabel* m_simulationAxisLabel = nullptr;
    QVector<QFrame*> m_dividers;
    AppThemeColors m_theme = buildAppThemeColors(AppThemeMode::Light);
    bool m_updatingUi = false;
    bool m_simulationControlsEnabled = false;
};
