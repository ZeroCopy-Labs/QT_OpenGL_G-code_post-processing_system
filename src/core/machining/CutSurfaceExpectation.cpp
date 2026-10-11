#include "core/machining/CutSurfaceExpectation.h"
#include "core/machining/TubeSectionProjector.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace cadcam::machining
{
    CutSurfaceExpectationResult CutSurfaceExpectation::correctPaths
    (
        const TubeSectionModel& section,
        const std::vector<geometry::Path3D>& sourcePaths,
        const machine::RotaryMachinePolicy& policy,
        double wallThickness,
        machining::EndCutSide side,
        bool positive,
        bool negative
    )
    {
        if (side == machining::EndCutSide::Section
            || (!positive && !negative)) return { sourcePaths, {}, {} };
        auto impacts = sourcePaths;
        for (auto& path : impacts)
        {
            const auto solved = machine::RotaryKinematics::transform
                (path, policy, section, std::nullopt, {});
            if (!solved.succeeded() || !solved.value.has_value()
                || solved.value->poses.size() != path.vertices.size())
                return { {}, {}, QStringLiteral("加工断面修正：无法解算刀头姿态。") };
            for (std::size_t i = 0; i < path.vertices.size(); ++i)
            {
                const auto ray = machine::RotaryKinematics::sourceToolRay(solved.value->poses[i], policy);
                if (!ray.has_value())
                    return { {}, {}, QStringLiteral("加工断面修正：刀头方向无效。") };
                const auto inner = TubeSectionProjector::projectToInnerSurface(section, wallThickness,
                    { ray->origin.y + ray->unitDirection.y * wallThickness,
                      ray->origin.z + ray->unitDirection.z * wallThickness });
                if (!inner.has_value())
                    return { {}, {}, QStringLiteral("加工断面修正：方管厚度或内截面无效。") };
                path.vertices[i].position = { ray->origin.x, inner->x, inner->y };
            }
        }
        auto expected = build(section, sourcePaths, impacts);
        if (!expected.errorMessage.isEmpty()) return expected;
        for (std::size_t p = 0; p < sourcePaths.size(); ++p)
        for (std::size_t i = 0; i < sourcePaths[p].vertices.size(); ++i)
        {
            const double delta = expected.paths[p].vertices[i].position.x - impacts[p].vertices[i].position.x;
            auto& point = expected.paths[p].vertices[i].position;
            point = sourcePaths[p].vertices[i].position;
            // 按端面过滤修正：左端面只补正向差值（向 +X），右端面只补负向差值（向 −X）；普通断面不修正。
            const bool allowed = side == machining::EndCutSide::Left
                ? (positive && delta > 0.0)
                : side == machining::EndCutSide::Right
                    ? (negative && delta < 0.0)
                    : false;
            if (allowed) point.x += delta;
        }
        return expected;
    }

    namespace
    {
        using geometry::Vector3d;
        constexpr double kPositionTolerance = 0.01;
        constexpr double kDirectionCosine = 0.9998476951563913; // 1 度

        struct CornerDirection
        {
            double ySlope = 0.0;
            double zSlope = 0.0;
            Vector3d normal;
        };

        bool finite(const Vector3d& point)
        {
            return std::isfinite(point.x) && std::isfinite(point.y)
                && std::isfinite(point.z);
        }

        bool fitCorner(const std::vector<Vector3d>& points, CornerDirection& direction)
        {
            if (points.size() < 3U) return false;
            Vector3d mean;
            const double pointCount = static_cast<double>(points.size());
            for (const auto& point : points)
            {
                mean.x += point.x / pointCount;
                mean.y += point.y / pointCount;
                mean.z += point.z / pointCount;
            }

            double yy = 0.0, yz = 0.0, zz = 0.0, xy = 0.0, xz = 0.0;
            for (const auto& point : points)
            {
                const double x = point.x - mean.x;
                const double y = point.y - mean.y;
                const double z = point.z - mean.z;
                yy += y * y;
                yz += y * z;
                zz += z * z;
                xy += x * y;
                xz += x * z;
            }
            const double determinant = yy * zz - yz * yz;
            if (yy <= 0.0 || zz <= 0.0 || determinant <= yy * zz * 1.0e-6)
                return false;

            // 仅拟合这个圆角的 x = a*y + b*z + c，不使用其余边的点。
            direction.ySlope = (xy * zz - xz * yz) / determinant;
            direction.zSlope = (xz * yy - xy * yz) / determinant;
            const double length = std::hypot(1.0,
                std::hypot(direction.ySlope, direction.zSlope));
            direction.normal = { 1.0 / length,
                -direction.ySlope / length, -direction.zSlope / length };
            if (!finite(direction.normal) || !std::isfinite(length)) return false;
            for (const auto& point : points)
            {
                const double residual = point.x - mean.x
                    - direction.ySlope * (point.y - mean.y)
                    - direction.zSlope * (point.z - mean.z);
                if (std::abs(residual) > kPositionTolerance) return false;
            }
            return true;
        }

        bool sameDirection(const CornerDirection& left, const CornerDirection& right)
        {
            return left.normal.x * right.normal.x + left.normal.y * right.normal.y
                + left.normal.z * right.normal.z >= kDirectionCosine;
        }
    }

    CutSurfaceExpectationResult CutSurfaceExpectation::build
    (
        const TubeSectionModel& section,
        const std::vector<geometry::Path3D>& sourcePaths,
        const std::vector<geometry::Path3D>& laserWallImpactPaths
    )
    {
        CutSurfaceExpectationResult result;
        if (sourcePaths.empty() || sourcePaths.size() != laserWallImpactPaths.size())
        {
            result.errorMessage = QStringLiteral("源路径与激光内壁作用点采样不完整。");
            return result;
        }

        std::array<std::vector<Vector3d>, 4> cornerPoints;
        std::vector<std::vector<std::size_t>> pathZones;
        for (std::size_t pathIndex = 0; pathIndex < sourcePaths.size(); ++pathIndex)
        {
            const auto& path = sourcePaths[pathIndex];
            const auto& endpoints = laserWallImpactPaths[pathIndex];
            if (path.vertices.size() < 2U || path.vertices.size() != endpoints.vertices.size())
            {
                result.errorMessage = QStringLiteral("源路径与激光内壁作用点采样不完整。");
                return result;
            }
            std::vector<std::size_t> zones;
            for (std::size_t index = 0; index < path.vertices.size(); ++index)
            {
                const auto& point = path.vertices[index].position;
                if (!finite(point) || !finite(endpoints.vertices[index].position))
                {
                    result.errorMessage = QStringLiteral("采样点包含无效坐标。");
                    return result;
                }
                const auto projection = TubeSectionProjector::project
                    (section, { point.y, point.z }, kPositionTolerance);
                if (!projection.valid || projection.absoluteDistanceToShell > kPositionTolerance)
                {
                    result.errorMessage = QStringLiteral("采样点偏离方管截面，无法确定所属圆角或边。");
                    return result;
                }
                const auto zone = tubeZoneIndex(projection.zone);
                zones.push_back(zone);
                // 1/2/3 属于右上圆角（含切点），其余圆角每隔四区出现。
                if (zone % 4U != 0U) cornerPoints[zone / 4U].push_back(point);
            }
            pathZones.push_back(std::move(zones));
        }

        const std::array<QString, 4> cornerNames =
        { QStringLiteral("右上"), QStringLiteral("右下"),
          QStringLiteral("左下"), QStringLiteral("左上") };
        const std::array<QString, 4> faceNames =
        { QStringLiteral("上边"), QStringLiteral("右边"),
          QStringLiteral("下边"), QStringLiteral("左边") };
        std::array<CornerDirection, 4> directions;
        std::array<int, 4> cornerGroups{};
        std::vector<std::vector<std::size_t>> groups;
        for (std::size_t corner = 0; corner < 4U; ++corner)
        {
            if (!fitCorner(cornerPoints[corner], directions[corner]))
            {
                result.errorMessage = QStringLiteral("%1圆角采样不足、退化或不共面，无法推断期望方向。")
                    .arg(cornerNames[corner]);
                return result;
            }
            std::size_t group = 0;
            for (; group < groups.size(); ++group)
            {
                if (std::all_of(groups[group].begin(), groups[group].end(),
                    [&](std::size_t member)
                    { return sameDirection(directions[corner], directions[member]); })) break;
            }
            if (group == groups.size()) groups.emplace_back();
            groups[group].push_back(corner);
            cornerGroups[corner] = static_cast<int>(group);
        }
        if (groups.size() > 2U)
        {
            result.errorMessage = QStringLiteral("工程数据错误：四个圆角存在超过两个期望方向。");
            return result;
        }
        for (std::size_t face = 0; face < 4U; ++face)
        {
            const std::size_t previous = (face + 3U) % 4U;
            if (cornerGroups[previous] != cornerGroups[face])
            {
                result.errorMessage = QStringLiteral("工程数据错误：%1两端（%2、%3圆角）的期望方向不一致。")
                    .arg(faceNames[face], cornerNames[previous], cornerNames[face]);
                return result;
            }
        }

        // 合并方向只平均四个圆角各自的结果，不让采样点较多的圆角改变权重。
        std::vector<CornerDirection> merged(groups.size());
        for (std::size_t group = 0; group < groups.size(); ++group)
        {
            const double cornerCount = static_cast<double>(groups[group].size());
            for (const auto corner : groups[group])
            {
                merged[group].ySlope += directions[corner].ySlope / cornerCount;
                merged[group].zSlope += directions[corner].zSlope / cornerCount;
            }
        }
        result.paths = laserWallImpactPaths;
        // 成功通过相邻边一致性检查后，四个圆角共享合并方向。
        result.xSlopes = { merged.front().ySlope, merged.front().zSlope };
        for (std::size_t path = 0; path < sourcePaths.size(); ++path)
        {
            for (std::size_t index = 0; index < sourcePaths[path].vertices.size(); ++index)
            {
                const auto& source = sourcePaths[path].vertices[index].position;
                auto& expected = result.paths[path].vertices[index].position;
                const auto& direction = merged[cornerGroups[pathZones[path][index] / 4U]];
                // 以当前源点为锚，保持曲线形状；YZ 深度与含 Z 修正的激光内壁作用点一致。
                expected.x = source.x + direction.ySlope * (expected.y - source.y)
                    + direction.zSlope * (expected.z - source.z);
            }
        }
        return result;
    }
}
