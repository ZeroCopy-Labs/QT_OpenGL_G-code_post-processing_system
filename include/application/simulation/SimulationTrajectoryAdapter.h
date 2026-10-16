#pragma once

#include "application/simulation/SimulationTypes.h"
#include "core/machine/MachineTrajectory.h"
#include "core/nc/NcProgram.h"

// 把既有的 CAM 运动结果整理成仿真输入。
//
// 四轴使用正式的 MachineTrajectory；三轴使用既有三轴结构化 NC 运动作为桥接输入。
// 两条路径都不解析 G-code 文本，也不重新计算刀路。
class SimulationTrajectoryAdapter
{
public:
    // 四轴：MachineTrajectory → SimulationTrajectory。
    static cadcam::simulation::SimulationTrajectory fromMachineTrajectory
    (
        const cadcam::machine::MachineTrajectory& trajectory
    );

    // 三轴：既有结构化 NC 运动 → SimulationTrajectory。
    // 圆弧等运动按起点到终点的直线段展示，不重新推导 NC 几何。
    static cadcam::simulation::SimulationTrajectory fromNcProgram
    (
        const cadcam::nc::NcProgram& program
    );
};
