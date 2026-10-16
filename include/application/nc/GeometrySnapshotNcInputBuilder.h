#pragma once

#include "application/geometry/GeometrySnapshot.h"
#include "core/nc/NcProgram.h"
#include "core/nc/PlanarNcProgramBuilder.h"
#include "core/planning/ProcessPlan.h"

#include <vector>

// 由几何快照与加工计划生成 NC 输入。
//
// 三轴 NC 与四轴 NC 元数据都只需要快照中的值对象：图元身份、源几何、路径、类型、图层和原始颜色事实。
// 因此转换层不接收 CadDocument，也不访问 CadItem / DRW_Entity，属于生产 NC 路径。
class GeometrySnapshotNcInputBuilder
{
public:
    // 单个图元的 NC 元数据：类型键、图层键和颜色键均由快照属性重建。
    static OperationResult<cadcam::nc::NcEntityMetadata> buildEntityMetadata
    (
        const GeometrySnapshotEntry& entry,
        int processOrder,
        int processGroupId,
        const OperationContext& context
    );

    // 三轴 NC 输入：按计划加工顺序生成，并校验计划与快照的覆盖关系。
    static OperationResult<std::vector<cadcam::nc::PlanarNcEntityInput>> capturePlanar
    (
        const GeometrySnapshot& snapshot,
        const cadcam::planning::ProcessPlan& plan,
        const OperationContext& context
    );

    // 四轴 NC 元数据：按计划加工顺序生成。
    static OperationResult<std::vector<cadcam::nc::NcEntityMetadata>> captureRotary
    (
        const GeometrySnapshot& snapshot,
        const cadcam::planning::ProcessPlan& plan,
        const OperationContext& context
    );
};
