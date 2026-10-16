#pragma once

#include "application/geometry/GeometrySnapshot.h"
#include "infrastructure/config/MachiningProcessConfig.h"
#include "application/tasks/TaskContext.h"
#include "application/process/DocumentProcessState.h"
#include "core/machine/MachineTrajectory.h"

// 由加工计划生成四轴机床轨迹。
// 几何只来自几何快照：服务不接收 CadDocument，也不自行捕获或编译 CAD 几何。
class MachineTrajectoryService
{
public:
    OperationResult<cadcam::machine::MachineTrajectory> buildRotaryTrajectory
    (
        const GeometrySnapshot& snapshot,
        const cadcam::process::DocumentProcessState& processState,
        const cadcam::planning::ProcessPlan& plan,
        const std::optional<cadcam::machining::TubeSectionModel>& tubeSection,
        const MachiningProcessConfig& processConfig,
        const TaskContext& context,
        const std::optional<cadcam::geometry::Vector2d>& explicitTubeCenter = std::nullopt
    ) const;
};
