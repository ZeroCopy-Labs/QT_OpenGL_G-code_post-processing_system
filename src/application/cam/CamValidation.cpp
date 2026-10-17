#include "platform/pch.h"

#include "application/cam/CamValidation.h"

#include <QSet>

#include <cmath>
#include <map>
#include <set>
#include <vector>

namespace
{
    using cadcam::geometry::EntityId;
    using cadcam::geometry::SourceGeometryKind;
    using cadcam::planning::ProcessAssignment;
    using cadcam::planning::ProcessGroup;
    using cadcam::planning::ProcessPlan;
    using cadcam::planning::ProcessUnit;

    const QString kComponent = QStringLiteral("CamValidation");

    Diagnostic makeDiagnostic
    (
        DiagnosticCode code,
        OperationStatus status,
        DiagnosticSeverity severity,
        const OperationContext& context,
        const QString& stage,
        const QString& userMessage,
        const QString& technicalDetail = QString(),
        EntityId entityId = 0U,
        const QVariantMap& diagnosticContext = QVariantMap()
    )
    {
        Diagnostic diagnostic;
        diagnostic.code = code;
        diagnostic.severity = severity;
        diagnostic.component = kComponent;
        diagnostic.operation = context.operationName;
        diagnostic.stage = stage;
        diagnostic.userMessage = userMessage;
        diagnostic.technicalDetail = technicalDetail;
        diagnostic.correlationId = context.correlationId;
        diagnostic.entityId = entityId;
        diagnostic.context = diagnosticContext;
        diagnostic.context.insert
        (
            QStringLiteral("validationStatus"),
            static_cast<int>(status)
        );
        return diagnostic;
    }

    void reportConflict
    (
        OperationReport& report,
        const OperationContext& context,
        const QString& stage,
        const QString& message,
        DiagnosticCode code,
        EntityId entityId = 0U
    )
    {
        report.status = OperationStatus::Conflict;
        report.addDiagnostic(makeDiagnostic
        (
            code,
            OperationStatus::Conflict,
            DiagnosticSeverity::Error,
            context,
            stage,
            message,
            QString(),
            entityId
        ));
    }

    void reportInvalid
    (
        OperationReport& report,
        const OperationContext& context,
        const QString& stage,
        const QString& message,
        DiagnosticCode code,
        EntityId entityId = 0U,
        const QVariantMap& diagnosticContext = QVariantMap()
    )
    {
        report.status = OperationStatus::InvalidInput;
        report.addDiagnostic(makeDiagnostic
        (
            code,
            OperationStatus::InvalidInput,
            DiagnosticSeverity::Error,
            context,
            stage,
            message,
            QString(),
            entityId,
            diagnosticContext
        ));
    }

    void reportInvariant
    (
        OperationReport& report,
        const OperationContext& context,
        const QString& stage,
        const QString& message,
        DiagnosticCode code,
        EntityId entityId = 0U
    )
    {
        report.status = OperationStatus::InternalError;
        report.addDiagnostic(makeDiagnostic
        (
            code,
            OperationStatus::InternalError,
            DiagnosticSeverity::Error,
            context,
            stage,
            message,
            QString(),
            entityId
        ));
    }

    // 几何快照中的图元身份集合；0 号或重复编号视为输入非法。
    bool collectSnapshotEntityIds
    (
        const GeometrySnapshot& snapshot,
        std::set<EntityId>& entityIds,
        EntityId& duplicateOrZero,
        const OperationContext& context,
        OperationReport& report
    )
    {
        for (const GeometrySnapshotEntry& entry : snapshot.entries)
        {
            const EntityId entityId = entry.attributes.entityId;
            if (entityId == 0U || !entityIds.insert(entityId).second)
            {
                duplicateOrZero = entityId;
                reportInvalid
                (
                    report,
                    context,
                    QStringLiteral("collect-snapshot-entities"),
                    QStringLiteral("几何快照包含零编号或重复编号图元，无法验证一致性。"),
                    DiagnosticCode::InternalInvariantViolation,
                    entityId
                );
                return false;
            }
        }
        return true;
    }

    bool findSnapshotEntry
    (
        const GeometrySnapshot& snapshot,
        EntityId entityId,
        const GeometrySnapshotEntry** outEntry
    )
    {
        for (const GeometrySnapshotEntry& entry : snapshot.entries)
        {
            if (entry.attributes.entityId == entityId)
            {
                if (outEntry != nullptr) *outEntry = &entry;
                return true;
            }
        }
        return false;
    }

    // 计划中的加工分配索引：entityId → assignment。
    std::map<EntityId, const ProcessAssignment*> indexAssignments(const ProcessPlan& plan)
    {
        std::map<EntityId, const ProcessAssignment*> assignments;
        for (const ProcessAssignment& assignment : plan.assignments)
        {
            assignments.emplace(assignment.entityId, &assignment);
        }
        return assignments;
    }

    std::set<int> collectGroupIds(const ProcessPlan& plan)
    {
        std::set<int> groupIds;
        for (const ProcessGroup& group : plan.groups) groupIds.insert(group.groupId);
        return groupIds;
    }

    bool finitePose(const cadcam::machine::MachinePose4D& pose)
    {
        return std::isfinite(pose.x) && std::isfinite(pose.y)
            && std::isfinite(pose.z) && std::isfinite(pose.aDegrees);
    }
}

namespace cadcam::cam
{
OperationReport CamValidation::validateCamJob
(
    const CamJob& job,
    std::uint64_t documentContentRevision,
    const OperationContext& context
)
{
    OperationReport report;

    if (!job.hasGeometrySnapshot())
    {
        reportInvalid
        (
            report,
            context,
            QStringLiteral("validate-cam-job"),
            QStringLiteral("当前 CAM 任务没有几何快照，无法继续。"),
            DiagnosticCode::MissingProcessOrder
        );
        return report;
    }

    // 复用 CamJob 已有的有效性判定，不复制 revision 规则。
    if (!job.isGeometrySnapshotCurrent(documentContentRevision))
    {
        reportConflict
        (
            report,
            context,
            QStringLiteral("validate-cam-job"),
            QStringLiteral("几何快照与当前文档版本不一致，请重新执行 Planning。"),
            DiagnosticCode::ProcessPlanningRevisionMismatch
        );
        return report;
    }

    if (!job.hasProcessPlan())
    {
        reportInvalid
        (
            report,
            context,
            QStringLiteral("validate-cam-job"),
            QStringLiteral("当前 CAM 任务没有加工计划，请先完成 CAM Planning。"),
            DiagnosticCode::MissingProcessOrder
        );
        return report;
    }

    if (!job.isProcessPlanCurrent(documentContentRevision))
    {
        reportConflict
        (
            report,
            context,
            QStringLiteral("validate-cam-job"),
            QStringLiteral("加工计划与当前文档或加工状态版本不一致，请重新执行 Planning。"),
            DiagnosticCode::ProcessStateRevisionMismatch
        );
        return report;
    }

    // 计划与快照的跨对象一致性由 validateProcessPlan 负责。
    report.status = OperationStatus::Success;
    return report;
}

OperationReport CamValidation::validateProcessPlan
(
    const GeometrySnapshot& snapshot,
    const cadcam::process::DocumentProcessState& processState,
    const cadcam::planning::ProcessPlan& plan,
    const OperationContext& context
)
{
    OperationReport report;

    if (plan.contentRevision != snapshot.contentRevision)
    {
        reportConflict
        (
            report,
            context,
            QStringLiteral("validate-plan-revision"),
            QStringLiteral("加工计划与几何快照版本不一致，请重新执行 Planning。"),
            DiagnosticCode::ProcessPlanningRevisionMismatch
        );
        return report;
    }

    if (plan.processStateRevision != processState.revision())
    {
        reportConflict
        (
            report,
            context,
            QStringLiteral("validate-plan-state-revision"),
            QStringLiteral("加工计划与当前加工状态版本不一致，请重新执行 Planning。"),
            DiagnosticCode::ProcessStateRevisionMismatch
        );
        return report;
    }

    std::set<EntityId> snapshotEntityIds;
    EntityId invalidEntityId = 0U;
    if (!collectSnapshotEntityIds(snapshot, snapshotEntityIds, invalidEntityId, context, report))
    {
        return report;
    }

    // 计划自身结构：加工单元、分配、片段与前置约束，复用 Core 的共享纯验证函数。
    if (!cadcam::planning::validateProcessUnitStructure(plan))
    {
        reportInvariant
        (
            report,
            context,
            QStringLiteral("validate-plan-structure"),
            QStringLiteral("加工计划的加工单元、分配或片段结构不完整。"),
            DiagnosticCode::ProcessPlanningInvariantViolation
        );
        return report;
    }

    if (!cadcam::planning::satisfiesPrecedenceConstraints(plan))
    {
        reportInvariant
        (
            report,
            context,
            QStringLiteral("validate-plan-precedence"),
            QStringLiteral("加工计划的前置约束与加工顺序不一致。"),
            DiagnosticCode::ProcessPlanningInvariantViolation
        );
        return report;
    }

    // 计划引用的图元必须仍然存在于几何快照中。
    const auto requireEntity = [&](EntityId entityId, const QString& stage) -> bool
    {
        if (snapshotEntityIds.find(entityId) != snapshotEntityIds.end())
        {
            return true;
        }

        reportConflict
        (
            report,
            context,
            stage,
            QStringLiteral("加工计划引用的图元已不在当前几何快照中，请重新执行 Planning。"),
            DiagnosticCode::ProcessPlanEntityMissing,
            entityId
        );
        return false;
    };

    for (const ProcessAssignment& assignment : plan.assignments)
    {
        if (!requireEntity(assignment.entityId, QStringLiteral("validate-plan-assignment"))) return report;
    }
    for (const ProcessUnit& unit : plan.processUnits)
    {
        for (const EntityId entityId : unit.orderedMemberEntityIds)
        {
            if (!requireEntity(entityId, QStringLiteral("validate-plan-unit"))) return report;
        }
    }
    for (const ProcessGroup& group : plan.groups)
    {
        for (const EntityId entityId : group.entityIds)
        {
            if (!requireEntity(entityId, QStringLiteral("validate-plan-group"))) return report;
        }
    }
    for (const cadcam::planning::ProcessPathFragment& fragment : plan.plannedFragments)
    {
        if (!requireEntity(fragment.entityId, QStringLiteral("validate-plan-fragment"))) return report;
    }
    for (const cadcam::planning::ProcessExclusion& exclusion : plan.exclusions)
    {
        if (!requireEntity(exclusion.entityId, QStringLiteral("validate-plan-exclusion"))) return report;
    }

    // 排除项不得与加工分配重叠：同一图元不能既参与加工又被排除。
    for (const cadcam::planning::ProcessExclusion& exclusion : plan.exclusions)
    {
        for (const ProcessAssignment& assignment : plan.assignments)
        {
            if (assignment.entityId != exclusion.entityId) continue;

            reportInvariant
            (
                report,
                context,
                QStringLiteral("validate-plan-exclusion-overlap"),
                QStringLiteral("加工计划中同一图元同时出现在加工分配和排除项中。"),
                DiagnosticCode::ProcessPlanningInvariantViolation
            );
            return report;
        }
    }

    // 断面端面归属：编号必须唯一且非负。
    std::set<int> endCutPairIds;
    for (const cadcam::planning::EndCutSideAssignment& side : plan.endCutSides)
    {
        if (side.boundaryPairId < 0 || !endCutPairIds.insert(side.boundaryPairId).second)
        {
            reportInvariant
            (
                report,
                context,
                QStringLiteral("validate-plan-end-cut-sides"),
                QStringLiteral("加工计划的加工断面端面归属存在重复或无效编号。"),
                DiagnosticCode::ProcessPlanningInvariantViolation
            );
            return report;
        }
    }

    report.status = OperationStatus::Success;
    return report;
}

OperationReport CamValidation::validateMachineTrajectory
(
    const GeometrySnapshot& snapshot,
    const cadcam::process::DocumentProcessState& processState,
    const cadcam::planning::ProcessPlan& plan,
    const cadcam::machine::MachineTrajectory& trajectory,
    const OperationContext& context
)
{
    OperationReport report;

    const OperationReport planReport = validateProcessPlan(snapshot, processState, plan, context);
    report.mergeDiagnostics(planReport);
    if (!planReport.succeeded())
    {
        report.status = planReport.status;
        return report;
    }

    if (trajectory.contentRevision != plan.contentRevision)
    {
        reportConflict
        (
            report,
            context,
            QStringLiteral("validate-trajectory-revision"),
            QStringLiteral("机床轨迹与加工计划版本不一致，请重新生成轨迹。"),
            DiagnosticCode::MachineTrajectoryRevisionMismatch
        );
        return report;
    }

    if (trajectory.processStateRevision != plan.processStateRevision)
    {
        reportConflict
        (
            report,
            context,
            QStringLiteral("validate-trajectory-state-revision"),
            QStringLiteral("机床轨迹与加工状态版本不一致，请重新生成轨迹。"),
            DiagnosticCode::MachineTrajectoryRevisionMismatch
        );
        return report;
    }

    const std::map<EntityId, const ProcessAssignment*> assignments = indexAssignments(plan);
    const std::set<int> groupIds = collectGroupIds(plan);
    // 计划片段会把同一图元拆成多条执行路径，因此轨迹实体的次数上限由片段数决定。
    std::map<EntityId, int> allowedOccurrences;
    for (const cadcam::planning::ProcessPathFragment& fragment : plan.plannedFragments)
    {
        ++allowedOccurrences[fragment.entityId];
    }
    std::map<EntityId, int> occurrences;
    int lastProcessUnitIndex = -1;

    for (const cadcam::machine::EntityTrajectory& entity : trajectory.entities)
    {
        if (entity.entityId == 0U
            || assignments.find(entity.entityId) == assignments.end())
        {
            reportConflict
            (
                report,
                context,
                QStringLiteral("validate-trajectory-entity"),
                QStringLiteral("机床轨迹包含计划外的图元，请重新生成轨迹。"),
                DiagnosticCode::MachineTrajectoryEntityMissing,
                entity.entityId
            );
            return report;
        }

        const int occurrenceLimit = std::max(1, allowedOccurrences[entity.entityId]);
        if (++occurrences[entity.entityId] > occurrenceLimit)
        {
            reportConflict
            (
                report,
                context,
                QStringLiteral("validate-trajectory-entity-repeat"),
                QStringLiteral("机床轨迹中同一图元的执行路径数超过计划片段数，请重新生成轨迹。"),
                DiagnosticCode::MachineTrajectoryEntityMissing,
                entity.entityId
            );
            return report;
        }


        const ProcessAssignment* assignment = assignments.at(entity.entityId);
        if (entity.processUnitIndex != assignment->processUnitIndex
            || entity.processUnitIndex < lastProcessUnitIndex)
        {
            reportInvariant
            (
                report,
                context,
                QStringLiteral("validate-trajectory-unit-order"),
                QStringLiteral("机床轨迹的加工单元归属或加工单元顺序与计划不一致。"),
                DiagnosticCode::MachineTrajectoryInvariantViolation
            );
            return report;
        }
        lastProcessUnitIndex = entity.processUnitIndex;

        if (entity.processGroupId >= 0 && groupIds.find(entity.processGroupId) == groupIds.end())
        {
            reportInvariant
            (
                report,
                context,
                QStringLiteral("validate-trajectory-group"),
                QStringLiteral("机床轨迹引用了计划中不存在的加工组。"),
                DiagnosticCode::MachineTrajectoryInvariantViolation
            );
            return report;
        }

        // 来源信息必须与几何快照一致：轨迹只能来自同一份几何事实。
        const GeometrySnapshotEntry* entry = nullptr;
        if (!findSnapshotEntry(snapshot, entity.entityId, &entry)
            || entry->sourceIndex != entity.sourceIndex
            || entry->sourceKind != entity.sourceKind)
        {
            reportConflict
            (
                report,
                context,
                QStringLiteral("validate-trajectory-source"),
                QStringLiteral("机床轨迹的图元来源与几何快照不一致，请重新生成轨迹。"),
                DiagnosticCode::MachineTrajectoryEntityMissing,
                entity.entityId
            );
            return report;
        }

        const auto checkMoves = [&](const std::vector<cadcam::machine::MachineMove>& moves) -> bool
        {
            for (const cadcam::machine::MachineMove& move : moves)
            {
                if (!finitePose(move.target))
                {
                    reportInvariant
                    (
                        report,
                        context,
                        QStringLiteral("validate-trajectory-pose"),
                        QStringLiteral("机床轨迹包含非有限坐标。"),
                        DiagnosticCode::MachineTrajectoryInvariantViolation
                    );
                    return false;
                }
            }
            return true;
        };

        if (!checkMoves(entity.approachMoves)
            || !checkMoves(entity.cuttingMoves)
            || !checkMoves(entity.overcutMoves))
        {
            return report;
        }

        for (const cadcam::geometry::Vector3d& point : entity.sourcePath)
        {
            if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z))
            {
                reportInvariant
                (
                    report,
                    context,
                    QStringLiteral("validate-trajectory-source-path"),
                    QStringLiteral("机床轨迹的源路径包含非有限坐标。"),
                    DiagnosticCode::MachineTrajectoryInvalidPath
                );
                return report;
            }
        }
    }

    for (const auto& entry : assignments)
    {
        if (occurrences[entry.first] > 0) continue;
        reportConflict
        (
            report,
            context,
            QStringLiteral("validate-trajectory-coverage"),
            QStringLiteral("机床轨迹没有覆盖计划中的全部加工图元，请重新生成轨迹。"),
            DiagnosticCode::MachineTrajectoryEntityMissing,
            entry.first
        );
        return report;
    }

    report.status = OperationStatus::Success;
    return report;
}

OperationReport CamValidation::validateNcProgram
(
    const GeometrySnapshot& snapshot,
    const cadcam::process::DocumentProcessState& processState,
    const cadcam::planning::ProcessPlan& plan,
    const std::optional<cadcam::machine::MachineTrajectory>& trajectory,
    const cadcam::nc::NcProgram& program,
    const OperationContext& context
)
{
    OperationReport report;

    const OperationReport planReport = validateProcessPlan(snapshot, processState, plan, context);
    report.mergeDiagnostics(planReport);
    if (!planReport.succeeded())
    {
        report.status = planReport.status;
        return report;
    }

    if (program.contentRevision != plan.contentRevision
        || program.processStateRevision != plan.processStateRevision)
    {
        reportConflict
        (
            report,
            context,
            QStringLiteral("validate-nc-revision"),
            QStringLiteral("NC 程序与加工计划版本不一致，请重新生成 NC。"),
            DiagnosticCode::NcProgramRevisionMismatch
        );
        return report;
    }

    const bool planIsRotary = plan.mode == cadcam::planning::ProcessPlanMode::Rotary4Axis;
    const cadcam::nc::NcProgramMode expectedMode = planIsRotary
        ? cadcam::nc::NcProgramMode::Rotary4Axis
        : cadcam::nc::NcProgramMode::Planar3Axis;
    if (program.mode != expectedMode)
    {
        reportConflict
        (
            report,
            context,
            QStringLiteral("validate-nc-mode"),
            QStringLiteral("NC 程序模式与加工计划模式不一致。"),
            DiagnosticCode::ProcessPlanModeMismatch
        );
        return report;
    }

    if (trajectory.has_value())
    {
        if (trajectory->contentRevision != plan.contentRevision
            || trajectory->processStateRevision != plan.processStateRevision)
        {
            reportConflict
            (
                report,
                context,
                QStringLiteral("validate-nc-trajectory-revision"),
                QStringLiteral("NC 程序使用的机床轨迹与加工计划版本不一致。"),
                DiagnosticCode::MachineTrajectoryRevisionMismatch
            );
            return report;
        }
    }
    else if (planIsRotary)
    {
        reportInvalid
        (
            report,
            context,
            QStringLiteral("validate-nc-trajectory-missing"),
            QStringLiteral("四轴 NC 程序缺少对应的机床轨迹，无法验证一致性。"),
            DiagnosticCode::NcProgramInputInvalid
        );
        return report;
    }

    const std::map<EntityId, const ProcessAssignment*> assignments = indexAssignments(plan);
    // 同一图元可能按计划片段拆成多条轨迹实体，因此按出现顺序保存全部实体。
    std::map<EntityId, std::vector<const cadcam::machine::EntityTrajectory*>> trajectoryEntities;
    if (trajectory.has_value())
    {
        for (const cadcam::machine::EntityTrajectory& entity : trajectory->entities)
        {
            trajectoryEntities[entity.entityId].push_back(&entity);
        }
    }

    // 四轴 NC 的实体块与机床轨迹实体一一对应（片段会拆成多块）；
    // 三轴 NC 每个图元恰好一个实体块。
    std::map<EntityId, int> allowedOccurrences;
    if (trajectory.has_value())
    {
        for (const auto& entry : trajectoryEntities)
        {
            allowedOccurrences[entry.first] = static_cast<int>(entry.second.size());
        }
    }
    else
    {
        for (const auto& entry : assignments) allowedOccurrences[entry.first] = 1;
    }
    std::map<EntityId, int> occurrences;
    std::map<int, int> enableCountByUnit;
    std::map<int, int> disableCountByUnit;

    for (const cadcam::nc::NcEntityBlock& block : program.entities)
    {
        const EntityId entityId = block.metadata.entityId;
        if (entityId == 0U
            || assignments.find(entityId) == assignments.end())
        {
            reportConflict
            (
                report,
                context,
                QStringLiteral("validate-nc-entity"),
                QStringLiteral("NC 程序包含计划外的图元，请重新生成 NC。"),
                DiagnosticCode::NcProgramEntityMissing,
                entityId
            );
            return report;
        }

        const int occurrenceIndex = occurrences[entityId]++;
        if (occurrenceIndex >= std::max(1, allowedOccurrences[entityId]))
        {
            reportConflict
            (
                report,
                context,
                QStringLiteral("validate-nc-entity-repeat"),
                QStringLiteral("NC 程序中同一图元的实体块数量超过预期，请重新生成 NC。"),
                DiagnosticCode::NcProgramDuplicateEntity,
                entityId
            );
            return report;
        }

        // 加工单元归属必须与计划一致。
        const ProcessAssignment* assignment = assignments.at(entityId);
        if (block.processUnitIndex != assignment->processUnitIndex)
        {
            reportInvariant
            (
                report,
                context,
                QStringLiteral("validate-nc-block-mapping"),
                QStringLiteral("NC 实体块的加工单元归属与计划不一致。"),
                DiagnosticCode::NcProgramInvariantViolation,
                entityId
            );
            return report;
        }

        // 加工顺序与加工组的来源随模式不同：
        // - 三轴直接来自计划分配；
        // - 四轴来自生成该 NC 的机床轨迹实体。
        if (trajectory.has_value())
        {
            // 每个实体块对应同一图元的第 occurrenceIndex 条轨迹实体（片段一一对应）。
            const auto trajectoryEntity = trajectoryEntities.find(entityId);
            const bool matched = trajectoryEntity != trajectoryEntities.end()
                && static_cast<std::size_t>(occurrenceIndex) < trajectoryEntity->second.size()
                && block.metadata.processOrder
                    == trajectoryEntity->second[static_cast<std::size_t>(occurrenceIndex)]->processOrder
                && block.metadata.processGroupId
                    == trajectoryEntity->second[static_cast<std::size_t>(occurrenceIndex)]->processGroupId;
            if (!matched)
            {
                reportInvariant
                (
                    report,
                    context,
                    QStringLiteral("validate-nc-trajectory-mapping"),
                    QStringLiteral("NC 实体块的加工顺序或加工组与机床轨迹不一致。"),
                    DiagnosticCode::NcProgramInvariantViolation,
                    entityId
                );
                return report;
            }
        }
        else if (block.metadata.processOrder != assignment->processOrder)
        {
            reportInvariant
            (
                report,
                context,
                QStringLiteral("validate-nc-block-order"),
                QStringLiteral("NC 实体块的加工顺序与计划分配不一致。"),
                DiagnosticCode::NcProgramInvariantViolation,
                entityId
            );
            return report;
        }

        const GeometrySnapshotEntry* entry = nullptr;
        if (!findSnapshotEntry(snapshot, entityId, &entry)
            || entry->sourceIndex != block.metadata.sourceIndex
            || entry->sourceKind != block.metadata.sourceKind)
        {
            reportConflict
            (
                report,
                context,
                QStringLiteral("validate-nc-metadata"),
                QStringLiteral("NC 实体块的图元来源与几何快照不一致，请重新生成 NC。"),
                DiagnosticCode::NcProgramMetadataMissing,
                entityId
            );
            return report;
        }



        // 切割控制：只允许“首块启用、末块停用”的合法组合。
        const bool enableLegit = block.beforeCutting == cadcam::nc::NcCuttingControl::None
            || block.beforeCutting == cadcam::nc::NcCuttingControl::Enable;
        const bool disableLegit = block.afterCutting == cadcam::nc::NcCuttingControl::None
            || block.afterCutting == cadcam::nc::NcCuttingControl::Disable;
        if (!enableLegit || !disableLegit)
        {
            reportInvariant
            (
                report,
                context,
                QStringLiteral("validate-nc-cutting-control"),
                QStringLiteral("NC 程序的加工单元启停代码位置非法。"),
                DiagnosticCode::NcProgramCuttingControlInvalid
            );
            return report;
        }
        if (block.beforeCutting == cadcam::nc::NcCuttingControl::Enable)
        {
            ++enableCountByUnit[block.processUnitIndex];
        }
        if (block.afterCutting == cadcam::nc::NcCuttingControl::Disable)
        {
            ++disableCountByUnit[block.processUnitIndex];
        }
        if (!planIsRotary
            && (block.beforeCutting != cadcam::nc::NcCuttingControl::None
                || block.afterCutting != cadcam::nc::NcCuttingControl::None))
        {
            reportInvariant
            (
                report,
                context,
                QStringLiteral("validate-nc-planar-cutting-control"),
                QStringLiteral("三轴 NC 程序不应包含加工单元启停代码。"),
                DiagnosticCode::NcProgramCuttingControlInvalid
            );
            return report;
        }

        for (const cadcam::nc::NcMotion& motion : block.motions)
        {
            if (motion.entityId != 0U && motion.entityId != entityId)
            {
                reportInvariant
                (
                    report,
                    context,
                    QStringLiteral("validate-nc-motion-entity"),
                    QStringLiteral("NC 运动的图元身份与所属实体块不一致。"),
                    DiagnosticCode::NcProgramInvariantViolation,
                    motion.entityId
                );
                return report;
            }

            const cadcam::nc::NcAxisWords& axes = motion.axes;
            const auto finiteWord = [](const std::optional<double>& value)
            {
                return !value.has_value() || std::isfinite(*value);
            };
            if (!finiteWord(axes.x) || !finiteWord(axes.y) || !finiteWord(axes.z)
                || !finiteWord(axes.a) || !finiteWord(axes.i) || !finiteWord(axes.j)
                || !finiteWord(axes.k) || !finiteWord(axes.r))
            {
                reportInvariant
                (
                    report,
                    context,
                    QStringLiteral("validate-nc-motion-axes"),
                    QStringLiteral("NC 运动包含非有限轴参数。"),
                    DiagnosticCode::NcProgramInvariantViolation,
                    entityId
                );
                return report;
            }
        }
    }

    for (const auto& entry : assignments)
    {
        if (occurrences[entry.first] > 0) continue;
        reportConflict
        (
            report,
            context,
            QStringLiteral("validate-nc-coverage"),
            QStringLiteral("NC 程序没有覆盖计划中的全部加工图元，请重新生成 NC。"),
            DiagnosticCode::NcProgramEntityMissing,
            entry.first
        );
        return report;
    }

    if (planIsRotary)
    {
        std::set<int> unitIndices;
        for (const cadcam::nc::NcEntityBlock& block : program.entities)
        {
            unitIndices.insert(block.processUnitIndex);
        }
        for (const int unitIndex : unitIndices)
        {
            if (enableCountByUnit[unitIndex] != 1 || disableCountByUnit[unitIndex] != 1)
            {
                reportInvariant
                (
                    report,
                    context,
                    QStringLiteral("validate-nc-unit-control"),
                    QStringLiteral("四轴 NC 程序的每个加工单元必须恰好包含一次启用和一次停用。"),
                    DiagnosticCode::NcProgramCuttingControlInvalid
                );
                return report;
            }
        }
    }

    report.status = OperationStatus::Success;
    return report;
}}