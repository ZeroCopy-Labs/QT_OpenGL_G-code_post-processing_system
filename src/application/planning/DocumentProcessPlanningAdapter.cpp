#include "application/planning/DocumentProcessPlanningAdapter.h"

#include "application/geometry/GeometrySamplingPolicy.h"
#include "application/tasks/TaskContext.h"
#include "application/topology/GeometrySnapshotTopologyAdapter.h"
#include "core/geometry/GeometryCompiler.h"

#include <cmath>
#include <utility>

namespace
{
    using cadcam::geometry::EntityId;
    using cadcam::geometry::SourceGeometryKind;

    Diagnostic captureDiagnostic
    (
        DiagnosticCode code,
        const QString& message,
        const OperationContext& context,
        std::uint64_t contentRevision,
        std::uint64_t processStateRevision,
        EntityId entityId = 0
    )
    {
        Diagnostic diagnostic;
        diagnostic.code = code;
        diagnostic.severity = DiagnosticSeverity::Error;
        diagnostic.component = QStringLiteral("DocumentProcessPlanningAdapter");
        diagnostic.operation = context.operationName;
        diagnostic.stage = QStringLiteral("capture-planning-input");
        diagnostic.userMessage = message;
        diagnostic.correlationId = context.correlationId;
        diagnostic.context =
        {
            { QStringLiteral("contentRevision"), QVariant::fromValue<qulonglong>(contentRevision) },
            { QStringLiteral("processStateRevision"), QVariant::fromValue<qulonglong>(processStateRevision) },
            { QStringLiteral("entityId"), QVariant::fromValue<qulonglong>(entityId) }
        };
        if (entityId != 0U) diagnostic.entityId = entityId;
        return diagnostic;
    }

    std::optional<double> effectiveStartParameter
    (
        const std::optional<double>& requestedStartParameter,
        SourceGeometryKind sourceKind,
        bool closed
    )
    {
        if (requestedStartParameter.has_value()) return requestedStartParameter;
        if (closed && (sourceKind == SourceGeometryKind::Circle
            || sourceKind == SourceGeometryKind::Ellipse))
        {
            return 1.57079632679489661923;
        }
        return std::nullopt;
    }

    // 计划用路径：
    // - 没有手动起点时直接使用快照路径（快照路径本身就是按同类型生产策略编译的结果）；
    // - 有手动起点时按快照条目自带的采样策略从快照内的源几何重新编译，
    //   因为起点只影响闭合几何的采样起点，而快照只描述 CAD 几何。
    // 两种情况都不读取 CadDocument。
    OperationResult<cadcam::geometry::Path3D> resolvePlanningPath
    (
        const GeometrySnapshotEntry& entry,
        const std::optional<double>& startParameter,
        const OperationContext& context
    )
    {
        using namespace cadcam;
        OperationResult<geometry::Path3D> result;

        if (!startParameter.has_value())
        {
            if (entry.path.has_value())
            {
                result.status = OperationStatus::Success;
                result.value = entry.path;
                return result;
            }
            result.status = entry.status == OperationStatus::Success
                ? OperationStatus::Failed : entry.status;
            result.diagnostics = entry.diagnostics;
            return result;
        }

        if (!entry.sourceEntity.has_value())
        {
            result.status = OperationStatus::Failed;
            result.diagnostics = entry.diagnostics;
            return result;
        }

        geometry::PathCompileOptions options;
        options.startParameter = startParameter;
        geometry::GeometryCompiler compiler;
        return compiler.compile(*entry.sourceEntity, entry.samplingPolicy, options, context);
    }
}

OperationResult<cadcam::planning::PlanarProcessPlanningInput>
DocumentProcessPlanningAdapter::capturePlanar
(
    const GeometrySnapshot& snapshot,
    const cadcam::process::DocumentProcessState& processState,
    cadcam::planning::ProcessSortIntent sortIntent,
    const OperationContext& context
) const
{
    using namespace cadcam;
    OperationResult<planning::PlanarProcessPlanningInput> result;
    const std::uint64_t contentRevision = snapshot.contentRevision;
    const std::uint64_t stateRevision = processState.revision();
    if (contentRevision == 0U)
    {
        result.status = OperationStatus::InvalidInput;
        result.addDiagnostic(captureDiagnostic(DiagnosticCode::PlanarPlanningInputInvalid,
            QStringLiteral("三轴加工计划输入需要有效的几何快照。"), context,
            contentRevision, stateRevision));
        return result;
    }

    planning::PlanarProcessPlanningInput input;
    input.contentRevision = contentRevision;
    input.processStateRevision = stateRevision;
    input.entities.reserve(snapshot.entries.size());
    const bool rebuildSequence =
        sortIntent == planning::ProcessSortIntent::RebuildSequence;
    for (const GeometrySnapshotEntry& entry : snapshot.entries)
    {
        planning::PlanarPlanningEntity entity;
        entity.entityId = entry.attributes.entityId;
        entity.sourceIndex = entry.sourceIndex;
        entity.sourceKind = entry.sourceKind;
        entity.visible = entry.attributes.visible;
        const process::EntityProcessState state = processState.stateOrDefault(entity.entityId);
        entity.processEnabled = state.overrideData.processEnabled;
        entity.directionPreference = rebuildSequence
            ? process::DirectionPreference::Auto : state.overrideData.direction;
        entity.startParameter = rebuildSequence
            ? std::nullopt : state.overrideData.startParameter;
        if (entry.sourceEntity.has_value())
        {
            entity.sourceEntity = *entry.sourceEntity;
            auto path = resolvePlanningPath(entry, entity.startParameter, context);
            if (path.succeeded() && path.value.has_value())
            {
                entity.path = std::move(*path.value);
                entity.startParameter = effectiveStartParameter
                    (entity.startParameter, entity.sourceKind, entity.path.closed);
            }
            else result.mergeDiagnostics(path);
        }
        else
        {
            entity.sourceEntity.id = entity.entityId;
            entity.sourceEntity.kind = entity.sourceKind;
        }
        input.entities.push_back(std::move(entity));
    }

    if (processState.revision() != stateRevision)
    {
        result.status = OperationStatus::Conflict;
        result.addDiagnostic(captureDiagnostic(DiagnosticCode::ProcessStateRevisionMismatch,
            QStringLiteral("加工状态在三轴计划输入捕获期间已变更。"), context,
            contentRevision, stateRevision));
        return result;
    }
    result.status = OperationStatus::Success;
    result.value = std::move(input);
    return result;
}

OperationResult<cadcam::planning::ProcessPlanningInput>
DocumentProcessPlanningAdapter::captureRotary
(
    const GeometrySnapshot& snapshot,
    const cadcam::process::DocumentProcessState& processState,
    const std::optional<cadcam::machining::TubeSectionModel>& tubeSection,
    cadcam::planning::ProcessSortIntent sortIntent,
    double connectionTolerance,
    cadcam::topology::PathTopology& topologyStorage,
    const OperationContext& context
) const
{
    using namespace cadcam;
    OperationResult<planning::ProcessPlanningInput> result;
    const std::uint64_t contentRevision = snapshot.contentRevision;
    const std::uint64_t stateRevision = processState.revision();
    if (contentRevision == 0U)
    {
        result.status = OperationStatus::InvalidInput;
        result.addDiagnostic(captureDiagnostic(DiagnosticCode::ProcessPlanningInputInvalid,
            QStringLiteral("四轴加工计划输入需要有效的几何快照。"), context,
            contentRevision, stateRevision));
        return result;
    }

    const topology::PathTopologyTolerance topologyTolerance =
        topology::PathTopologyTolerance::fromConnectionTolerance(connectionTolerance);
    planning::ProcessPlanningInput input;
    input.contentRevision = contentRevision;
    input.processStateRevision = stateRevision;
    input.tubeSection = tubeSection;
    if (tubeSection.has_value())
    {
        input.tubeSectionCenter = geometry::Vector2d
            { tubeSection->geometry.centerY, tubeSection->geometry.centerZ };
    }
    input.topologyInput.contentRevision = contentRevision;
    input.entities.reserve(snapshot.entries.size());
    const bool rebuildSequence =
        sortIntent == planning::ProcessSortIntent::RebuildSequence;
    for (const GeometrySnapshotEntry& entry : snapshot.entries)
    {
        planning::PlanningEntity entity;
        entity.entityId = entry.attributes.entityId;
        entity.sourceIndex = entry.sourceIndex;
        entity.sourceKind = entry.sourceKind;
        entity.visible = entry.attributes.visible;
        const process::EntityProcessState state = processState.stateOrDefault(entity.entityId);
        entity.processEnabled = state.overrideData.processEnabled;
        entity.boundaryRole = state.overrideData.boundaryRole;
        entity.boundaryPairId = state.overrideData.boundaryPairId;
        entity.endCutSide = state.overrideData.endCutSide;
        entity.directionPreference = rebuildSequence
            ? process::DirectionPreference::Auto : state.overrideData.direction;
        entity.startParameter = rebuildSequence
            ? std::nullopt : state.overrideData.startParameter;
        entity.manualDirectionPreference = state.overrideData.direction;
        entity.manualStartParameter = state.overrideData.startParameter;
        // 轨迹执行继续使用按图元类型选择的轨迹采样策略。
        entity.executionSamplingPolicy =
            geometry::trajectoryGeometrySamplingPolicy(entry.attributes.originalDxfType);

        if (entry.sourceEntity.has_value())
        {
            entity.sourceEntity = *entry.sourceEntity;
            auto path = resolvePlanningPath(entry, entity.startParameter, context);
            if (path.succeeded() && path.value.has_value())
            {
                entity.path = std::move(*path.value);
            }
            else result.mergeDiagnostics(path);
        }
        input.entities.push_back(std::move(entity));
    }

    // 拓扑输入由几何快照统一转换，不再从 CAD 图元重新编译几何。
    auto topologyInput = GeometrySnapshotTopologyAdapter().convert
        (snapshot, {}, topologyTolerance, context);
    result.mergeDiagnostics(topologyInput);
    if (!topologyInput.succeeded() || !topologyInput.value.has_value())
    {
        result.status = topologyInput.status;
        return result;
    }
    input.topologyInput = std::move(*topologyInput.value);

    TaskContext taskContext;
    taskContext.operationContext = context;
    topology::PathTopologyBuilder topologyBuilder;
    auto topologyResult = topologyBuilder.build(input.topologyInput, topologyTolerance, taskContext);
    result.mergeDiagnostics(topologyResult);
    if (!topologyResult.succeeded() || !topologyResult.value.has_value())
    {
        result.status = topologyResult.status;
        return result;
    }
    topologyStorage = std::move(*topologyResult.value);
    input.topology = &topologyStorage;

    if (processState.revision() != stateRevision)
    {
        result.status = OperationStatus::Conflict;
        result.addDiagnostic(captureDiagnostic(DiagnosticCode::ProcessStateRevisionMismatch,
            QStringLiteral("加工状态在四轴计划输入捕获期间已变更。"), context,
            contentRevision, stateRevision));
        return result;
    }
    result.status = OperationStatus::Success;
    result.value = std::move(input);
    return result;
}
