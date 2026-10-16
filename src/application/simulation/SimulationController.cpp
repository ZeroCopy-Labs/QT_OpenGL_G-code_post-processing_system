#include "platform/pch.h"

#include "application/simulation/SimulationController.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace
{
    using cadcam::simulation::SimulationPose;

    double lerp(double start, double target, double progress)
    {
        return start + (target - start) * progress;
    }

    SimulationPose interpolatePose
    (
        const SimulationPose& start,
        const SimulationPose& target,
        double progress
    )
    {
        SimulationPose pose;
        pose.x = lerp(start.x, target.x, progress);
        pose.y = lerp(start.y, target.y, progress);
        pose.z = lerp(start.z, target.z, progress);

        if (start.aDegrees.has_value() && target.aDegrees.has_value())
        {
            // A 轴沿用 CAM 已经算出的目标角，只做数值插值，不重新计算角度规则。
            pose.aDegrees = lerp(*start.aDegrees, *target.aDegrees, progress);
        }
        else if (target.aDegrees.has_value())
        {
            pose.aDegrees = target.aDegrees;
        }

        return pose;
    }

    double clamp01(double value)
    {
        return std::clamp(value, 0.0, 1.0);
    }
}

const char* cadcam::simulation::simulationMoveKindName(SimulationMoveKind kind)
{
    switch (kind)
    {
    case SimulationMoveKind::Rapid: return "Rapid";
    case SimulationMoveKind::Cutting: return "Cutting";
    case SimulationMoveKind::CuttingConnection: return "CuttingConnection";
    case SimulationMoveKind::Overcut: return "Overcut";
    }
    return "Rapid";
}

void SimulationController::load(cadcam::simulation::SimulationTrajectory trajectory)
{
    m_trajectory = std::move(trajectory);
    m_moveIndex = 0;
    m_moveProgress = 0.0;
    m_state = m_trajectory.has_value() && !m_trajectory->moves.empty()
        ? cadcam::simulation::SimulationPlaybackState::Ready
        : cadcam::simulation::SimulationPlaybackState::Empty;
}

void SimulationController::clear()
{
    m_trajectory.reset();
    m_moveIndex = 0;
    m_moveProgress = 0.0;
    m_state = cadcam::simulation::SimulationPlaybackState::Empty;
}

bool SimulationController::isPlaying() const
{
    return m_state == cadcam::simulation::SimulationPlaybackState::Playing;
}

bool SimulationController::isFinished() const
{
    return m_state == cadcam::simulation::SimulationPlaybackState::Finished;
}

void SimulationController::play()
{
    if (!isLoaded() || isFinished())
    {
        return;
    }

    m_state = cadcam::simulation::SimulationPlaybackState::Playing;
}

void SimulationController::pause()
{
    if (isPlaying())
    {
        m_state = cadcam::simulation::SimulationPlaybackState::Paused;
    }
}

void SimulationController::reset()
{
    if (!isLoaded())
    {
        return;
    }

    m_moveIndex = 0;
    m_moveProgress = 0.0;
    m_state = cadcam::simulation::SimulationPlaybackState::Ready;
}

void SimulationController::step()
{
    if (!isLoaded() || m_trajectory->moves.empty())
    {
        return;
    }

    if (m_moveIndex + 1U < m_trajectory->moves.size())
    {
        ++m_moveIndex;
        m_moveProgress = 0.0;
        m_state = cadcam::simulation::SimulationPlaybackState::Paused;
        return;
    }

    m_moveIndex = m_trajectory->moves.size() - 1U;
    m_moveProgress = 1.0;
    m_state = cadcam::simulation::SimulationPlaybackState::Finished;
}

void SimulationController::advance(double deltaSeconds)
{
    if (!isPlaying() || !isLoaded() || m_trajectory->moves.empty())
    {
        return;
    }

    if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0)
    {
        return;
    }

    m_moveProgress += deltaSeconds * m_speed / kSecondsPerMove;

    while (m_moveProgress >= 1.0)
    {
        if (m_moveIndex + 1U >= m_trajectory->moves.size())
        {
            m_moveProgress = 1.0;
            m_state = cadcam::simulation::SimulationPlaybackState::Finished;
            return;
        }

        m_moveProgress -= 1.0;
        ++m_moveIndex;
    }
}

void SimulationController::seek(double overallProgress)
{
    if (!isLoaded() || m_trajectory->moves.empty())
    {
        return;
    }

    const double progress = clamp01(overallProgress);
    const double scaled = progress * static_cast<double>(m_trajectory->moves.size());

    if (progress >= 1.0)
    {
        m_moveIndex = m_trajectory->moves.size() - 1U;
        m_moveProgress = 1.0;
    }
    else
    {
        m_moveIndex = static_cast<std::size_t>(scaled);
        m_moveProgress = scaled - static_cast<double>(m_moveIndex);
    }

    clampToBounds();

    if (m_state != cadcam::simulation::SimulationPlaybackState::Playing)
    {
        m_state = progress >= 1.0
            ? cadcam::simulation::SimulationPlaybackState::Finished
            : cadcam::simulation::SimulationPlaybackState::Paused;
    }
}

void SimulationController::setSpeed(double speed)
{
    if (!std::isfinite(speed))
    {
        return;
    }

    m_speed = std::clamp(speed, kMinimumSpeed, kMaximumSpeed);
}

void SimulationController::clampToBounds()
{
    if (!isLoaded() || m_trajectory->moves.empty())
    {
        m_moveIndex = 0;
        m_moveProgress = 0.0;
        return;
    }

    if (m_moveIndex >= m_trajectory->moves.size())
    {
        m_moveIndex = m_trajectory->moves.size() - 1U;
        m_moveProgress = 1.0;
    }

    m_moveProgress = clamp01(m_moveProgress);
}

std::size_t SimulationController::completedMoveCount() const
{
    if (!isLoaded() || m_trajectory->moves.empty())
    {
        return 0;
    }

    if (isFinished())
    {
        return m_trajectory->moves.size();
    }

    return m_moveIndex;
}

cadcam::simulation::SimulationSnapshot SimulationController::snapshot() const
{
    using cadcam::simulation::SimulationPlaybackState;
    using cadcam::simulation::SimulationSnapshot;

    SimulationSnapshot snapshot;
    snapshot.state = m_state;

    if (!isLoaded() || m_trajectory->moves.empty())
    {
        return snapshot;
    }

    snapshot.hasTrajectory = true;
    snapshot.sourceMode = m_trajectory->sourceMode;
    snapshot.moveCount = m_trajectory->moves.size();
    snapshot.moveIndex = m_moveIndex;
    snapshot.moveProgress = m_moveProgress;
    snapshot.overallProgress =
        (static_cast<double>(m_moveIndex) + m_moveProgress)
        / static_cast<double>(m_trajectory->moves.size());

    const cadcam::simulation::SimulationMove& move = m_trajectory->moves[m_moveIndex];
    snapshot.currentKind = move.kind;
    snapshot.currentEntityId = move.entityId;
    snapshot.currentProcessUnitIndex = move.processUnitIndex;
    snapshot.currentPose = interpolatePose(move.startPose, move.targetPose, m_moveProgress);
    return snapshot;
}


