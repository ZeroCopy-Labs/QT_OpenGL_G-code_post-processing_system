#pragma once

#include "application/geometry/GeometrySnapshot.h"

#include <cstddef>
#include <vector>

// 一个被判为重复的图元，以及同组中保留参与加工的图元。
struct GeometryDuplicateEntry
{
    cadcam::geometry::EntityId entityId = 0;
    cadcam::geometry::EntityId keptEntityId = 0;
};

struct GeometryDuplicateAnalysis
{
    std::vector<GeometryDuplicateEntry> duplicates;
    std::size_t duplicateCount = 0;
};

// 基于几何快照的重复几何分析。
//
// 判定条件与既有生产去重语义一致：可加工路径类型、相同的原始图元类型、相同的显示颜色，
// 以及规范化后的路径完全相同（坐标按容差量化、折叠相邻重复点、闭合环对起点与方向归一化）。
// 分析只读取快照值对象，不访问 CadDocument，也不修改任何 CAD 数据。
class GeometryDuplicateAnalyzer
{
public:
    GeometryDuplicateAnalysis analyze(const GeometrySnapshot& snapshot) const;
};
