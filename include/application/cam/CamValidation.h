#pragma once

#include "application/cam/CamJob.h"
#include "application/geometry/GeometrySnapshot.h"
#include "application/process/DocumentProcessState.h"
#include "core/diagnostics/OperationContext.h"
#include "core/diagnostics/OperationResult.h"
#include "core/machine/MachineTrajectory.h"
#include "core/nc/NcProgram.h"
#include "core/planning/ProcessPlan.h"

#include <cstdint>
#include <optional>

namespace cadcam::cam
{
    // CAM 数据一致性验证。
    //
    // 只读检查已经存在的值对象之间是否自洽、完整、可继续使用：
    // - 不生成、不修复、不缓存任何结果；
    // - 不修改 CamJob / ProcessPlan / GeometrySnapshot / DocumentProcessState / CAD 文档；
    // - 失败只返回 OperationReport 与 Diagnostic，由调用方决定下一步。
    //
    // 与 CamLifecycle（什么时候失效）和 CamReadiness（是否具备前置条件）职责不同。
    class CamValidation
    {
    public:
        // 验证 CamJob 当前数据与文档内容 revision 是否自洽。
        // 复用 CamJob 已有的有效性判定，不复制 revision 规则。
        static OperationReport validateCamJob
        (
            const CamJob& job,
            std::uint64_t documentContentRevision,
            const OperationContext& context
        );

        // 验证加工计划与几何快照、加工状态的跨对象一致性。
        static OperationReport validateProcessPlan
        (
            const GeometrySnapshot& snapshot,
            const cadcam::process::DocumentProcessState& processState,
            const cadcam::planning::ProcessPlan& plan,
            const OperationContext& context
        );

        // 验证四轴机床轨迹是否仍然与输入计划和几何快照一致。
        static OperationReport validateMachineTrajectory
        (
            const GeometrySnapshot& snapshot,
            const cadcam::process::DocumentProcessState& processState,
            const cadcam::planning::ProcessPlan& plan,
            const cadcam::machine::MachineTrajectory& trajectory,
            const OperationContext& context
        );

        // 验证 NC 程序是否仍然与输入计划、几何快照一致。
        // 四轴程序可以提供生成它的机床轨迹，用于进一步的实体映射检查。
        static OperationReport validateNcProgram
        (
            const GeometrySnapshot& snapshot,
            const cadcam::process::DocumentProcessState& processState,
            const cadcam::planning::ProcessPlan& plan,
            const std::optional<cadcam::machine::MachineTrajectory>& trajectory,
            const cadcam::nc::NcProgram& program,
            const OperationContext& context
        );
    };
}
