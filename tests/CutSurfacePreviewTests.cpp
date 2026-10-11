#include "core/machining/CutSurfaceExpectation.h"
#include "core/machining/TubeSectionProjector.h"
#include "core/machine/RotaryKinematics.h"
#include "ui/widgets/MachiningSettingsWidget.h"

#include <QApplication>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QLabel>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
    using namespace cadcam;
    constexpr double pi = 3.14159265358979323846;

    void require(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    void near(double actual, double expected, double tolerance = 1.0e-8)
    {
        require(std::abs(actual - expected) <= tolerance, "coordinate mismatch");
    }

    void onInnerSurface(const geometry::Vector3d& point, double radius, double thickness)
    {
        const double innerRadius = std::max(0.0, radius - thickness);
        const double y = std::abs(point.y - 13.0) - (40.0 - thickness - innerRadius);
        const double z = std::abs(point.z + 7.0) - (30.0 - thickness - innerRadius);
        const double signedDistance = std::hypot(std::max(y, 0.0), std::max(z, 0.0))
            + std::min(std::max(y, z), 0.0) - innerRadius;
        if (std::abs(signedDistance) >= 1.0e-8)
            std::cerr << "inner surface: radius=" << radius << " thickness=" << thickness
                << " point=" << point.x << ',' << point.y << ',' << point.z
                << " distance=" << signedDistance << '\n';
        require(std::abs(signedDistance) < 1.0e-8, "preview point is not on inner section");
    }

    struct Fixture
    {
        machining::TubeSectionModel section;
        std::vector<geometry::Path3D> paths;
    };

    Fixture fixture(double radius, double a, double b, bool curvedFace = false)
    {
        Fixture data;
        data.section.geometry.centerY = 13.0;
        data.section.geometry.centerZ = -7.0;
        data.section.geometry.yLength = 80.0;
        data.section.geometry.zWidth = 60.0;
        data.section.cornerRadius = radius;
        data.section.roundedCornerCount = 4;
        const int ys[] = { 1, 1, -1, -1 };
        const int zs[] = { 1, -1, -1, 1 };
        std::array<geometry::Path3D, 4> corners;
        for (int corner = 0; corner < 4; ++corner)
        {
            const geometry::Vector2d center
                { 13.0 + ys[corner] * (40.0 - radius), -7.0 + zs[corner] * (30.0 - radius) };
            data.section.corners.push_back({ center, radius, ys[corner], zs[corner] });
            corners[corner].sourceEntityId = 2U * corner + 2U;
            for (int sample = 0; sample <= 128; ++sample)
            {
                const double angle = pi * 0.5 * (1.0 - corner - sample / 128.0);
                const double y = center.x + radius * std::cos(angle);
                const double z = center.y + radius * std::sin(angle);
                corners[corner].vertices.push_back({ { 100.0 + a * y + b * z, y, z } });
            }
        }
        for (int face = 0; face < 4; ++face)
        {
            const auto start = corners[(face + 3) % 4].vertices.back().position;
            const auto end = corners[face].vertices.front().position;
            geometry::Path3D path;
            path.sourceEntityId = 2U * face + 1U;
            for (int sample = 0; sample <= 32; ++sample)
            {
                const double t = sample / 32.0;
                const double bump = curvedFace && face == 0 ? 8.0 * std::sin(pi * t) : 0.0;
                path.vertices.push_back({ { start.x + (end.x - start.x) * t + bump,
                    start.y + (end.y - start.y) * t, start.z + (end.z - start.z) * t } });
            }
            data.paths.push_back(path);
            data.paths.push_back(corners[face]);
        }
        for (const auto& path : data.paths)
            for (const auto& vertex : path.vertices)
                data.section.geometry.boundary.push_back({ vertex.position.y, vertex.position.z });
        return data;
    }

    std::vector<geometry::Path3D> endpoints(const Fixture& data, double wallThickness,
        const machine::RotaryMachinePolicy& policy)
    {
        auto result = data.paths;
        for (std::size_t path = 0; path < result.size(); ++path)
        {
            const auto solved = machine::RotaryKinematics::transform
                (data.paths[path], policy, data.section, std::nullopt, {});
            require(solved.succeeded() && solved.value.has_value(), "kinematics failed");
            require(solved.value->poses.size() == result[path].vertices.size(), "pose count");
            for (std::size_t index = 0; index < result[path].vertices.size(); ++index)
            {
                const auto ray = machine::RotaryKinematics::sourceToolRay(solved.value->poses[index], policy);
                require(ray.has_value(), "tool ray failed");
                result[path].vertices[index].position =
                { ray->origin.x + wallThickness * ray->unitDirection.x,
                  ray->origin.y + wallThickness * ray->unitDirection.y,
                  ray->origin.z + wallThickness * ray->unitDirection.z };
                const auto& point = result[path].vertices[index].position;
                near(std::hypot(point.x - ray->origin.x,
                    std::hypot(point.y - ray->origin.y, point.z - ray->origin.z)), wallThickness);
                const auto innerPoint = machining::TubeSectionProjector::projectToInnerSurface
                    (data.section, wallThickness, { point.y, point.z });
                require(innerPoint.has_value(), "inner section projection failed");
                result[path].vertices[index].position.y = innerPoint->x;
                result[path].vertices[index].position.z = innerPoint->y;
            }
        }
        return result;
    }

    void geometryTests()
    {
        machine::RotaryMachinePolicy policy;
        policy.rotaryAxisY = 13.0;
        policy.rotaryAxisZ = -7.0;
        policy.surfaceClassificationTolerance = 1.0e-5;
        for (double radius : { 2.0, 4.0, 8.0 })
        for (double wallThickness : { radius * 0.5, radius, radius * 1.5 })
        for (double zOffset : { -1.25, 0.0, 1.25 })
        for (bool curved : { false, true })
        for (bool inverted : { false, true })
        {
            policy.machiningPlaneZOffset = zOffset;
            policy.invertAAxisDirection = inverted;
            policy.aAxisOffsetDegrees = inverted ? 17.0 : 0.0;
            const auto data = fixture(radius, 0.7, -0.4, curved);
            const auto laser = endpoints(data, wallThickness, policy);
            const auto expected = machining::CutSurfaceExpectation::build(data.section, data.paths, laser);
            if (!expected.errorMessage.isEmpty()) std::cerr << expected.errorMessage.toStdString() << '\n';
            require(expected.errorMessage.isEmpty(), "expected surface failed");
            require(expected.paths.size() == data.paths.size(), "path count");
            for (std::size_t path = 0; path < data.paths.size(); ++path)
            for (std::size_t index = 0; index < data.paths[path].vertices.size(); ++index)
            {
                const auto& p = data.paths[path].vertices[index].position;
                const auto& e = laser[path].vertices[index].position;
                const auto& q = expected.paths[path].vertices[index].position;
                onInnerSurface(e, radius, wallThickness);
                onInnerSurface(q, radius, wallThickness);
                near(q.y, e.y);
                near(q.z, e.z);
                near(q.x - p.x, 0.7 * (q.y - p.y) - 0.4 * (q.z - p.z));
                near(e.x, p.x);
            }
            for (std::size_t path = 0; path < laser.size(); ++path)
            {
                const auto green = machining::TubeSectionProjector::connectInnerCorners
                    (data.section, wallThickness, laser[path]);
                const auto purple = machining::TubeSectionProjector::connectInnerCorners
                    (data.section, wallThickness, expected.paths[path], expected.xSlopes);
                require(!green.vertices.empty() && green.vertices.size() == purple.vertices.size(),
                    "inner corner connection failed");
                for (std::size_t index = 0; index < green.vertices.size(); ++index)
                {
                    const auto& e = green.vertices[index].position;
                    const auto& q = purple.vertices[index].position;
                    near(e.y, q.y); near(e.z, q.z);
                    onInnerSurface(e, radius, wallThickness);
                    if (!curved) near(q.x, 100.0 + 0.7 * q.y - 0.4 * q.z);
                    if (wallThickness >= radius && index > 0U)
                    {
                        const auto& previous = green.vertices[index - 1U].position;
                        onInnerSurface({ 0.0, (previous.y + e.y) * 0.5,
                            (previous.z + e.z) * 0.5 }, radius, wallThickness);
                    }
                }
            }
            if (!inverted)
                near(laser[0].vertices[16].position.z,
                    data.paths[0].vertices[16].position.z - wallThickness);
        }

        policy.machiningPlaneZOffset = 0.0;
        policy.invertAAxisDirection = false;
        policy.aAxisOffsetDegrees = 0.0;
        const auto thickData = fixture(4.0, 0.7, -0.4);
        const auto thickLaser = endpoints(thickData, 6.0, policy);
        const std::array<int, 4> ys{ 1, 1, -1, -1 }, zs{ 1, -1, -1, 1 };
        for (std::size_t corner = 0; corner < 4U; ++corner)
        for (const auto& vertex : thickLaser[corner * 2U + 1U].vertices)
        {
            near(vertex.position.y, 13.0 + ys[corner] * 34.0);
            near(vertex.position.z, -7.0 + zs[corner] * 24.0);
        }
        for (double invalid : { 0.0, -1.0, 30.0, 40.0, std::numeric_limits<double>::quiet_NaN() })
            require(!machining::TubeSectionProjector::projectToInnerSurface
                (thickData.section, invalid, { 0.0, 0.0 }).has_value(), "invalid inner section accepted");

        geometry::Path3D sparseLoop;
        sparseLoop.closed = true;
        for (const geometry::Vector2d yz : { geometry::Vector2d{ 13.0, 17.0 },
            { 47.0, -7.0 }, { 13.0, -31.0 }, { -21.0, -7.0 } })
            sparseLoop.vertices.push_back({ { 100.0 + 0.7 * yz.x - 0.4 * yz.y, yz.x, yz.y } });
        const auto connected = machining::TubeSectionProjector::connectInnerCorners
            (thickData.section, 6.0, sparseLoop, { 0.7, -0.4 });
        require(connected.closed && connected.vertices.size() == 8U, "closed loop did not acquire four corners");
        for (std::size_t index = 0; index < connected.vertices.size(); ++index)
        {
            const auto& p = connected.vertices[index].position;
            const auto& next = connected.vertices[(index + 1U) % connected.vertices.size()].position;
            near(p.x, 100.0 + 0.7 * p.y - 0.4 * p.z);
            onInnerSurface({ 0.0, (p.y + next.y) * 0.5, (p.z + next.z) * 0.5 }, 4.0, 6.0);
        }
        auto data = fixture(4.0, 0.7, -0.4, true);
        auto laser = endpoints(data, 3.0, policy);
        const auto baseline = machining::CutSurfaceExpectation::build(data.section, data.paths, laser);
        // 两条线共用壁厚截止位置：壁厚减半，期望点相对源点的位移也减半。
        const auto thinLaser = endpoints(data, 1.5, policy);
        const auto thinExpected = machining::CutSurfaceExpectation::build(data.section, data.paths, thinLaser);
        require(thinExpected.errorMessage.isEmpty(), "shortened expected surface failed");
        for (std::size_t path = 0; path < data.paths.size(); ++path)
        for (std::size_t index = 0; index < data.paths[path].vertices.size(); ++index)
        {
            const auto& p = data.paths[path].vertices[index].position;
            const auto& thick = baseline.paths[path].vertices[index].position;
            const auto& thin = thinExpected.paths[path].vertices[index].position;
            near(thin.x - p.x, (thick.x - p.x) * 0.5);
            near(thin.y - p.y, (thick.y - p.y) * 0.5);
            near(thin.z - p.z, (thick.z - p.z) * 0.5);
        }
        // 相同采样路径反转、图元重排仍得到逐点相同的期望位置。
        for (auto& path : data.paths) std::reverse(path.vertices.begin(), path.vertices.end());
        std::reverse(data.paths.begin(), data.paths.end());
        laser = endpoints(data, 3.0, policy);
        const auto reversed = machining::CutSurfaceExpectation::build(data.section, data.paths, laser);
        require(reversed.errorMessage.isEmpty(), "reversed paths failed");
        for (std::size_t path = 0; path < data.paths.size(); ++path)
        for (std::size_t index = 0; index < data.paths[path].vertices.size(); ++index)
        {
            const auto& q = reversed.paths[path].vertices[index].position;
            const auto& original = baseline.paths[7U - path].vertices;
            const auto& p = original[original.size() - 1U - index].position;
            near(q.x, p.x); near(q.y, p.y); near(q.z, p.z);
        }

        data = fixture(4.0, 0.0, 0.0);
        laser = endpoints(data, 3.0, policy);
        const auto vertical = machining::CutSurfaceExpectation::build(data.section, data.paths, laser);
        require(vertical.errorMessage.isEmpty(), "vertical section failed");
        near(vertical.paths[1].vertices[64].position.x, laser[1].vertices[64].position.x);

        // 一个闭合路径包含全部区域，同样按采样点分区，而非图元类型。
        geometry::Path3D closed;
        closed.closed = true;
        for (const auto& path : data.paths)
            closed.vertices.insert(closed.vertices.end(), path.vertices.begin(), path.vertices.end());
        data.paths = { closed };
        laser = endpoints(data, 3.0, policy);
        const auto whole = machining::CutSurfaceExpectation::build(data.section, data.paths, laser);
        require(whole.errorMessage.isEmpty() && whole.paths[0].closed, "closed path failed");

        // 任意拆分同一离散路径，圆角方向仍来自整个加工断面。
        std::vector<geometry::Path3D> pieces, endpointPieces;
        for (std::size_t begin = 0; begin + 1U < closed.vertices.size(); begin += 40U)
        {
            const auto end = std::min(begin + 41U, closed.vertices.size());
            geometry::Path3D piece, endpointPiece;
            piece.vertices.assign(closed.vertices.begin() + begin, closed.vertices.begin() + end);
            endpointPiece.vertices.assign(laser[0].vertices.begin() + begin, laser[0].vertices.begin() + end);
            pieces.push_back(piece);
            endpointPieces.push_back(endpointPiece);
        }
        const auto split = machining::CutSurfaceExpectation::build(data.section, pieces, endpointPieces);
        require(split.errorMessage.isEmpty(), "split paths failed");
        for (std::size_t part = 0; part < pieces.size(); ++part)
        for (std::size_t index = 0; index < pieces[part].vertices.size(); ++index)
            near(split.paths[part].vertices[index].position.x,
                whole.paths[0].vertices[part * 40U + index].position.x);

        auto checkError = [&](Fixture invalid, const QString& text)
        {
            const auto actual = endpoints(invalid, 3.0, policy);
            const auto failed = machining::CutSurfaceExpectation::build(invalid.section, invalid.paths, actual);
            require(failed.paths.empty() && failed.errorMessage.contains(text), "missing expected diagnostic");
            require(!actual.empty(), "laser preview must survive expected-surface failure");
        };
        data = fixture(4.0, 0.7, -0.4);
        data.paths.erase(data.paths.begin() + 1);
        checkError(data, QStringLiteral("右上圆角"));
        data = fixture(4.0, 0.7, -0.4);
        data.paths[1].vertices[64].position.x += 1.0;
        checkError(data, QStringLiteral("不共面"));
        data = fixture(4.0, 0.7, -0.4);
        // 保留两个切点，修改一个圆角的独立平面方向，构造真正的两组方向冲突。
        for (auto& vertex : data.paths[1].vertices)
            vertex.position.x += 0.5 * (vertex.position.y + vertex.position.z - 72.0);
        // 右上切点的 y+z 均为 72，边端点仍与圆角连续。
        checkError(data, QStringLiteral("两端"));
        for (auto& vertex : data.paths[3].vertices)
            vertex.position.x += 0.3 * (vertex.position.y - vertex.position.z - 86.0);
        checkError(data, QStringLiteral("超过两个"));

        const auto good = fixture(4.0, 0.7, -0.4);
        auto malformed = endpoints(good, 3.0, policy);
        malformed[0].vertices.pop_back();
        const auto mismatch = machining::CutSurfaceExpectation::build(good.section, good.paths, malformed);
        require(mismatch.paths.empty() && !mismatch.errorMessage.isEmpty(), "mismatched samples accepted");
        malformed = endpoints(good, 3.0, policy);
        malformed[0].vertices[0].position.x = std::numeric_limits<double>::quiet_NaN();
        const auto nonFinite = machining::CutSurfaceExpectation::build(good.section, good.paths, malformed);
        require(nonFinite.paths.empty() && nonFinite.errorMessage.contains(QStringLiteral("无效坐标")),
            "non-finite endpoint accepted");
    }

    void correctionTests()
    {
        machine::RotaryMachinePolicy policy;
        policy.machiningPlaneZOffset = 1.25;
        for (double thickness : { 2.0, 4.0, 6.0 })
        {
            const auto data = fixture(4.0, 0.7, -0.4, true);
            const auto laser = endpoints(data, thickness, policy);
            const auto expected = machining::CutSurfaceExpectation::build(data.section, data.paths, laser);
            for (bool positive : { false, true })
            for (bool negative : { false, true })
            for (const auto side : { machining::EndCutSide::Left,
                                     machining::EndCutSide::Right,
                                     machining::EndCutSide::Section })
            {
                const auto corrected = machining::CutSurfaceExpectation::correctPaths
                    (data.section, data.paths, policy, thickness, side, positive, negative);
                require(corrected.errorMessage.isEmpty(), "correction failed");
                Fixture moved = data;
                moved.paths = corrected.paths;
                const auto impact = endpoints(moved, thickness, policy);
                for (std::size_t p = 0; p < data.paths.size(); ++p)
                for (std::size_t i = 0; i < data.paths[p].vertices.size(); ++i)
                {
                    const auto& source = data.paths[p].vertices[i].position;
                    const auto& actual = corrected.paths[p].vertices[i].position;
                    const double delta = expected.paths[p].vertices[i].position.x - laser[p].vertices[i].position.x;
                    // 左端面只补正向差值（向 +X），右端面只补负向差值（向 −X），普通断面不修正。
                    const bool enabled = side == machining::EndCutSide::Left
                        ? (positive && delta > 0.0)
                        : side == machining::EndCutSide::Right
                            ? (negative && delta < 0.0) : false;
                    near(actual.x, source.x + (enabled ? delta : 0.0));
                    near(actual.y, source.y);
                    near(actual.z, source.z);
                    near(impact[p].vertices[i].position.x,
                        enabled ? expected.paths[p].vertices[i].position.x : laser[p].vertices[i].position.x);
                }
            }
            require(!machining::CutSurfaceExpectation::correctPaths
                (data.section, data.paths, policy, 30.0, machining::EndCutSide::Right,
                    true, false).errorMessage.isEmpty(),
                "invalid thickness accepted for correction");
        }
    }

    void widgetTests()
    {
        MachiningSettingsWidget widget;
        auto* positive = widget.findChild<QCheckBox*>(QStringLiteral("cutSurfacePositiveCorrection"));
        auto* negative = widget.findChild<QCheckBox*>(QStringLiteral("cutSurfaceNegativeCorrection"));
        require(positive && negative && !positive->isChecked() && !negative->isChecked(), "correction defaults");
        bool positiveState = false, negativeState = false;
        QObject::connect(&widget, &MachiningSettingsWidget::cutSurfacePositiveCorrectionChanged,
            [&](bool value) { positiveState = value; });
        QObject::connect(&widget, &MachiningSettingsWidget::cutSurfaceNegativeCorrectionChanged,
            [&](bool value) { negativeState = value; });
        positive->setChecked(true);
        require(positiveState && !negativeState, "positive correction wiring");
        negative->setChecked(true);
        require(positiveState && negativeState, "independent correction switches");
        positive->setChecked(false);
        require(!positiveState && negativeState, "negative correction wiring");
        auto* thickness = widget.findChild<QDoubleSpinBox*>(QStringLiteral("tubeWallThickness"));
        require(thickness != nullptr, "tube wall thickness setting missing");
        int thicknessChanges = 0;
        double editedThickness = 0.0;
        QObject::connect(&widget, &MachiningSettingsWidget::rotaryTubeWallThicknessChanged,
            [&](double value) { ++thicknessChanges; editedThickness = value; });
        widget.setRotaryTubeWallThickness(4.0);
        near(thickness->value(), 4.0);
        require(thicknessChanges == 0, "UI refresh emitted a thickness edit");
        thickness->setValue(2.5);
        thickness->editingFinished();
        near(editedThickness, 2.5);
        require(thicknessChanges == 1, "thickness edit did not propagate");
        thickness->setValue(0.0);
        thickness->editingFinished();
        near(editedThickness, 0.0);
        require(thicknessChanges == 2, "default reset request did not propagate");
        auto* laser = widget.findChild<QCheckBox*>(QStringLiteral("laserWallImpactVisible"));
        auto* expected = widget.findChild<QCheckBox*>(QStringLiteral("expectedSurfaceVisible"));
        require(laser && expected && laser->isChecked() && expected->isChecked(), "default preview switches");
        require(laser->text() == QStringLiteral("激光内壁作用线（绿色）"), "old laser line label remains");
        for (const auto* label : widget.findChildren<QLabel*>())
            require(!label->text().contains(QStringLiteral("加工深度")), "old depth input remains");
        int laserChanges = 0, expectedChanges = 0;
        QObject::connect(&widget, &MachiningSettingsWidget::rotaryLaserWallImpactVisibleChanged,
            [&](bool) { ++laserChanges; });
        QObject::connect(&widget, &MachiningSettingsWidget::rotaryExpectedSurfaceVisibleChanged,
            [&](bool) { ++expectedChanges; });
        laser->click();
        require(laserChanges == 1 && expectedChanges == 0 && expected->isChecked(), "laser toggle not independent");
        expected->click();
        require(laserChanges == 1 && expectedChanges == 1 && !laser->isChecked(), "expected toggle not independent");
        widget.setRotaryTubeWallThickness(3.5);
        require(!laser->isChecked() && !expected->isChecked(), "wall thickness change reset switches");
        laser->click(); expected->click();
        require(laserChanges == 2 && expectedChanges == 2, "switches cannot restore");
        widget.setRotaryEndCutPreviewStatus(QStringLiteral("工程数据错误：上边两端方向不一致。"));
        require(widget.findChild<QLabel*>(QStringLiteral("endCutPreviewStatus"))->text().contains(
            QStringLiteral("工程数据错误")), "diagnostic not displayed");
    }

    void endCutSideTests()
    {
        const auto sideOf = [](const std::vector<planning::EndCutSideAssignment>& assignments,
            int pairId)
        {
            for (const planning::EndCutSideAssignment& assignment : assignments)
            {
                if (assignment.boundaryPairId == pairId) return assignment.side;
            }
            throw std::runtime_error("missing boundary assignment");
        };

        // 未指定时按最小 X 升序交替判定：左、右、左、右……
        {
            std::vector<planning::EndCutSideBoundary> boundaries;
            for (int index = 6; index >= 1; --index)
            {
                boundaries.push_back({ index, static_cast<double>(index) * 10.0, std::nullopt });
            }
            const auto assignments = planning::resolveEndCutSides(boundaries);
            require(assignments.size() == 6U, "boundary count mismatch");
            require(sideOf(assignments, 1) == machining::EndCutSide::Left, "leftmost must be left");
            require(sideOf(assignments, 2) == machining::EndCutSide::Right, "second must be right");
            require(sideOf(assignments, 3) == machining::EndCutSide::Left, "third must be left");
            require(sideOf(assignments, 6) == machining::EndCutSide::Right, "sixth must be right");
        }

        // 手动指定作为新的交替基准，只影响其 +X 方向：左 右 [右] 左 右 左
        {
            std::vector<planning::EndCutSideBoundary> boundaries;
            for (int index = 1; index <= 6; ++index)
            {
                planning::EndCutSideBoundary boundary;
                boundary.boundaryPairId = index;
                boundary.minimumX = static_cast<double>(index) * 10.0;
                if (index == 3) boundary.explicitSide = machining::EndCutSide::Right;
                boundaries.push_back(boundary);
            }
            const auto assignments = planning::resolveEndCutSides(boundaries);
            require(sideOf(assignments, 1) == machining::EndCutSide::Left, "left of manual changed");
            require(sideOf(assignments, 2) == machining::EndCutSide::Right, "left of manual changed");
            require(sideOf(assignments, 3) == machining::EndCutSide::Right, "manual side ignored");
            require(sideOf(assignments, 4) == machining::EndCutSide::Left, "alternation not restarted");
            require(sideOf(assignments, 5) == machining::EndCutSide::Right, "alternation broken");
            require(sideOf(assignments, 6) == machining::EndCutSide::Left, "alternation broken");
        }

        // 手动“断面”不参与交替，也不改变交替基准。
        {
            std::vector<planning::EndCutSideBoundary> boundaries;
            for (int index = 1; index <= 3; ++index)
            {
                planning::EndCutSideBoundary boundary;
                boundary.boundaryPairId = index;
                boundary.minimumX = static_cast<double>(index);
                if (index == 1) boundary.explicitSide = machining::EndCutSide::Section;
                boundaries.push_back(boundary);
            }
            const auto assignments = planning::resolveEndCutSides(boundaries);
            require(sideOf(assignments, 1) == machining::EndCutSide::Section, "manual section lost");
            require(sideOf(assignments, 2) == machining::EndCutSide::Left, "section consumed a side slot");
            require(sideOf(assignments, 3) == machining::EndCutSide::Right, "alternation affected");
        }

        // 最小 X 相同时按断面编号稳定消歧。
        {
            std::vector<planning::EndCutSideBoundary> boundaries;
            boundaries.push_back({ 7, 5.0, std::nullopt });
            boundaries.push_back({ 2, 5.0, std::nullopt });
            const auto assignments = planning::resolveEndCutSides(boundaries);
            require(assignments.front().boundaryPairId == 2, "pairId tie-break failed");
            require(assignments.front().side == machining::EndCutSide::Left, "tie-break order wrong");
        }
    }
}

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    try
    {
        geometryTests();
        widgetTests();
        correctionTests();
        endCutSideTests();
        std::cout << "PASS: 108 inner-surface/thickness/radius/offset/curve/axis combinations, path reversal, closed paths, errors, display switches and end-cut side assignment.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
