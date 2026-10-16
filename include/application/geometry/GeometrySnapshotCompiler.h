#pragma once

#include "application/geometry/GeometrySnapshot.h"
#include "application/tasks/TaskContext.h"

enum class GeometryExecutionMode
{
    Serial,
    Parallel
};

class GeometrySnapshotCompiler
{
public:
    // 把捕获到的原始几何编译为路径快照。
    // 采样策略按条目的原始 DXF 类型选择，因此调用方不再传入统一策略。
    OperationResult<GeometrySnapshot> compile
    (
        const GeometrySourceSnapshot& source,
        GeometryExecutionMode mode,
        const TaskContext& taskContext
    ) const;
};
