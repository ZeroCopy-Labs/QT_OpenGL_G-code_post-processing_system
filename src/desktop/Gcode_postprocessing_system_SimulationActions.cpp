// Desktop CAM 仿真实现：准备仿真会话、播放控制与状态同步。
//
// 仿真只读取 CamJob 当前有效的加工计划与既有运动结果：
// 四轴使用 MachineTrajectoryService，三轴使用既有结构化 NC 运动桥接。
// 仿真不修改 CAD 文档、加工状态、加工计划，也不产生 Undo/Redo。
#include "platform/pch.h"

#include "desktop/Gcode_postprocessing_system.h"

#include "application/cam/CamValidation.h"
#include "application/machine/MachineTrajectoryService.h"
#include "application/nc/NcProgramService.h"
#include "application/simulation/SimulationTrajectoryAdapter.h"
#include "cad/view/rendering/SimulationRenderLayer.h"
#include "core/machine/MachineTrajectory.h"
#include "core/nc/NcProgram.h"
#include "infrastructure/config/MachiningProcessConfig.h"

#include <QMessageBox>
#include <QStatusBar>

#include <utility>

namespace
{
    constexpr int kSimulationTimerIntervalMs = 16;

    QString simulationStateDisplayName(cadcam::simulation::SimulationPlaybackState state)
    {
        using State = cadcam::simulation::SimulationPlaybackState;
        switch (state)
        {
        case State::Empty: return QStringLiteral("未准备");
        case State::Ready: return QStringLiteral("就绪");
        case State::Playing: return QStringLiteral("播放中");
        case State::Paused: return QStringLiteral("已暂停");
        case State::Finished: return QStringLiteral("已结束");
        }
        return QString();
    }

    QString simulationSourceDisplayName(cadcam::simulation::SimulationSourceMode mode)
    {
        return mode == cadcam::simulation::SimulationSourceMode::Rotary4Axis
            ? QStringLiteral("四轴(绕A)")
            : QStringLiteral("三轴");
    }
}

bool Gcode_postprocessing_system::prepareSimulation()
{
    const OperationContext context = createOperationContext(QStringLiteral("prepare-simulation"));

    // 仿真前置条件同样来自就绪投影：完全由当前数据计算，不单独保存状态。
    const cadcam::cam::CamReadiness readiness =
        cadcam::cam::evaluateCamReadiness(m_camJob, m_document.contentRevision(), m_simulation.isLoaded());
    if (!readiness.hasProcessPlan || !readiness.processPlanCurrent)
    {
        QMessageBox::warning(this, QStringLiteral("仿真"),
            QStringLiteral("当前没有有效的加工计划，请先完成 CAM Planning。"));
        return false;
    }
    if (!readiness.geometrySnapshotCurrent)
    {
        QMessageBox::warning(this, QStringLiteral("仿真"),
            QStringLiteral("当前几何快照已失效，请重新执行 Planning。"));
        return false;
    }

    // 数据一致性验证：只读检查，不修复任何状态。运动结果本身由生成服务在返回前验证。
    const OperationReport planValidation = cadcam::cam::CamValidation::validateProcessPlan
    (
        *m_camJob.geometrySnapshot(),
        m_camJob.processState(),
        *m_camJob.processPlan(),
        context
    );
    if (!planValidation.succeeded())
    {
        const QString message = planValidation.diagnostics.isEmpty()
            ? QStringLiteral("当前加工计划未通过一致性验证，请重新执行 Planning。")
            : planValidation.diagnostics.constLast().userMessage;
        QMessageBox::warning(this, QStringLiteral("仿真"), message);
        return false;
    }

    const cadcam::planning::ProcessPlan& plan = *m_camJob.processPlan();
    const GeometrySnapshot& snapshot = *m_camJob.geometrySnapshot();
    const cadcam::process::DocumentProcessState& processState = m_camJob.processState();

    cadcam::simulation::SimulationTrajectory simulationTrajectory;

    if (plan.mode == cadcam::planning::ProcessPlanMode::Rotary4Axis)
    {
        // 四轴：直接复用既有四轴轨迹结果，不重新推导刀路。
        std::optional<cadcam::geometry::Vector2d> explicitCenter;
        if (m_rotaryTubeSectionModel.centerValid)
        {
            explicitCenter = cadcam::geometry::Vector2d
            {
                m_rotaryTubeSectionModel.centerY,
                m_rotaryTubeSectionModel.centerZ
            };
        }

        MachiningProcessConfig processConfig = MachiningProcessConfig::fromProfile(m_activeProfile);
        processConfig.tubeWallThickness = rotaryTubeWallThickness();
        processConfig.cutSurfacePositiveCorrection = m_camJob.settings().cutSurfacePositiveCorrection;
        processConfig.cutSurfaceNegativeCorrection = m_camJob.settings().cutSurfaceNegativeCorrection;

        TaskContext taskContext;
        taskContext.operationContext = context;
        MachineTrajectoryService trajectoryService;
        auto trajectory = trajectoryService.buildRotaryTrajectory
        (
            snapshot,
            processState,
            plan,
            m_rotaryTubeSectionModel.coreModel,
            processConfig,
            taskContext,
            explicitCenter
        );
        if (!trajectory.succeeded() || !trajectory.value.has_value())
        {
            const QString message = trajectory.diagnostics.isEmpty()
                ? QStringLiteral("无法生成四轴仿真轨迹。")
                : trajectory.diagnostics.constLast().userMessage;
            QMessageBox::warning(this, QStringLiteral("仿真"), message);
            return false;
        }

        simulationTrajectory =
            SimulationTrajectoryAdapter::fromMachineTrajectory(*trajectory.value);
    }
    else
    {
        // 三轴：当前唯一完整保存三轴结构化运动的是 NcProgram，因此按内存桥接使用；
        // 不生成 G-code 文本、不做后处理、不写文件、也不解析 G-code。
        MachiningProcessConfig processConfig = MachiningProcessConfig::fromProfile(m_activeProfile);
        processConfig.tubeWallThickness = rotaryTubeWallThickness();
        processConfig.cutSurfacePositiveCorrection = m_camJob.settings().cutSurfacePositiveCorrection;
        processConfig.cutSurfaceNegativeCorrection = m_camJob.settings().cutSurfaceNegativeCorrection;

        NcProgramService ncService;
        auto program = ncService.buildPlanarProgram(snapshot, processState, plan, processConfig, context);
        if (!program.succeeded() || !program.value.has_value())
        {
            const QString message = program.diagnostics.isEmpty()
                ? QStringLiteral("无法生成三轴仿真运动。")
                : program.diagnostics.constLast().userMessage;
            QMessageBox::warning(this, QStringLiteral("仿真"), message);
            return false;
        }

        simulationTrajectory = SimulationTrajectoryAdapter::fromNcProgram(*program.value);
    }

    if (simulationTrajectory.moves.empty())
    {
        QMessageBox::warning(this, QStringLiteral("仿真"),
            QStringLiteral("当前加工计划没有可仿真的机床运动。"));
        return false;
    }

    const std::size_t moveCount = simulationTrajectory.moves.size();
    const cadcam::simulation::SimulationSourceMode sourceMode = simulationTrajectory.sourceMode;

    m_simulation.load(std::move(simulationTrajectory));
    if (ui != nullptr && ui->openGLWidget != nullptr)
    {
        ui->openGLWidget->simulationRenderLayer()->setTrajectory(*m_simulation.trajectory());
        ui->openGLWidget->simulationRenderLayer()->setVisible(true);
        publishSimulationSnapshot(false);
        ui->openGLWidget->update();
    }

    if (m_camToolPanelWidget != nullptr)
    {
        m_camToolPanelWidget->setSimulationControlsEnabled(true);
        m_camToolPanelWidget->setSimulationPlaying(false);
    }

    const QString message = QStringLiteral("仿真已准备：%1，共 %2 段运动。")
        .arg(simulationSourceDisplayName(sourceMode))
        .arg(moveCount);
    ui->openGLWidget->appendCommandMessage(message);
    ui->openGLWidget->refreshCommandPrompt();
    statusBar()->showMessage(message, 4000);
    publishSimulationSnapshot(true);
    return true;
}

void Gcode_postprocessing_system::clearSimulation()
{
    m_simulation.clear();

    if (m_simulationTimer != nullptr)
    {
        m_simulationTimer->stop();
    }

    if (ui != nullptr && ui->openGLWidget != nullptr)
    {
        ui->openGLWidget->simulationRenderLayer()->clearTrajectory();
        ui->openGLWidget->update();
    }

    if (m_camToolPanelWidget != nullptr)
    {
        m_camToolPanelWidget->setSimulationControlsEnabled(false);
        m_camToolPanelWidget->setSimulationPlaying(false);
        m_camToolPanelWidget->setSimulationStateText(QStringLiteral("未准备"));
        m_camToolPanelWidget->setSimulationMotionText(QStringLiteral("-"));
        m_camToolPanelWidget->setSimulationMoveText(QStringLiteral("Move：0 / 0"));
        m_camToolPanelWidget->setSimulationAxisAlignmentText(QStringLiteral("A：-"));
        m_camToolPanelWidget->setSimulationProgress(0.0);
    }
}

void Gcode_postprocessing_system::startSimulationPlayback()
{
    if (!m_simulation.isLoaded())
    {
        QMessageBox::warning(this, QStringLiteral("仿真"), QStringLiteral("请先准备仿真。"));
        return;
    }

    // 已结束时再次播放从起点重新开始，避免停留在终点无反馈。
    if (m_simulation.isFinished())
    {
        m_simulation.reset();
    }

    m_simulation.play();
    if (m_simulationTimer != nullptr && !m_simulationTimer->isActive())
    {
        m_simulationFrameTimer.restart();
        m_simulationTimer->start(kSimulationTimerIntervalMs);
    }

    if (m_camToolPanelWidget != nullptr)
    {
        m_camToolPanelWidget->setSimulationPlaying(true);
    }
    publishSimulationSnapshot(true);
}

void Gcode_postprocessing_system::pauseSimulationPlayback()
{
    if (!m_simulation.isLoaded())
    {
        return;
    }

    m_simulation.pause();
    if (m_simulationTimer != nullptr)
    {
        m_simulationTimer->stop();
    }

    if (m_camToolPanelWidget != nullptr)
    {
        m_camToolPanelWidget->setSimulationPlaying(false);
    }
    publishSimulationSnapshot(true);
}

void Gcode_postprocessing_system::stepSimulation()
{
    if (!m_simulation.isLoaded())
    {
        QMessageBox::warning(this, QStringLiteral("仿真"), QStringLiteral("请先准备仿真。"));
        return;
    }

    m_simulation.step();
    if (m_simulationTimer != nullptr)
    {
        m_simulationTimer->stop();
    }

    if (m_camToolPanelWidget != nullptr)
    {
        m_camToolPanelWidget->setSimulationPlaying(false);
    }
    publishSimulationSnapshot(true);
}

void Gcode_postprocessing_system::resetSimulation()
{
    if (!m_simulation.isLoaded())
    {
        return;
    }

    m_simulation.reset();
    if (m_simulationTimer != nullptr)
    {
        m_simulationTimer->stop();
    }

    if (m_camToolPanelWidget != nullptr)
    {
        m_camToolPanelWidget->setSimulationPlaying(false);
    }
    publishSimulationSnapshot(true);
}

void Gcode_postprocessing_system::setSimulationSpeed(double speed)
{
    m_simulation.setSpeed(speed);
}

void Gcode_postprocessing_system::seekSimulation(double progress)
{
    if (!m_simulation.isLoaded())
    {
        return;
    }

    m_simulation.seek(progress);
    publishSimulationSnapshot(true);
}

void Gcode_postprocessing_system::advanceSimulation()
{
    if (!m_simulation.isPlaying())
    {
        return;
    }

    const double elapsedSeconds = m_simulationFrameTimer.isValid()
        ? static_cast<double>(m_simulationFrameTimer.restart()) / 1000.0
        : 0.0;
    m_simulation.advance(elapsedSeconds);
    publishSimulationSnapshot(false);

    if (!m_simulation.isPlaying())
    {
        // 播放到终点后停止定时器并恢复进度交互。
        if (m_simulationTimer != nullptr)
        {
            m_simulationTimer->stop();
        }
        if (m_camToolPanelWidget != nullptr)
        {
            m_camToolPanelWidget->setSimulationPlaying(false);
        }
        publishSimulationSnapshot(true);
    }
}

void Gcode_postprocessing_system::publishSimulationSnapshot(bool announceStatus)
{
    const cadcam::simulation::SimulationSnapshot snapshot = m_simulation.snapshot();

    if (ui != nullptr && ui->openGLWidget != nullptr)
    {
        ui->openGLWidget->simulationRenderLayer()->setSnapshot(snapshot);
        ui->openGLWidget->update();
    }

    if (m_camToolPanelWidget == nullptr)
    {
        return;
    }

    m_camToolPanelWidget->setSimulationStateText(simulationStateDisplayName(snapshot.state));
    m_camToolPanelWidget->setSimulationProgress(snapshot.overallProgress);

    if (!snapshot.hasTrajectory)
    {
        return;
    }

    m_camToolPanelWidget->setSimulationMotionText
    (
        QString::fromLatin1(cadcam::simulation::simulationMoveKindName(snapshot.currentKind))
    );
    m_camToolPanelWidget->setSimulationMoveText
    (
        QStringLiteral("Move：%1 / %2").arg(snapshot.moveIndex + 1U).arg(snapshot.moveCount)
    );
    m_camToolPanelWidget->setSimulationAxisAlignmentText
    (
        snapshot.currentPose.aDegrees.has_value()
            ? QStringLiteral("A：%1°").arg(*snapshot.currentPose.aDegrees, 0, 'f', 1)
            : QStringLiteral("A：-")
    );

    if (announceStatus && ui != nullptr && ui->openGLWidget != nullptr)
    {
        const QString message = QStringLiteral("仿真 %1：%2，Move %3 / %4")
            .arg(simulationStateDisplayName(snapshot.state))
            .arg(QString::fromLatin1(cadcam::simulation::simulationMoveKindName(snapshot.currentKind)))
            .arg(snapshot.moveIndex + 1U)
            .arg(snapshot.moveCount);
        statusBar()->showMessage(message, 2500);
    }
}
