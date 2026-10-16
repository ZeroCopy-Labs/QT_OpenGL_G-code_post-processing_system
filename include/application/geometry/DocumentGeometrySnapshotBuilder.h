#pragma once

#include "application/geometry/GeometrySnapshot.h"
#include "application/geometry/GeometrySnapshotCompiler.h"
#include "application/tasks/TaskContext.h"

class CadDocument;

class DocumentGeometrySnapshotBuilder
{
public:
    OperationResult<GeometrySourceSnapshot> capture
    (
        const CadDocument& document,
        const OperationContext& context
    ) const;
};

// 捕获并编译当前文档的几何快照，即 CadDocument → GeometrySourceSnapshot → GeometrySnapshot 的完整链路。
// 应用层编排（桌面、测试等）使用该入口，避免各处重复拼接 builder 与 compiler。
OperationResult<GeometrySnapshot> captureAndCompileDocumentGeometrySnapshot
(
    const CadDocument& document,
    GeometryExecutionMode mode,
    const TaskContext& taskContext
);
