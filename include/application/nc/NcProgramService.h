#pragma once

#include "application/geometry/GeometrySnapshot.h"
#include "infrastructure/config/MachiningProcessConfig.h"
#include "application/tasks/TaskContext.h"
#include "core/machining/TubeSection.h"
#include "core/nc/NcProgram.h"
#include "core/planning/ProcessPlan.h"
#include "application/process/DocumentProcessState.h"

class CadDocument;

class NcProgramService
{
public:
    OperationResult<cadcam::nc::NcProgram> buildPlanarProgram
    (
        CadDocument& document,
        const cadcam::process::DocumentProcessState& processState,
        const cadcam::planning::ProcessPlan& processPlan,
        const MachiningProcessConfig& processConfig,
        const OperationContext& context
    ) const;

    // 四轴 NC 程序：几何来自几何快照；CAD 文档只用于兼容层元数据捕获（后续阶段迁移）。
    OperationResult<cadcam::nc::NcProgram> buildRotaryProgram
    (
        const GeometrySnapshot& snapshot,
        CadDocument& document,
        const cadcam::process::DocumentProcessState& processState,
        const cadcam::planning::ProcessPlan& processPlan,
        const std::optional<cadcam::machining::TubeSectionModel>& tubeSection,
        const MachiningProcessConfig& processConfig,
        const OperationContext& context,
        const std::optional<cadcam::geometry::Vector2d>& explicitTubeCenter = std::nullopt
    ) const;
};
