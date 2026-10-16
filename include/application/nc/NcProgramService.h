#pragma once

#include "application/geometry/GeometrySnapshot.h"
#include "infrastructure/config/MachiningProcessConfig.h"
#include "application/tasks/TaskContext.h"
#include "core/machining/TubeSection.h"
#include "core/nc/NcProgram.h"
#include "core/planning/ProcessPlan.h"
#include "application/process/DocumentProcessState.h"

// 由加工计划生成 NC 程序。
// 几何只来自几何快照：服务不接收 CadDocument，也不访问 CAD 图元；
// 只校验快照、加工计划与加工状态之间的 revision 一致性，不自动重新规划。
class NcProgramService
{
public:
    OperationResult<cadcam::nc::NcProgram> buildPlanarProgram
    (
        const GeometrySnapshot& snapshot,
        const cadcam::process::DocumentProcessState& processState,
        const cadcam::planning::ProcessPlan& processPlan,
        const MachiningProcessConfig& processConfig,
        const OperationContext& context
    ) const;

    OperationResult<cadcam::nc::NcProgram> buildRotaryProgram
    (
        const GeometrySnapshot& snapshot,
        const cadcam::process::DocumentProcessState& processState,
        const cadcam::planning::ProcessPlan& processPlan,
        const std::optional<cadcam::machining::TubeSectionModel>& tubeSection,
        const MachiningProcessConfig& processConfig,
        const OperationContext& context,
        const std::optional<cadcam::geometry::Vector2d>& explicitTubeCenter = std::nullopt
    ) const;
};
