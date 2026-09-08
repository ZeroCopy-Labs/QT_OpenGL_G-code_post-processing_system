#pragma once

#include "core/machining/TubeSection.h"
#include "core/machine/RotaryKinematics.h"

namespace cadcam::machining
{
    struct CutSurfaceExpectationResult
    {
        std::vector<geometry::Path3D> paths;
        geometry::Vector2d xSlopes;
        QString errorMessage;
    };

    class CutSurfaceExpectation
    {
    public:
        // 在原始期望点上计算一次有符号 X 差值；返回供轨迹生成使用的源路径。
        static CutSurfaceExpectationResult correctPaths
        (
            const TubeSectionModel& section,
            const std::vector<geometry::Path3D>& sourcePaths,
            const machine::RotaryMachinePolicy& policy,
            double wallThickness,
            bool positive,
            bool negative
        );

        // 一个加工断面的全部源路径及逐点对应的激光内壁作用点路径；不依赖路径顺序。
        static CutSurfaceExpectationResult build
        (
            const TubeSectionModel& section,
            const std::vector<geometry::Path3D>& sourcePaths,
            const std::vector<geometry::Path3D>& laserWallImpactPaths
        );
    };
}
