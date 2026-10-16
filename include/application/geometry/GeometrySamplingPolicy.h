#pragma once

#include "core/geometry/Path3D.h"

// 按原始 DXF 图元类型选择采样策略。
// 几何快照编译、规划输入捕获和加工轨迹执行共用同一来源，避免采样语义分叉；
// 实现内部控制 DXF 类型判断，调用方只需要传入图元记录的 originalDxfType。
namespace cadcam::geometry
{
    // 生产几何策略：规划输入与拓扑使用的路径按此采样。
    SamplingPolicy productionGeometrySamplingPolicy(int originalDxfType);

    // 轨迹执行策略：圆弧使用更小的最小分段数和 5 度最大角度步长。
    SamplingPolicy trajectoryGeometrySamplingPolicy(int originalDxfType);
}
