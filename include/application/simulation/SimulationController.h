#pragma once

#include "application/simulation/SimulationTypes.h"

#include <cstddef>
#include <optional>

// 仿真播放会话：加载仿真轨迹并提供播放、暂停、单步、复位与推进。
//
// 只保存本次仿真的轨迹输入和播放进度，不依赖 CAD、CamJob、ProcessPlan 或任何视图对象，
// 也不修改加工状态、加工计划或 CAD 文档。
class SimulationController
{
public:
    // 归一化播放：一段运动固定占用同一时长，不模拟真实机床时间。
    static constexpr double kSecondsPerMove = 0.25;
    static constexpr double kMinimumSpeed = 0.1;
    static constexpr double kMaximumSpeed = 16.0;

    void load(cadcam::simulation::SimulationTrajectory trajectory);
    void clear();

    bool isLoaded() const { return m_trajectory.has_value(); }
    bool isPlaying() const;
    bool isFinished() const;

    cadcam::simulation::SimulationPlaybackState state() const { return m_state; }

    // 已整理好的仿真输入，供渲染层读取展示数据。
    const std::optional<cadcam::simulation::SimulationTrajectory>& trajectory() const
    {
        return m_trajectory;
    }

    void play();
    void pause();
    void reset();
    // 单步：前进一整段运动并保持暂停。
    void step();
    // 按播放倍率推进；deltaSeconds 为真实经过时间。
    void advance(double deltaSeconds);
    // 进度条拖动：按整体进度定位到对应运动的对应位置。
    void seek(double overallProgress);

    double speed() const { return m_speed; }
    void setSpeed(double speed);

    std::size_t completedMoveCount() const;
    cadcam::simulation::SimulationSnapshot snapshot() const;

private:
    void clampToBounds();

    std::optional<cadcam::simulation::SimulationTrajectory> m_trajectory;
    cadcam::simulation::SimulationPlaybackState m_state
        = cadcam::simulation::SimulationPlaybackState::Empty;
    std::size_t m_moveIndex = 0;
    double m_moveProgress = 0.0;
    double m_speed = 1.0;
};
