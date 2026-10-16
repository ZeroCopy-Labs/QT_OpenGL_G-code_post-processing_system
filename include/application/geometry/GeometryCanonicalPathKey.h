#pragma once

#include "core/geometry/Path3D.h"

#include <QString>

#include <vector>

namespace cadcam::geometry
{
    // 由点序列、图元类型和显示颜色生成规范化几何键：
    // 坐标按容差量化、折叠相邻重复点、识别闭合环，并对闭合环的起点与方向做归一化。
    // 几何、类型和颜色相同的图元得到相同键；点序列不足以形成路径时返回空字符串。
    QString canonicalGeometryPathKey
    (
        const std::vector<Vector3d>& points,
        int typeKey,
        unsigned int colorKey,
        double coordinateTolerance = 1.0e-4
    );
}
