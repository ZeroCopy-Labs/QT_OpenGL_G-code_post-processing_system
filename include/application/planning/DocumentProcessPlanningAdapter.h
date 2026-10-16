#pragma once

#include "application/geometry/GeometrySnapshot.h"
#include "application/process/DocumentProcessState.h"
#include "core/diagnostics/OperationContext.h"
#include "core/diagnostics/OperationResult.h"
#include "core/planning/PlanarProcessPlanBuilder.h"
#include "core/topology/PathTopology.h"

// 把 CAD 几何快照与加工状态捕获为规划输入。
// 几何只来自 GeometrySnapshot：适配器不再接收 CadDocument，也不重新从 CAD 图元编译几何。
class DocumentProcessPlanningAdapter
{
public:
    OperationResult<cadcam::planning::PlanarProcessPlanningInput> capturePlanar
    (
        const GeometrySnapshot& snapshot,
        const cadcam::process::DocumentProcessState& processState,
        cadcam::planning::ProcessSortIntent sortIntent,
        const OperationContext& context
    ) const;

    OperationResult<cadcam::planning::ProcessPlanningInput> captureRotary
    (
        const GeometrySnapshot& snapshot,
        const cadcam::process::DocumentProcessState& processState,
        const std::optional<cadcam::machining::TubeSectionModel>& tubeSection,
        cadcam::planning::ProcessSortIntent sortIntent,
        double connectionTolerance,
        cadcam::topology::PathTopology& topologyStorage,
        const OperationContext& context
    ) const;
};
