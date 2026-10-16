#include "platform/pch.h"

#include "application/geometry/GeometryDuplicateAnalyzer.h"

#include "application/geometry/GeometryCanonicalPathKey.h"

#include <algorithm>

namespace
{
    // 与生产规划一致：只有可加工路径类型参与重复判定，点、构造线等不参与。
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
}

GeometryDuplicateAnalysis GeometryDuplicateAnalyzer::analyze(const GeometrySnapshot& snapshot) const
{
    GeometryDuplicateAnalysis analysis;

    std::vector<const GeometrySnapshotEntry*> orderedEntries;
    orderedEntries.reserve(snapshot.entries.size());

    for (const GeometrySnapshotEntry& entry : snapshot.entries)
    {
        orderedEntries.push_back(&entry);
    }

    // 按 sourceIndex 升序确定“保留项”，与文档顺序一致。
    std::stable_sort
    (
        orderedEntries.begin(), orderedEntries.end(),
        [](const GeometrySnapshotEntry* left, const GeometrySnapshotEntry* right)
        {
            return left->sourceIndex < right->sourceIndex;
        }
    );

    std::vector<QString> seenKeys;
    std::vector<cadcam::geometry::EntityId> seenEntityIds;

    for (const GeometrySnapshotEntry* entry : orderedEntries)
    {
        if (entry == nullptr
            || entry->attributes.entityId == 0U
            || !entry->path.has_value()
            || !supportsProcessPath(entry->sourceKind))
        {
            continue;
        }

        std::vector<cadcam::geometry::Vector3d> points;
        points.reserve(entry->path->vertices.size());

        for (const cadcam::geometry::PathVertex3D& vertex : entry->path->vertices)
        {
            points.push_back(vertex.position);
        }

        const QString key = cadcam::geometry::canonicalGeometryPathKey
        (
            points,
            entry->attributes.originalDxfType,
            static_cast<unsigned int>(entry->attributes.color.rgba())
        );

        if (key.isEmpty())
        {
            continue;
        }

        const auto found = std::find(seenKeys.begin(), seenKeys.end(), key);

        if (found != seenKeys.end())
        {
            GeometryDuplicateEntry duplicate;
            duplicate.entityId = entry->attributes.entityId;
            duplicate.keptEntityId = seenEntityIds[static_cast<std::size_t>(found - seenKeys.begin())];
            analysis.duplicates.push_back(duplicate);
            ++analysis.duplicateCount;
            continue;
        }

        seenKeys.push_back(key);
        seenEntityIds.push_back(entry->attributes.entityId);
    }

    return analysis;
}
