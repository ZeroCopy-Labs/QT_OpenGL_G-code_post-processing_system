#pragma once

#include "core/geometry/GeometryTypes.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace cadcam::simulation
{
    // 仿真运动分类：沿用 CAM 已经算出的运动语义，仿真不重新判断运动类型。
    enum class SimulationMoveKind { Rapid, Cutting, CuttingConnection, Overcut };

    // 仿真的机床位姿。X/Y/Z 为机床坐标；A 轴只在 CAM 已给出角度时存在。
    struct SimulationPose
    {
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
        std::optional<double> aDegrees;
    };

    // 一次仿真运动：起点、终点与 CAM 已确定的运动分类。
    struct SimulationMove
    {
        SimulationPose startPose;
        SimulationPose targetPose;
        SimulationMoveKind kind = SimulationMoveKind::Rapid;
        geometry::EntityId entityId = 0;
        int processGroupId = -1;
        int processUnitIndex = -1;
    };

    // 仿真输入的来源模式：四轴来自 MachineTrajectory，三轴来自既有三轴结构化运动。
    enum class SimulationSourceMode { Planar3Axis, Rotary4Axis };

    // 仿真轨迹：一次播放会话的完整输入，只保存已经存在的加工结果。
    struct SimulationTrajectory
    {
        std::uint64_t contentRevision = 0;
        std::uint64_t processStateRevision = 0;
        SimulationSourceMode sourceMode = SimulationSourceMode::Rotary4Axis;
        std::optional<SimulationPose> initialPose;
        std::vector<SimulationMove> moves;
    };

    // 播放会话状态，只描述当前仿真播放进度。
    enum class SimulationPlaybackState { Empty, Ready, Playing, Paused, Finished };

    // 每帧展示状态：渲染层唯一输入，只包含已经整理好的展示数据。
    struct SimulationSnapshot
    {
        SimulationPlaybackState state = SimulationPlaybackState::Empty;
        bool hasTrajectory = false;
        SimulationSourceMode sourceMode = SimulationSourceMode::Rotary4Axis;
        std::size_t moveIndex = 0;
        std::size_t moveCount = 0;
        double moveProgress = 0.0;
        double overallProgress = 0.0;
        SimulationPose currentPose;
        SimulationMoveKind currentKind = SimulationMoveKind::Rapid;
        geometry::EntityId currentEntityId = 0;
        int currentProcessUnitIndex = -1;
    };

    // 运动分类的稳定显示名，用于状态文字。
    const char* simulationMoveKindName(SimulationMoveKind kind);
}
