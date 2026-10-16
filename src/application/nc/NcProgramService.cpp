#include "application/nc/NcProgramService.h"
#include "core/diagnostics/SummaryLog.h"

#include "application/machine/MachineTrajectoryService.h"
#include "application/cam/CamValidation.h"
#include "application/nc/GeometrySnapshotNcInputBuilder.h"
#include "core/nc/NcProgramBuilder.h"
#include "core/nc/PlanarNcProgramBuilder.h"

namespace
{
    void fillRevisionContext(Diagnostic& diagnostic, const GeometrySnapshot& snapshot)
    {
        diagnostic.context.insert(QStringLiteral("contentRevision"),
            QVariant::fromValue<qulonglong>(snapshot.contentRevision));
    }
}

OperationResult<cadcam::nc::NcProgram> NcProgramService::buildPlanarProgram
(
    const GeometrySnapshot& snapshot,
    const cadcam::process::DocumentProcessState& processState,
    const cadcam::planning::ProcessPlan& processPlan,
    const MachiningProcessConfig& processConfig,
    const OperationContext& context
) const
{
    OperationResult<cadcam::nc::NcProgram> result;

    // NC 只校验自己收到的数据：快照、计划与加工状态必须互相对应。
    if (processPlan.mode != cadcam::planning::ProcessPlanMode::Planar3Axis
        || snapshot.contentRevision != processPlan.contentRevision
        || processPlan.processStateRevision != processState.revision())
    {
        result.status = OperationStatus::Conflict;
        Diagnostic diagnostic;
        diagnostic.code = processPlan.mode != cadcam::planning::ProcessPlanMode::Planar3Axis
            ? DiagnosticCode::ProcessPlanModeMismatch
            : DiagnosticCode::PlanarNcRevisionMismatch;
        diagnostic.severity = DiagnosticSeverity::Error;
        diagnostic.component = QStringLiteral("NcProgramService");
        diagnostic.operation = context.operationName;
        diagnostic.stage = QStringLiteral("validate-planar-revision");
        diagnostic.userMessage = processPlan.mode != cadcam::planning::ProcessPlanMode::Planar3Axis
            ? QStringLiteral("三轴 NC 只能使用三轴加工计划。")
            : QStringLiteral("几何快照、加工计划或加工状态版本不一致，三轴导出已拒绝。");
        diagnostic.correlationId = context.correlationId;
        fillRevisionContext(diagnostic, snapshot);
        result.addDiagnostic(diagnostic);
        return result;
    }

    auto capture = GeometrySnapshotNcInputBuilder::capturePlanar(snapshot, processPlan, context);
    result.mergeDiagnostics(capture);
    if (!capture.succeeded() || !capture.value.has_value())
    {
        result.status = capture.status;
        return result;
    }

    cadcam::nc::PlanarNcBuildPolicy policy;
    policy.clearance.retractClearance =
        processConfig.rotationSafetyClearance;
    policy.clearance.approachClearance =
        processConfig.sameZoneTransferClearance;
    cadcam::core::emitSummaryLog
    (
        QStringLiteral("MachineTrajectory"),
        QStringLiteral("Clearance"),
        QStringLiteral("mode=Planar3Axis retractClearance=%1 approachClearance=%2")
            .arg(policy.clearance.retractClearance, 0, 'g', 15)
            .arg(policy.clearance.approachClearance, 0, 'g', 15));
    auto program = cadcam::nc::PlanarNcProgramBuilder::build
        (snapshot.contentRevision, *capture.value, policy, context,
            processPlan.processStateRevision);
    result.mergeDiagnostics(program);
    if (!program.succeeded() || !program.value.has_value())
    {
        result.status = program.status;
        return result;
    }

    // 结果一致性检查：NC 程序必须仍然与输入计划、几何快照与加工状态对应。
    const auto validation = cadcam::cam::CamValidation::validateNcProgram
    (
        snapshot,
        processState,
        processPlan,
        std::nullopt,
        *program.value,
        context
    );
    result.mergeDiagnostics(validation);
    if (!validation.succeeded())
    {
        result.status = validation.status;
        return result;
    }

    result.status = capture.status == OperationStatus::PartialSuccess
        || program.status == OperationStatus::PartialSuccess
        ? OperationStatus::PartialSuccess : OperationStatus::Success;
    result.value = std::move(*program.value);
    return result;
}

OperationResult<cadcam::nc::NcProgram> NcProgramService::buildRotaryProgram
(
    const GeometrySnapshot& snapshot,
    const cadcam::process::DocumentProcessState& processState,
    const cadcam::planning::ProcessPlan& processPlan,
    const std::optional<cadcam::machining::TubeSectionModel>& tubeSection,
    const MachiningProcessConfig& processConfig,
    const OperationContext& context,
    const std::optional<cadcam::geometry::Vector2d>& explicitTubeCenter
) const
{
    OperationResult<cadcam::nc::NcProgram> result;
    TaskContext taskContext;
    taskContext.operationContext = context;
    MachineTrajectoryService trajectoryService;
    auto trajectory = trajectoryService.buildRotaryTrajectory
    (
        snapshot,
        processState,
        processPlan,
        tubeSection,
        processConfig,
        taskContext,
        explicitTubeCenter
    );
    result.mergeDiagnostics(trajectory);
    if (!trajectory.succeeded() || !trajectory.value.has_value())
    {
        result.status = trajectory.status;
        return result;
    }

    auto metadata = GeometrySnapshotNcInputBuilder::captureRotary(snapshot, processPlan, context);
    result.mergeDiagnostics(metadata);
    if (!metadata.succeeded() || !metadata.value.has_value())
    {
        result.status = metadata.status;
        return result;
    }
    if (snapshot.contentRevision != processPlan.contentRevision
        || processState.revision() != processPlan.processStateRevision
        || trajectory.value->contentRevision != processPlan.contentRevision
        || trajectory.value->processStateRevision != processPlan.processStateRevision)
    {
        result.status = OperationStatus::Conflict;
        Diagnostic diagnostic;
        diagnostic.code = DiagnosticCode::NcProgramRevisionMismatch;
        diagnostic.severity = DiagnosticSeverity::Error;
        diagnostic.component = QStringLiteral("NcProgramService");
        diagnostic.operation = context.operationName;
        diagnostic.stage = QStringLiteral("validate-revision");
        diagnostic.userMessage = QStringLiteral("轨迹、元数据、加工计划或几何快照版本不一致。");
        diagnostic.correlationId = context.correlationId;
        fillRevisionContext(diagnostic, snapshot);
        result.addDiagnostic(diagnostic);
        return result;
    }

    auto program = cadcam::nc::NcProgramBuilder::buildRotary
        (*trajectory.value, *metadata.value, context, tubeSection);
    result.mergeDiagnostics(program);
    if (!program.succeeded() || !program.value.has_value())
    {
        result.status = program.status;
        return result;
    }

    // 结果一致性检查：四轴 NC 同时与加工计划和生成它的机床轨迹对照。
    const auto validation = cadcam::cam::CamValidation::validateNcProgram
    (
        snapshot,
        processState,
        processPlan,
        trajectory.value,
        *program.value,
        context
    );
    result.mergeDiagnostics(validation);
    if (!validation.succeeded())
    {
        result.status = validation.status;
        return result;
    }

    result.status = program.status;
    result.value = std::move(*program.value);
    return result;
}
