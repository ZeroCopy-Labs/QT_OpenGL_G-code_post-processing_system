#pragma once

#include "application/cam/CamJob.h"

#include <cstdint>

namespace cadcam::cam
{
    // CAM 派生结果的失效范围。范围由真实数据依赖决定，不作为独立事实源保存。
    //
    // - GeometryDerived：CAD 几何变化。快照、加工计划以及全部下游结果失效。
    // - PlanningDerived：加工状态变化。加工计划与下游结果失效，几何快照继续有效。
    // - MotionDerived：机床运动配置变化。轨迹、NC 与仿真会话失效，快照与加工计划继续有效。
    // - OutputDerived：G 代码后处理配置变化。只影响输出文本，没有需要失效的内存派生结果。
    enum class CamDerivedScope
    {
        GeometryDerived,
        PlanningDerived,
        MotionDerived,
        OutputDerived
    };

    const char* camDerivedScopeName(CamDerivedScope scope);

    // 该范围是否使几何快照失效。
    bool camDerivedScopeInvalidatesGeometrySnapshot(CamDerivedScope scope);

    // 该范围是否使加工计划失效。
    bool camDerivedScopeInvalidatesProcessPlan(CamDerivedScope scope);

    // 该范围是否使仿真会话失效（轨迹与 NC 都依赖加工计划与机床运动配置）。
    bool camDerivedScopeInvalidatesSimulationSession(CamDerivedScope scope);

    // 当前 CAM 任务的就绪状态投影。
    //
    // 完全由现有数据计算得到，不独立保存：真实结果对象与 revision 才是事实源，
    // 这个结构只用于界面提示与前置条件判断。
    struct CamReadiness
    {
        std::uint64_t documentContentRevision = 0;
        std::uint64_t processStateRevision = 0;
        bool hasGeometrySnapshot = false;
        bool geometrySnapshotCurrent = false;
        bool hasProcessPlan = false;
        bool processPlanCurrent = false;
        bool hasSimulationSession = false;

        // 快照与加工计划都与当前文档、加工状态一致。
        bool planningReady() const
        {
            return geometrySnapshotCurrent && processPlanCurrent;
        }
    };

    // 由 CamJob 当前数据与文档 revision 计算就绪状态。
    CamReadiness evaluateCamReadiness
    (
        const CamJob& job,
        std::uint64_t documentContentRevision,
        bool hasSimulationSession
    );
}
