#include "platform/pch.h"

#include "application/nc/GeometrySnapshotNcInputBuilder.h"

#include "infrastructure/config/GProfile.h"
#include "drw_entities.h"

#include <QColor>

#include <algorithm>
#include <map>
#include <set>

namespace
{
    Diagnostic builderDiagnostic
    (
        DiagnosticCode code,
        const QString& message,
        const OperationContext& context,
        const cadcam::planning::ProcessPlan& plan,
        cadcam::geometry::EntityId entityId = 0
    )
    {
        Diagnostic diagnostic;
        diagnostic.code = code;
        diagnostic.severity = DiagnosticSeverity::Error;
        diagnostic.component = QStringLiteral("GeometrySnapshotNcInputBuilder");
        diagnostic.operation = context.operationName;
        diagnostic.stage = QStringLiteral("build-nc-input");
        diagnostic.userMessage = message;
        diagnostic.correlationId = context.correlationId;
        diagnostic.context =
        {
            { QStringLiteral("contentRevision"), QVariant::fromValue<qulonglong>(plan.contentRevision) },
            { QStringLiteral("planMode"), plan.mode == cadcam::planning::ProcessPlanMode::Planar3Axis
                ? QStringLiteral("Planar3Axis") : QStringLiteral("Rotary4Axis") },
            { QStringLiteral("assignmentCount"), static_cast<qulonglong>(plan.assignments.size()) },
            { QStringLiteral("excludedCount"), static_cast<qulonglong>(plan.exclusions.size()) }
        };
        if (entityId != 0U) diagnostic.entityId = entityId;
        return diagnostic;
    }

    Diagnostic metadataDiagnostic
    (
        DiagnosticCode code,
        const QString& message,
        const OperationContext& context,
        cadcam::geometry::EntityId entityId
    )
    {
        Diagnostic diagnostic;
        diagnostic.code = code;
        diagnostic.severity = DiagnosticSeverity::Error;
        diagnostic.component = QStringLiteral("GeometrySnapshotNcInputBuilder");
        diagnostic.operation = context.operationName;
        diagnostic.stage = QStringLiteral("build-nc-input");
        diagnostic.userMessage = message;
        diagnostic.correlationId = context.correlationId;
        if (entityId != 0U) diagnostic.entityId = entityId;
        return diagnostic;
    }

    bool processable(cadcam::geometry::SourceGeometryKind kind)
    {
        using Kind = cadcam::geometry::SourceGeometryKind;
        return kind == Kind::Line || kind == Kind::Arc || kind == Kind::Circle
            || kind == Kind::Ellipse || kind == Kind::Polyline || kind == Kind::Spline;
    }

    QString entityTypeKey(int originalDxfType)
    {
        switch (static_cast<DRW::ETYPE>(originalDxfType))
        {
        case DRW::ETYPE::LINE: return QStringLiteral("LINE");
        case DRW::ETYPE::ARC: return QStringLiteral("ARC");
        case DRW::ETYPE::CIRCLE: return QStringLiteral("CIRCLE");
        case DRW::ETYPE::ELLIPSE: return QStringLiteral("ELLIPSE");
        case DRW::ETYPE::POLYLINE: return QStringLiteral("POLYLINE");
        case DRW::ETYPE::LWPOLYLINE: return QStringLiteral("LWPOLYLINE");
        case DRW::ETYPE::SPLINE: return QStringLiteral("SPLINE");
        case DRW::ETYPE::POINT: return QStringLiteral("POINT");
        default: return QString();
        }
    }

    // 与既有生产元数据保持一致：true color 优先，其次 ByLayer/ByBlock，最后按 ACI 索引色。
    QString colorKey(const EntitySnapshotAttributes& attributes)
    {
        if (attributes.trueColor24 >= 0)
        {
            return GProfile::colorKeyFromColor(QColor::fromRgb
            (
                (attributes.trueColor24 >> 16) & 0xFF,
                (attributes.trueColor24 >> 8) & 0xFF,
                attributes.trueColor24 & 0xFF
            ));
        }
        if (attributes.aciColor == DRW::ColorByLayer) return QStringLiteral("BYLAYER");
        if (attributes.aciColor == DRW::ColorByBlock) return QStringLiteral("BYBLOCK");
        return GProfile::colorKeyFromAci(attributes.aciColor);
    }
}

OperationResult<cadcam::nc::NcEntityMetadata>
GeometrySnapshotNcInputBuilder::buildEntityMetadata
(
    const GeometrySnapshotEntry& entry,
    int processOrder,
    int processGroupId,
    const OperationContext& context
)
{
    OperationResult<cadcam::nc::NcEntityMetadata> result;
    if (entry.attributes.entityId == 0U)
    {
        result.status = OperationStatus::InvalidInput;
        result.addDiagnostic(metadataDiagnostic
        (
            DiagnosticCode::NcProgramEntityMissing,
            QStringLiteral("几何快照中的图元缺少稳定编号。"),
            context,
            0U
        ));
        return result;
    }

    cadcam::nc::NcEntityMetadata metadata;
    metadata.entityId = entry.attributes.entityId;
    metadata.sourceKind = entry.sourceKind;
    metadata.sourceIndex = entry.sourceIndex;
    metadata.processOrder = processOrder;
    metadata.processGroupId = processGroupId;
    metadata.entityTypeKey = entityTypeKey(entry.attributes.originalDxfType).toStdString();
    metadata.layerKey = GProfile::normalizeLayerKey(entry.attributes.layer).toStdString();
    metadata.colorKey = colorKey(entry.attributes).toStdString();

    if (metadata.sourceKind == cadcam::geometry::SourceGeometryKind::Unknown
        || metadata.entityTypeKey.empty())
    {
        result.status = OperationStatus::NotSupported;
        result.addDiagnostic(metadataDiagnostic
        (
            DiagnosticCode::NcProgramInputInvalid,
            QStringLiteral("图元类型不支持 NC 输出。"),
            context,
            entry.attributes.entityId
        ));
        return result;
    }

    result.status = OperationStatus::Success;
    result.value = std::move(metadata);
    return result;
}

OperationResult<std::vector<cadcam::nc::PlanarNcEntityInput>>
GeometrySnapshotNcInputBuilder::capturePlanar
(
    const GeometrySnapshot& snapshot,
    const cadcam::planning::ProcessPlan& plan,
    const OperationContext& context
)
{
    using cadcam::geometry::EntityId;
    OperationResult<std::vector<cadcam::nc::PlanarNcEntityInput>> result;

    if (plan.mode != cadcam::planning::ProcessPlanMode::Planar3Axis)
    {
        result.status = OperationStatus::Conflict;
        result.addDiagnostic(builderDiagnostic(DiagnosticCode::ProcessPlanModeMismatch,
            QStringLiteral("三轴 NC 只能使用三轴加工计划。"), context, plan));
        return result;
    }
    if (plan.contentRevision == 0U || snapshot.contentRevision != plan.contentRevision)
    {
        result.status = OperationStatus::Conflict;
        result.addDiagnostic(builderDiagnostic(DiagnosticCode::PlanarNcRevisionMismatch,
            QStringLiteral("三轴加工计划与几何快照版本不一致。"), context, plan));
        return result;
    }

    std::map<EntityId, const GeometrySnapshotEntry*> entries;
    for (const GeometrySnapshotEntry& entry : snapshot.entries)
    {
        if (entry.attributes.entityId == 0U
            || !entries.emplace(entry.attributes.entityId, &entry).second)
        {
            result.status = OperationStatus::InvalidInput;
            result.addDiagnostic(builderDiagnostic(DiagnosticCode::PlanarNcInputInvalid,
                QStringLiteral("几何快照包含无效或重复图元编号。"), context, plan,
                entry.attributes.entityId));
            return result;
        }
    }

    std::set<EntityId> referenced;
    for (const auto& exclusion : plan.exclusions)
    {
        if (entries.find(exclusion.entityId) == entries.end()
            || !referenced.insert(exclusion.entityId).second)
        {
            result.status = OperationStatus::Conflict;
            result.addDiagnostic(builderDiagnostic(DiagnosticCode::PlanarNcEntityMissing,
                QStringLiteral("三轴计划排除项引用了缺失或重复图元。"), context, plan,
                exclusion.entityId));
            return result;
        }
    }

    std::vector<cadcam::planning::ProcessAssignment> assignments = plan.assignments;
    std::sort(assignments.begin(), assignments.end(), [](const auto& left, const auto& right)
    {
        return left.processOrder < right.processOrder;
    });

    std::vector<cadcam::nc::PlanarNcEntityInput> inputs;
    inputs.reserve(assignments.size());
    for (std::size_t order = 0; order < assignments.size(); ++order)
    {
        const auto& assignment = assignments[order];
        const auto found = entries.find(assignment.entityId);
        if (assignment.processOrder != static_cast<int>(order)
            || assignment.processUnitIndex < 0
            || assignment.continuousGroupId != -1
            || found == entries.end()
            || !referenced.insert(assignment.entityId).second
            || !found->second->sourceEntity.has_value()
            || !processable(found->second->sourceKind))
        {
            result.status = OperationStatus::Conflict;
            result.addDiagnostic(builderDiagnostic(DiagnosticCode::PlanarNcInputInvalid,
                QStringLiteral("三轴计划的顺序、分组或图元几何无效。"), context, plan,
                assignment.entityId));
            return result;
        }

        auto metadata = buildEntityMetadata
        (
            *found->second,
            assignment.processOrder,
            assignment.continuousGroupId,
            context
        );
        result.mergeDiagnostics(metadata);
        if (!metadata.succeeded() || !metadata.value.has_value())
        {
            result.status = metadata.status;
            return result;
        }

        cadcam::nc::PlanarNcEntityInput input;
        input.sourceEntity = *found->second->sourceEntity;
        input.metadata = std::move(*metadata.value);
        input.processUnitIndex = assignment.processUnitIndex;
        input.reverse = assignment.reverse;
        input.startParameter = assignment.startParameter;
        inputs.push_back(std::move(input));
    }

    if (referenced.size() != entries.size())
    {
        result.status = OperationStatus::Conflict;
        result.addDiagnostic(builderDiagnostic(DiagnosticCode::PlanarNcRevisionMismatch,
            QStringLiteral("三轴计划未完整覆盖当前几何快照。"), context, plan));
        return result;
    }
    if (inputs.empty())
    {
        result.status = OperationStatus::InvalidInput;
        result.addDiagnostic(builderDiagnostic(DiagnosticCode::PlanarNcInputInvalid,
            QStringLiteral("三轴加工计划中没有可生成 NC 的图元。"), context, plan));
        return result;
    }

    result.status = OperationStatus::Success;
    result.value = std::move(inputs);
    return result;
}

OperationResult<std::vector<cadcam::nc::NcEntityMetadata>>
GeometrySnapshotNcInputBuilder::captureRotary
(
    const GeometrySnapshot& snapshot,
    const cadcam::planning::ProcessPlan& plan,
    const OperationContext& context
)
{
    using cadcam::geometry::EntityId;
    OperationResult<std::vector<cadcam::nc::NcEntityMetadata>> result;

    if (plan.mode != cadcam::planning::ProcessPlanMode::Rotary4Axis)
    {
        result.status = OperationStatus::Conflict;
        result.addDiagnostic(builderDiagnostic(DiagnosticCode::ProcessPlanModeMismatch,
            QStringLiteral("四轴 NC 元数据只能使用四轴加工计划。"), context, plan));
        return result;
    }
    if (plan.contentRevision == 0U || snapshot.contentRevision != plan.contentRevision)
    {
        result.status = OperationStatus::Conflict;
        result.addDiagnostic(builderDiagnostic(DiagnosticCode::NcProgramRevisionMismatch,
            QStringLiteral("几何快照与加工计划版本不一致，无法生成 NC 图元元数据。"), context, plan));
        return result;
    }

    std::map<EntityId, const GeometrySnapshotEntry*> entries;
    for (const GeometrySnapshotEntry& entry : snapshot.entries)
    {
        if (entry.attributes.entityId == 0U
            || !entries.emplace(entry.attributes.entityId, &entry).second)
        {
            result.status = OperationStatus::InvalidInput;
            result.addDiagnostic(builderDiagnostic(DiagnosticCode::NcProgramDuplicateEntity,
                QStringLiteral("几何快照包含零编号或重复编号图元。"), context, plan,
                entry.attributes.entityId));
            return result;
        }
    }

    std::vector<cadcam::planning::ProcessAssignment> assignments = plan.assignments;
    std::sort(assignments.begin(), assignments.end(), [](const auto& left, const auto& right)
    {
        return left.processOrder < right.processOrder;
    });

    std::vector<cadcam::nc::NcEntityMetadata> metadataList;
    metadataList.reserve(assignments.size());
    std::map<EntityId, bool> usedIds;
    for (std::size_t index = 0; index < assignments.size(); ++index)
    {
        const auto& assignment = assignments[index];
        const auto found = entries.find(assignment.entityId);
        if (assignment.entityId == 0U
            || !usedIds.emplace(assignment.entityId, true).second
            || assignment.processOrder != static_cast<int>(index))
        {
            result.status = OperationStatus::InvalidInput;
            result.addDiagnostic(builderDiagnostic(DiagnosticCode::NcProgramDuplicateEntity,
                QStringLiteral("加工计划包含零编号、重复图元或不连续加工顺序。"), context, plan,
                assignment.entityId));
            return result;
        }
        if (found == entries.end())
        {
            result.status = OperationStatus::Failed;
            result.addDiagnostic(builderDiagnostic(DiagnosticCode::NcProgramEntityMissing,
                QStringLiteral("加工计划中的图元已不在几何快照中。"), context, plan,
                assignment.entityId));
            return result;
        }

        auto captured = buildEntityMetadata
        (
            *found->second,
            assignment.processOrder,
            assignment.continuousGroupId,
            context
        );
        result.mergeDiagnostics(captured);
        if (!captured.succeeded() || !captured.value.has_value())
        {
            result.status = captured.status;
            return result;
        }
        metadataList.push_back(std::move(*captured.value));
    }

    if (metadataList.empty())
    {
        result.status = OperationStatus::InvalidInput;
        result.addDiagnostic(builderDiagnostic(DiagnosticCode::NcProgramInputInvalid,
            QStringLiteral("加工计划中没有可生成 NC 元数据的图元。"), context, plan));
        return result;
    }

    result.status = OperationStatus::Success;
    result.value = std::move(metadataList);
    return result;
}
