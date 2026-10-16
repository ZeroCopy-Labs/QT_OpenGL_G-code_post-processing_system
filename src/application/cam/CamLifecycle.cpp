#include "platform/pch.h"

#include "application/cam/CamLifecycle.h"

namespace cadcam::cam
{
    const char* camDerivedScopeName(CamDerivedScope scope)
    {
        switch (scope)
        {
        case CamDerivedScope::GeometryDerived: return "GeometryDerived";
        case CamDerivedScope::PlanningDerived: return "PlanningDerived";
        case CamDerivedScope::MotionDerived: return "MotionDerived";
        case CamDerivedScope::OutputDerived: return "OutputDerived";
        }
        return "OutputDerived";
    }

    bool camDerivedScopeInvalidatesGeometrySnapshot(CamDerivedScope scope)
    {
        return scope == CamDerivedScope::GeometryDerived;
    }

    bool camDerivedScopeInvalidatesProcessPlan(CamDerivedScope scope)
    {
        return scope == CamDerivedScope::GeometryDerived
            || scope == CamDerivedScope::PlanningDerived;
    }

    bool camDerivedScopeInvalidatesSimulationSession(CamDerivedScope scope)
    {
        // 仿真消费的是加工计划与机床运动结果，因此前三个范围都会使它失效。
        return scope != CamDerivedScope::OutputDerived;
    }

    CamReadiness evaluateCamReadiness
    (
        const CamJob& job,
        std::uint64_t documentContentRevision,
        bool hasSimulationSession
    )
    {
        CamReadiness readiness;
        readiness.documentContentRevision = documentContentRevision;
        readiness.processStateRevision = job.processState().revision();
        readiness.hasGeometrySnapshot = job.hasGeometrySnapshot();
        readiness.geometrySnapshotCurrent =
            readiness.hasGeometrySnapshot
            && job.isGeometrySnapshotCurrent(documentContentRevision);
        readiness.hasProcessPlan = job.hasProcessPlan();
        readiness.processPlanCurrent =
            readiness.hasProcessPlan
            && job.isProcessPlanCurrent(documentContentRevision);
        readiness.hasSimulationSession = hasSimulationSession;
        return readiness;
    }
}
