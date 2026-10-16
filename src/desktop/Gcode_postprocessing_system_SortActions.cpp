#include "platform/pch.h"

#include "desktop/Gcode_postprocessing_system.h"
#include "core/diagnostics/SummaryLog.h"

#include "cad/items/CadItem.h"
#include "cad/items/CadCircleItem.h"
#include "cad/items/CadEllipseItem.h"
#include "cad/geometry/CadEllipseGeometry.h"
#include "cad/geometry/CadOcsGeometry.h"
#include "cad/view/rendering/CadProcessVisualUtils.h"
#include "application/machining/RotaryCutBoundaryAnalyzer.h"
#include "application/machining/RotaryPathTopology.h"
#include "application/planning/ProcessPlanningService.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QHash>
#include <QMessageBox>
#include <QSet>
#include <QStatusBar>
#include <QStringList>

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <vector>
#include "desktop/internal/ProcessActionSupport.h"


bool Gcode_postprocessing_system::sortEntitiesByCurrentMode
(cadcam::planning::ProcessSortIntent sortIntent)
{
    const bool rebuildSequence =
        sortIntent == cadcam::planning::ProcessSortIntent::RebuildSequence;
    const GGenerator::GenerationMode generationMode = resolveGenerationMode();
    if (generationMode == GGenerator::GenerationMode::Mode3D)
    {
        return sortEntitiesWithProcessPlan3D
        (
            rebuildSequence ? QStringLiteral("4轴(绕A)智能排序")
                : QStringLiteral("4轴(绕A)排序"),
            sortIntent
        );
    }

    const int excludedCount = refreshWasteProcessingExclusions();

    if (excludedCount > 0)
    {
        ui->openGLWidget->appendCommandMessage
        (
            QStringLiteral("废面规则已排除 %1 个图元，本次排序不会处理这些图元。").arg(excludedCount)
        );
    }

    return sortEntitiesWithProcessPlan2D
    (
        rebuildSequence ? QStringLiteral("3轴智能排序") : QStringLiteral("3轴排序"),
        sortIntent
    );
}

OperationResult<RotaryBoundaryOperationGeometry>
Gcode_postprocessing_system::buildRotaryBoundaryOperationGeometry
(
    const QVector<CadItem*>& requiredItems
) const
{
    OperationResult<RotaryBoundaryOperationGeometry> result;
    RotaryBoundaryOperationGeometry geometry;
    geometry.documentRevision = m_document.contentRevision();
    geometry.connectionTolerance = kEndCutConnectionTolerance;
    geometry.sceneItems.reserve(static_cast<qsizetype>(m_document.m_entities.size()));

    // 加工断面操作的候选场景包含文档中的全部图元；
    // requiredItems 仅用于调用方在场景之外补充的图元。
    const QSet<CadItem*> requiredSet(requiredItems.begin(), requiredItems.end());
    for (const std::unique_ptr<CadItem>& entity : m_document.m_entities)
    {
        if (entity == nullptr)
        {
            continue;
        }
        geometry.sceneItems.push_back(entity.get());
    }
    Q_UNUSED(requiredSet);

    {
        BoundaryPerformanceTimer topologyTimer
            (m_boundaryAssignmentPerformanceReport != nullptr
                ? &m_boundaryAssignmentPerformanceReport->topologyBuildMs : nullptr);
        if (m_boundaryAssignmentPerformanceReport != nullptr)
        {
            ++m_boundaryAssignmentPerformanceReport->topologyBuildCount;
        }
        geometry.topology = std::make_unique<RotaryPathTopology>
        (
            geometry.sceneItems,
            RotaryPathTopologyTolerance::fromConnectionTolerance
                (geometry.connectionTolerance),
            pathRebuildObserver(m_boundaryAssignmentPerformanceReport),
            m_boundaryAssignmentPerformanceReport != nullptr
                ? &m_boundaryAssignmentPerformanceReport->topologyMetrics : nullptr
        );
    }

    if (geometry.documentRevision != m_document.contentRevision())
    {
        result.status = OperationStatus::Conflict;
        Diagnostic diagnostic;
        diagnostic.code = DiagnosticCode::TopologyBuildFailure;
        diagnostic.severity = DiagnosticSeverity::Error;
        diagnostic.component = QStringLiteral("BoundaryAssignment");
        diagnostic.operation = QStringLiteral("BuildOperationGeometry");
        diagnostic.stage = QStringLiteral("RevisionValidation");
        diagnostic.userMessage = QStringLiteral("文档已发生变化，请重新执行加工断面操作。");
        diagnostic.technicalDetail = QStringLiteral
            ("document revision changed while building operation topology");
        diagnostic.context.insert(QStringLiteral("capturedRevision"),
            static_cast<qulonglong>(geometry.documentRevision));
        diagnostic.context.insert(QStringLiteral("currentRevision"),
            static_cast<qulonglong>(m_document.contentRevision()));
        result.addDiagnostic(diagnostic);
        return result;
    }

    result.status = geometry.topology->status();
    result.diagnostics = geometry.topology->diagnostics();
    if (result.status != OperationStatus::Success
        && result.status != OperationStatus::PartialSuccess)
    {
        return result;
    }

    result.value.emplace(std::move(geometry));
    return result;
}

bool Gcode_postprocessing_system::sortEntitiesByCurrentDirection()
{
    return sortEntitiesWithProcessPlan2D
        (QStringLiteral("3轴排序"),
            cadcam::planning::ProcessSortIntent::PreserveCurrentSequence);
}

bool Gcode_postprocessing_system::assignSelectedEntityProcessOrder()
{
    return sortEntitiesWithProcessPlan2D
        (QStringLiteral("3轴排序"),
            cadcam::planning::ProcessSortIntent::PreserveCurrentSequence);
}

bool Gcode_postprocessing_system::smartSortEntities()
{
    return sortEntitiesWithProcessPlan2D
        (QStringLiteral("3轴智能排序"),
            cadcam::planning::ProcessSortIntent::RebuildSequence);
}

bool Gcode_postprocessing_system::sortEntitiesWithProcessPlan2D
(
    const QString& commandTitle,
    cadcam::planning::ProcessSortIntent sortIntent
)
{
    if (m_document.m_entities.empty())
    {
        QMessageBox::warning(this, commandTitle, QStringLiteral("当前文档为空，无法执行排序。"));
        return false;
    }

    std::vector<CadItem*> dedupCandidates;
    for (const std::unique_ptr<CadItem>& entity : m_document.m_entities)
    {
        if (entity != nullptr && buildProcessVisualInfo(entity.get()).valid)
            dedupCandidates.push_back(entity.get());
    }
    const SortableDedupResult dedupResult = deduplicateSortableItems(dedupCandidates);
    if (!dedupResult.duplicateItems.isEmpty()
        && !m_editer.deleteEntities(dedupResult.duplicateItems))
    {
        QMessageBox::warning(this, commandTitle, QStringLiteral("重复图元自动去重失败，排序已中止。"));
        return false;
    }

    cadcam::planning::PlanarProcessPlanningPolicy policy;
    policy.sortIntent = sortIntent;
    policy.allowReverse = true;
    policy.preserveUserDirection =
        sortIntent == cadcam::planning::ProcessSortIntent::PreserveCurrentSequence;
    policy.initialPosition = { 0.0, 0.0, 0.0 };
    policy.hasInitialPosition = true;
    policy.numericalEpsilon = 1.0e-5;
    const OperationContext context = createOperationContext(QStringLiteral("BuildAndApplyPlanarProcessPlan"));
    // 规划输入来自 CamJob 持有的几何快照：过期时按需重建，规划不再读取 CadDocument。
    if (!ensureCurrentGeometrySnapshot(context))
    {
        QMessageBox::warning(this, commandTitle, QStringLiteral("无法建立当前几何快照，排序已中止。"));
        return false;
    }

    ProcessPlanningService service;
    auto plan = service.buildPlanarPlan
        (*m_camJob.geometrySnapshot(), m_camJob.processState(), policy, context);
    if (!plan.succeeded() || !plan.value.has_value())
    {
        QString message = QStringLiteral("无法生成三轴加工计划。");
        for (const Diagnostic& diagnostic : plan.diagnostics)
        {
            if (!diagnostic.userMessage.trimmed().isEmpty())
            {
                message = diagnostic.userMessage;
                break;
            }
        }
        ui->openGLWidget->appendCommandMessage(QStringLiteral("[加工计划] %1").arg(message));
        ui->openGLWidget->refreshCommandPrompt();
        statusBar()->showMessage(message, 8000);
        QMessageBox::warning(this, commandTitle, message);
        return false;
    }

    cadcam::planning::ProcessPlan resolvedPlan = std::move(*plan.value);
    // 发布守卫：规划期间文档已变化时拒绝写入计划。
    if (m_document.contentRevision() != resolvedPlan.contentRevision)
    {
        const QString message = QStringLiteral("CAD 几何在规划期间发生变化，本次排序结果未应用。");
        ui->openGLWidget->appendCommandMessage(QStringLiteral("[加工计划] %1").arg(message));
        ui->openGLWidget->refreshCommandPrompt();
        statusBar()->showMessage(message, 8000);
        QMessageBox::warning(this, commandTitle, message);
        return false;
    }
    m_camJob.processState().setProcessUnitSequence(resolvedPlan.processUnitSequence.units);
    resolvedPlan.processUnitSequence = m_camJob.processState().processUnitSequence();
    resolvedPlan.processStateRevision = m_camJob.processState().revision();
    m_camJob.setProcessPlan(std::move(resolvedPlan));
    auto presentation = cadcam::process::ProcessPresentationSnapshot::build
        (*m_camJob.processPlan(), context);
    if (!presentation.succeeded() || !presentation.value.has_value())
    {
        invalidateCurrentProcessPlan();
        QMessageBox::warning(this, commandTitle, QStringLiteral("加工计划显示快照构建失败。"));
        return false;
    }
    m_processPresentation = std::move(*presentation.value);
    ui->openGLWidget->setProcessPresentation(&*m_processPresentation);
    ui->openGLWidget->update();
    const QString message = QStringLiteral("%1完成：最近距离排序，共安排 %2 个图元，排除 %3 个图元。%4")
        .arg(commandTitle)
        .arg(m_camJob.processPlan()->assignments.size())
        .arg(m_camJob.processPlan()->exclusions.size())
        .arg(dedupResult.removedCount > 0
            ? QStringLiteral("已自动删除 %1 个重复图元。").arg(dedupResult.removedCount)
            : QString());
    ui->openGLWidget->appendCommandMessage(message);
    ui->openGLWidget->refreshCommandPrompt();
    statusBar()->showMessage(message, 5000);
    return true;
}

bool Gcode_postprocessing_system::sortEntitiesByCurrentDirection3D()
{
    return sortEntitiesWithProcessPlan3D
        (QStringLiteral("4轴(绕A)排序"),
            cadcam::planning::ProcessSortIntent::PreserveCurrentSequence);
}

bool Gcode_postprocessing_system::sortEntitiesWithProcessPlan3D
(
    const QString& commandTitle,
    cadcam::planning::ProcessSortIntent sortIntent
)
{
    if (m_document.m_entities.empty())
    {
        QMessageBox::warning(this, commandTitle, QStringLiteral("当前文档为空，无法执行排序。"));
        return false;
    }
    if (!documentContainsThreeDimensionalGeometry(m_document))
    {
        QMessageBox::warning(this, commandTitle, QStringLiteral("当前文档未检测到可用于4轴排序的有效路径。"));
        return false;
    }

    std::vector<CadItem*> dedupCandidates;
    for (const std::unique_ptr<CadItem>& entity : m_document.m_entities)
    {
        if (entity == nullptr) continue;
        const CadProcessVisualInfo info = buildProcessVisualInfo(entity.get());
        const auto state = m_camJob.processState().stateOrDefault(entity->m_entityId);
        const bool isBoundary = state.overrideData.boundaryRole != cadcam::planning::BoundaryRole::None
            && state.overrideData.boundaryPairId >= 0;
        if (info.valid || isBoundary) dedupCandidates.push_back(entity.get());
    }
    const SortableDedupResult dedupResult = deduplicateSortableItems(dedupCandidates);
    if (!dedupResult.duplicateItems.isEmpty()
        && !m_editer.deleteEntities(dedupResult.duplicateItems))
    {
        QMessageBox::warning(this, commandTitle, QStringLiteral("重复图元自动去重失败，排序已中止。"));
        return false;
    }

    cadcam::planning::ProcessPlanningPolicy policy;
    policy.sortIntent = sortIntent;
    policy.orderingStrategy = m_activeProfile.rotaryAxisConfig().lazyRotationProcessing
        ? cadcam::planning::ProcessOrderingStrategy::LazyRotation
        : cadcam::planning::ProcessOrderingStrategy::NearestNext;
    policy.connectionTolerance = kEndCutConnectionTolerance;
    policy.rotationSafetyClearance =
        m_activeProfile.toolTransferConfig().rotationSafetyClearance;
    policy.sameZoneTransferClearance =
        m_activeProfile.toolTransferConfig().sameZoneTransferClearance;
    policy.coordinatedTransferEnabled =
        m_activeProfile.toolTransferConfig().coordinatedTransferEnabled;
    policy.sameZone = m_activeProfile.toolTransferConfig().sameZone;
    policy.rotaryAxisY = m_activeProfile.rotaryAxisConfig().centerY;
    policy.rotaryAxisZ = m_activeProfile.rotaryAxisConfig().centerZ;
    policy.invertAAxisDirection =
        m_activeProfile.rotaryAxisConfig().invertAAxisDirection;
    policy.aAxisOffsetDegrees =
        m_activeProfile.rotaryAxisConfig().aAxisOffsetDegrees;
    policy.keepContinuousAngle =
        m_activeProfile.rotaryAxisConfig().keepContinuousAngle;
    policy.useInitialMachinePoint =
        m_activeProfile.rotaryAxisConfig().useInitialMachinePoint;
    policy.initialMachinePoint =
    {
        m_activeProfile.rotaryAxisConfig().initialMachineX,
        m_activeProfile.rotaryAxisConfig().initialMachineY,
        m_activeProfile.rotaryAxisConfig().initialMachineZ,
        0.0
    };
    policy.machiningPlaneZOffset =
        m_activeProfile.rotaryAxisConfig().machiningPlaneZOffset;
    policy.overcutDistance =
        m_activeProfile.rotaryAxisConfig().overcutDistance;
    policy.allowReverse = true;
    policy.preserveClosedLoopsAsAtomicGroups = true;
    policy.initialPosition = { 0.0, 0.0, 500.0 };
    policy.zone16Sweep.initialZone =
        cadcam::machining::TubeZone16::TopFace;
    policy.zone16Sweep.perimeterDirection =
        m_activeProfile.rotaryAxisConfig().perimeterSweepDirection
            == GProfilePerimeterSweepDirection::Clockwise
        ? cadcam::planning::PerimeterSweepDirection::Clockwise
        : cadcam::planning::PerimeterSweepDirection::CounterClockwise;
    policy.zone16Sweep.longitudinalDirection =
        m_activeProfile.rotaryAxisConfig().longitudinalSweepDirection
            == GProfileLongitudinalSweepDirection::PositiveX
        ? cadcam::planning::LongitudinalSweepDirection::PositiveX
        : cadcam::planning::LongitudinalSweepDirection::NegativeX;

    const OperationContext context = createOperationContext(QStringLiteral("BuildAndApplyProcessPlan3D"));
    const std::optional<cadcam::machining::TubeSectionModel> section =
        m_rotaryTubeSectionModel.coreModel;
    // 规划输入来自 CamJob 持有的几何快照：过期时按需重建，规划不再读取 CadDocument。
    if (!ensureCurrentGeometrySnapshot(context))
    {
        QMessageBox::warning(this, commandTitle, QStringLiteral("无法建立当前几何快照，排序已中止。"));
        return false;
    }

    ProcessPlanningService service;
    auto planResult = service.buildRotaryPlan
        (*m_camJob.geometrySnapshot(), m_camJob.processState(), section, policy, context);
    if (!planResult.succeeded() || !planResult.value.has_value())
    {
        QString message = QStringLiteral("无法生成加工计划。");
        for (const Diagnostic& diagnostic : planResult.diagnostics)
        {
            if (!diagnostic.userMessage.trimmed().isEmpty())
            {
                message = diagnostic.userMessage;
                break;
            }
        }
        ui->openGLWidget->appendCommandMessage(QStringLiteral("[加工计划] %1").arg(message));
        ui->openGLWidget->refreshCommandPrompt();
        statusBar()->showMessage(message, 8000);
        QMessageBox::warning(this, commandTitle, message);
        return false;
    }

    cadcam::planning::ProcessPlan resolvedPlan = std::move(*planResult.value);
    // 发布守卫：规划期间文档已变化时拒绝写入计划。
    if (m_document.contentRevision() != resolvedPlan.contentRevision)
    {
        const QString message = QStringLiteral("CAD 几何在规划期间发生变化，本次排序结果未应用。");
        ui->openGLWidget->appendCommandMessage(QStringLiteral("[加工计划] %1").arg(message));
        ui->openGLWidget->refreshCommandPrompt();
        statusBar()->showMessage(message, 8000);
        QMessageBox::warning(this, commandTitle, message);
        return false;
    }
    m_camJob.processState().setProcessUnitSequence(resolvedPlan.processUnitSequence.units);
    resolvedPlan.processUnitSequence = m_camJob.processState().processUnitSequence();
    resolvedPlan.processStateRevision = m_camJob.processState().revision();
    m_camJob.setProcessPlan(std::move(resolvedPlan));
    auto presentation = cadcam::process::ProcessPresentationSnapshot::build
        (*m_camJob.processPlan(), context);
    if (!presentation.succeeded() || !presentation.value.has_value())
    {
        invalidateCurrentProcessPlan();
        QMessageBox::warning(this, commandTitle, QStringLiteral("加工计划显示快照构建失败。"));
        return false;
    }
    m_processPresentation = std::move(*presentation.value);
    ui->openGLWidget->setProcessPresentation(&*m_processPresentation);
    ui->openGLWidget->update();
    const QString strategyText = policy.orderingStrategy == cadcam::planning::ProcessOrderingStrategy::LazyRotation
        ? QStringLiteral("懒旋转加工")
        : QStringLiteral("普通最近距离排序");
    const QString message = QStringLiteral("%1完成：%2，共安排 %3 个图元，排除 %4 个图元。%5")
        .arg(commandTitle)
        .arg(strategyText)
        .arg(m_camJob.processPlan()->assignments.size())
        .arg(m_camJob.processPlan()->exclusions.size())
        .arg(dedupResult.removedCount > 0
            ? QStringLiteral("已自动删除 %1 个重复图元。").arg(dedupResult.removedCount)
            : QString());
    ui->openGLWidget->appendCommandMessage(message);
    ui->openGLWidget->refreshCommandPrompt();
    statusBar()->showMessage(message, 5000);
    return true;
}

bool Gcode_postprocessing_system::smartSortEntities3D()
{
    return sortEntitiesWithProcessPlan3D
        (QStringLiteral("4轴(绕A)智能排序"),
            cadcam::planning::ProcessSortIntent::RebuildSequence);
}


GGenerator::GenerationMode Gcode_postprocessing_system::resolveGenerationMode() const
{
    switch (m_generationPreference)
    {
    case GCodeGenerationPreference::Force2D:
        return GGenerator::GenerationMode::Mode2D;
    case GCodeGenerationPreference::Force3D:
        return GGenerator::GenerationMode::Mode3D;
    case GCodeGenerationPreference::Auto:
    default:
        return documentContainsThreeDimensionalGeometry(m_document)
            ? GGenerator::GenerationMode::Mode3D
            : GGenerator::GenerationMode::Mode2D;
    }
}