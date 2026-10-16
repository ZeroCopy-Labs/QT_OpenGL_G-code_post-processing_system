#include "platform/pch.h"

#include "application/cam/CamJob.h"

namespace cadcam::cam
{
    bool CamJob::isProcessPlanCurrent(std::uint64_t contentRevision) const
    {
        return m_processPlan.has_value()
            && m_processPlan->contentRevision == contentRevision
            && m_processPlan->processStateRevision == m_processState.revision();
    }

    void CamJob::setProcessPlan(planning::ProcessPlan plan)
    {
        m_processPlan = std::move(plan);
    }

    void CamJob::invalidateProcessPlan()
    {
        m_processPlan.reset();
    }
}
