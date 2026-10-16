#include "application/topology/GeometrySnapshotTopologyAdapter.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace
{
    double distance3D
    (
        const cadcam::geometry::Vector3d& left,
        const cadcam::geometry::Vector3d& right
    )
    {
        const double dx = left.x - right.x;
        const double dy = left.y - right.y;
        const double dz = left.z - right.z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    // 只有可加工路径类型参与拓扑构建，与生产四轴规划输入的筛选保持一致。
    bool supportsProcessPath(cadcam::geometry::SourceGeometryKind kind)
    {
        using cadcam::geometry::SourceGeometryKind;
        return kind == SourceGeometryKind::Line
            || kind == SourceGeometryKind::Arc
            || kind == SourceGeometryKind::Circle
            || kind == SourceGeometryKind::Ellipse
            || kind == SourceGeometryKind::Polyline
            || kind == SourceGeometryKind::Spline;
    }

    // 至少存在一条可用边才建立拓扑记录，阈值与生产实现相同。
    bool hasUsablePath(const cadcam::geometry::Path3D& path, double minimumEdgeLength)
    {
        if (path.vertices.size() < 2U) return false;
        for (std::size_t index = 1; index < path.vertices.size(); ++index)
        {
            const auto& left = path.vertices[index - 1U].position;
            const auto& right = path.vertices[index].position;
            if (distance3D(left, right) > minimumEdgeLength) return true;
        }
        return false;
    }

    Diagnostic adapterDiagnostic
    (
        const OperationContext& context,
        DiagnosticCode code,
        DiagnosticSeverity severity,
        const QString& detail,
        std::size_t recordCount,
        const cadcam::topology::PathTopologyTolerance& tolerance
    )
    {
        Diagnostic diagnostic;
        diagnostic.code = code;
        diagnostic.severity = severity;
        diagnostic.component = QStringLiteral("GeometrySnapshotTopologyAdapter");
        diagnostic.operation = QStringLiteral("AdaptGeometrySnapshotTopology");
        diagnostic.stage = QStringLiteral("CopyPathRecords");
        diagnostic.userMessage = code == DiagnosticCode::TopologyPathUnavailable
            ? QStringLiteral("部分图元没有可用的拓扑路径。")
            : QStringLiteral("几何快照无法转换为拓扑输入。");
        diagnostic.technicalDetail = detail;
        diagnostic.correlationId = context.correlationId;
        diagnostic.context.insert(QStringLiteral("entityId"), static_cast<qulonglong>(0U));
        diagnostic.context.insert(QStringLiteral("sourceIndex"), static_cast<qulonglong>(0U));
        diagnostic.context.insert
            (QStringLiteral("recordCount"), static_cast<qulonglong>(recordCount));
        diagnostic.context.insert(QStringLiteral("nodeCount"), static_cast<qulonglong>(0U));
        diagnostic.context.insert(QStringLiteral("edgeCount"), static_cast<qulonglong>(0U));
        diagnostic.context.insert(QStringLiteral("componentCount"), 0);
        diagnostic.context.insert(QStringLiteral("openNodeCount"), 0);
        diagnostic.context.insert(QStringLiteral("branchNodeCount"), 0);
        diagnostic.context.insert(QStringLiteral("maximumJoinGap"), 0.0);
        diagnostic.context.insert
            (QStringLiteral("numericalJoinEpsilon"), tolerance.numericalJoinEpsilon);
        diagnostic.context.insert(QStringLiteral("nodeSnap"), tolerance.nodeSnap);
        diagnostic.context.insert
            (QStringLiteral("intersectionTolerance"), tolerance.intersection);
        return diagnostic;
    }
}

OperationResult<cadcam::topology::TopologyInput>
GeometrySnapshotTopologyAdapter::convert
(
    const GeometrySnapshot& snapshot,
    const std::vector<cadcam::geometry::EntityId>& subset,
    const cadcam::topology::PathTopologyTolerance& tolerance,
    const OperationContext& context
) const
{
    using cadcam::geometry::EntityId;
    using cadcam::topology::TopologyInput;
    using cadcam::topology::TopologyPathRecord;

    OperationResult<TopologyInput> result;
    if (snapshot.contentRevision == 0U
        || tolerance.minimumEdgeLength < 0.0
        || !std::isfinite(tolerance.minimumEdgeLength))
    {
        result.status = OperationStatus::InvalidInput;
        result.addDiagnostic(adapterDiagnostic
        (
            context,
            DiagnosticCode::TopologyInputInvalid,
            DiagnosticSeverity::Error,
            QStringLiteral("snapshot revision or minimum edge length is invalid"),
            0U,
            tolerance
        ));
        return result;
    }

    const std::set<EntityId> requested(subset.begin(), subset.end());
    std::set<EntityId> found;
    TopologyInput input;
    input.contentRevision = snapshot.contentRevision;
    bool hasFailure = false;

    std::vector<const GeometrySnapshotEntry*> orderedEntries;
    orderedEntries.reserve(snapshot.entries.size());
    for (const GeometrySnapshotEntry& entry : snapshot.entries)
    {
        orderedEntries.push_back(&entry);
    }
    std::stable_sort
    (
        orderedEntries.begin(), orderedEntries.end(),
        [](const GeometrySnapshotEntry* left, const GeometrySnapshotEntry* right)
        {
            return left->sourceIndex < right->sourceIndex;
        }
    );

    for (const GeometrySnapshotEntry* entry : orderedEntries)
    {
        if (!requested.empty() && requested.count(entry->attributes.entityId) == 0U)
        {
            continue;
        }
        found.insert(entry->attributes.entityId);

        if (!entry->path.has_value()
            || (entry->status != OperationStatus::Success
                && entry->status != OperationStatus::PartialSuccess))
        {
            input.diagnostics += entry->diagnostics;
            Diagnostic diagnostic = adapterDiagnostic
            (
                context,
                DiagnosticCode::TopologyPathUnavailable,
                DiagnosticSeverity::Warning,
                QStringLiteral("snapshot entry has no successful Path3D"),
                input.records.size(),
                tolerance
            );
            diagnostic.entityId = entry->attributes.entityId;
            diagnostic.context.insert(QStringLiteral("entityId"),
                static_cast<qulonglong>(entry->attributes.entityId));
            diagnostic.context.insert
                (QStringLiteral("sourceIndex"), static_cast<qulonglong>(entry->sourceIndex));
            input.diagnostics.push_back(diagnostic);
            hasFailure = true;
            continue;
        }

        // 与生产四轴规划输入保持一致：只接收可加工路径类型和存在可用边的条目，
        // 路径点按快照路径原样复制，不额外插入闭合点，闭合语义由 semanticallyClosed 表达。
        if (!supportsProcessPath(entry->sourceKind)
            || !hasUsablePath(*entry->path, tolerance.minimumEdgeLength))
        {
            continue;
        }

        TopologyPathRecord record;
        record.sourceIndex = entry->sourceIndex;
        record.entityId = entry->attributes.entityId;
        record.sourceKind = entry->sourceKind;
        record.semanticallyClosed = entry->path->closed;
        record.points.reserve(entry->path->vertices.size());
        for (const cadcam::geometry::PathVertex3D& vertex : entry->path->vertices)
        {
            record.points.push_back(vertex.position);
        }
        input.records.push_back(std::move(record));
    }

    for (EntityId entityId : requested)
    {
        if (found.count(entityId) != 0U)
        {
            continue;
        }
        Diagnostic diagnostic = adapterDiagnostic
        (
            context,
            DiagnosticCode::TopologyPathUnavailable,
            DiagnosticSeverity::Warning,
            QStringLiteral("requested EntityId is absent from snapshot"),
            input.records.size(),
            tolerance
        );
        diagnostic.entityId = entityId;
        diagnostic.context.insert
            (QStringLiteral("entityId"), static_cast<qulonglong>(entityId));
        input.diagnostics.push_back(diagnostic);
        hasFailure = true;
    }

    result.status = hasFailure ? OperationStatus::PartialSuccess : OperationStatus::Success;
    result.diagnostics = input.diagnostics;
    result.value = std::move(input);
    return result;
}
