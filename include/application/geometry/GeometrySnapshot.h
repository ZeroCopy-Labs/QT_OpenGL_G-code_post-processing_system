#pragma once

#include "core/geometry/GeometryCompiler.h"

#include <QColor>
#include <QString>
#include <QVector>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

struct EntitySnapshotAttributes
{
    cadcam::geometry::EntityId entityId = 0;
    int originalDxfType = 0;
    QString layer;
    QColor color;
    // 原始颜色事实：true color 优先，其次 ByLayer/ByBlock，最后按 ACI 索引色。
    // 用于在不访问 CAD 图元的情况下重建 NcEntityMetadata 的颜色键。
    int trueColor24 = -1;
    int aciColor = 256;
    bool visible = true;
};

struct GeometrySourceEntry
{
    std::size_t sourceIndex = 0;
    EntitySnapshotAttributes attributes;
    cadcam::geometry::SourceGeometryKind sourceKind =
        cadcam::geometry::SourceGeometryKind::Unknown;
    OperationStatus status = OperationStatus::InternalError;
    std::optional<cadcam::geometry::SourceEntity> sourceEntity;
    QVector<Diagnostic> diagnostics;
};

struct GeometrySourceSnapshot
{
    std::uint64_t contentRevision = 0;
    std::vector<GeometrySourceEntry> entries;
};

struct GeometrySnapshotEntry
{
    std::size_t sourceIndex = 0;
    EntitySnapshotAttributes attributes;
    cadcam::geometry::SourceGeometryKind sourceKind =
        cadcam::geometry::SourceGeometryKind::Unknown;
    OperationStatus status = OperationStatus::InternalError;
    // 原始解析几何（值对象），供需要解析参数或按起点重新采样的调用方使用。
    std::optional<cadcam::geometry::SourceEntity> sourceEntity;
    std::optional<cadcam::geometry::Path3D> path;
    // 编译本条目路径时实际使用的采样策略，供按同语义重新编译的调用方复用。
    cadcam::geometry::SamplingPolicy samplingPolicy;
    QVector<Diagnostic> diagnostics;
};

struct GeometrySnapshot
{
    std::uint64_t contentRevision = 0;
    std::vector<GeometrySnapshotEntry> entries;

    bool matchesRevision(std::uint64_t currentRevision) const
    {
        return contentRevision == currentRevision;
    }
};
