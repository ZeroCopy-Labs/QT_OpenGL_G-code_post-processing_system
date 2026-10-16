#pragma once

#include "application/export/GGenerator.h"
#include "application/geometry/DocumentGeometrySnapshotBuilder.h"
#include "application/machine/MachineTrajectoryService.h"
#include "application/planning/ProcessPlanningService.h"
#include "cad/document/CadDocument.h"
#include "cad/items/CadItem.h"
#include "core/machining/TubeSectionProjector.h"
#include "core/machine/RotaryKinematics.h"
#include "drw_entities.h"
#include <array>
#include <cmath>
#include <stdexcept>

inline void boundaryMinimumXOrderTests()
{
    using namespace cadcam;
    // 先放右断面，再放倾斜更大的左断面；二者的 X 范围重叠且实际相交。
    for (double secondCenter : { 100.0, 142.0 })
    {
        CadDocument document;
        process::DocumentProcessState state;
        machining::TubeSectionModel section;
        section.geometry.yLength = 80.0;
        section.geometry.zWidth = 60.0;
        section.cornerRadius = 4.0;
        section.roundedCornerCount = 4;
        std::array<std::vector<geometry::EntityId>, 2> ids;
        for (int boundary = 0; boundary < 2; ++boundary)
        {
            std::vector<geometry::Vector3d> points;
            const int ys[] = { 1, 1, -1, -1 }, zs[] = { 1, -1, -1, 1 };
            for (int c = 0; c < 4; ++c)
            {
                const geometry::Vector2d center{ ys[c] * 36.0, zs[c] * 26.0 };
                if (boundary == 0) section.corners.push_back({ center, 4.0, ys[c], zs[c] });
                for (int i = 0; i <= 16; ++i)
                {
                    const double angle = 1.5707963267948966 * (1 - c - i / 16.0);
                    const double y = center.x + 4 * std::cos(angle), z = center.y + 4 * std::sin(angle);
                    points.push_back({ (boundary == 0 ? 110.0 + 0.2 * y : secondCenter + y), y, z });
                    if (boundary == 0) section.geometry.boundary.push_back({ y, z });
                }
            }
            for (std::size_t i = 0; i < points.size(); ++i)
            {
                auto line = std::make_unique<DRW_Line>();
                const auto& a = points[i];
                const auto& b = points[(i + 1) % points.size()];
                line->basePoint = DRW_Coord(a.x, a.y, a.z);
                line->secPoint = DRW_Coord(b.x, b.y, b.z);
                const auto id = document.appendEntity(std::move(line))->m_entityId;
                ids[boundary].push_back(id);
                state.setBoundary(id, planning::BoundaryRole::Break, boundary);
            }
        }
        section.contentRevision = document.contentRevision();
        planning::ProcessPlanningPolicy policy;
        policy.connectionTolerance = 0.001;
        auto snapshot = captureAndCompileDocumentGeometrySnapshot
        (document, GeometryExecutionMode::Serial, {});
    if (!snapshot.succeeded() || !snapshot.value.has_value())
    {
        throw std::runtime_error("geometry snapshot build failed");
    }
    auto result = ProcessPlanningService().buildRotaryPlan
        (*snapshot.value, state, section, policy, {});
        if (!result.succeeded() || !result.value.has_value())
        {
            for (const auto& diagnostic : result.diagnostics)
                std::cerr << diagnostic.userMessage.toStdString() << '\n';
            throw std::runtime_error("minimum-X boundary planning failed");
        }
        const int first = secondCenter == 100.0 ? 1 : 0;
        for (std::size_t i = 0; i < result.value->assignments.size(); ++i)
        {
            const auto id = result.value->assignments[i].entityId;
            const auto& expected = ids[i < ids[first].size() ? first : 1 - first];
            if (std::find(expected.begin(), expected.end(), id) == expected.end())
                throw std::runtime_error("boundary minimum-X order or stable tie failed");
        }
        GGenerator generator;
        generator.setGeometrySnapshot(&*snapshot.value);
        generator.setGenerationMode(GGenerator::GenerationMode::Mode3D);
        generator.setProcessState(&state);
        generator.setProcessPlan(&*result.value);
        generator.setTubeSectionModel(section);
        const auto program = generator.buildProgramText({});
        if (!program.succeeded() || !program.value.has_value() || program.value->isEmpty())
        {
            for (const auto& diagnostic : program.diagnostics)
                std::cerr << diagnostic.userMessage.toStdString() << '\n';
            throw std::runtime_error("minimum-X sorted plan could not export G-code");
        }

        // 端面归属由规划阶段判定：两个断面按最小 X 依次为左端面、右端面。
        if (result.value->endCutSides.size() != 2U)
            throw std::runtime_error("end-cut side assignment missing from plan");
        const auto sideOf = [&result](int pairId)
        {
            for (const auto& assignment : result.value->endCutSides)
                if (assignment.boundaryPairId == pairId) return assignment.side;
            throw std::runtime_error("end-cut side assignment missing for boundary");
        };
        if (sideOf(first) != machining::EndCutSide::Left
            || sideOf(1 - first) != machining::EndCutSide::Right)
            throw std::runtime_error("end-cut side alternation wrong for minimum-X order");

        // 两个端面修正开关同时开启时仍应导出成功：左端面只补正向差值（+X）、右端面只补负向差值（−X）。
        generator.setCutSurfaceCorrection(6.0, true, true);
        const auto correctedProgram = generator.buildProgramText({});
        if (!correctedProgram.succeeded() || !correctedProgram.value.has_value()
            || correctedProgram.value->isEmpty())
        {
            for (const auto& diagnostic : correctedProgram.diagnostics)
                std::cerr << diagnostic.userMessage.toStdString() << '\n';
            throw std::runtime_error("end-cut corrected export failed");
        }
    }
    std::cout << "PASS: crossing boundaries sorted by minimum X, stable equal minima and G-code export.\n";
}

inline void cutSurfaceCorrectionFlowTests()
{
    using namespace cadcam;
    const auto check = [](bool ok) { if (!ok) throw std::runtime_error("cut surface export flow failed"); };
    const auto near = [&](double a, double b) { check(std::abs(a - b) < 1.0e-7); };
    CadDocument document;
    process::DocumentProcessState state;
    planning::ProcessPlan plan;
    plan.mode = planning::ProcessPlanMode::Rotary4Axis;
    machining::TubeSectionModel section;
    section.geometry.yLength = 80;
    section.geometry.zWidth = 60;
    section.cornerRadius = 4;
    section.roundedCornerCount = 4;
    std::vector<geometry::Vector3d> points;
    const int ys[] = { 1, 1, -1, -1 }, zs[] = { 1, -1, -1, 1 };
    for (int c = 0; c < 4; ++c)
    {
        const geometry::Vector2d center{ ys[c] * 36.0, zs[c] * 26.0 };
        section.corners.push_back({ center, 4.0, ys[c], zs[c] });
        for (int i = 0; i <= 16; ++i)
        {
            const double a = 1.5707963267948966 * (1.0 - c - i / 16.0);
            const double y = center.x + 4 * std::cos(a), z = center.y + 4 * std::sin(a);
            points.push_back({ 100 + 0.7 * y - 0.4 * z, y, z });
            section.geometry.boundary.push_back({ y, z });
        }
    }
    planning::ProcessUnit unit;
    unit.closed = true;
    for (std::size_t i = 0; i < points.size(); ++i)
    {
        auto line = std::make_unique<DRW_Line>();
        const auto& a = points[i];
        const auto& b = points[(i + 1) % points.size()];
        line->basePoint = DRW_Coord(a.x, a.y, a.z);
        line->secPoint = DRW_Coord(b.x, b.y, b.z);
        const auto id = document.appendEntity(std::move(line))->m_entityId;
        unit.key.memberEntityIds.push_back(id);
        unit.orderedMemberEntityIds.push_back(id);
        planning::ProcessAssignment assignment;
        assignment.entityId = id;
        assignment.processOrder = static_cast<int>(i);
        assignment.processUnitIndex = 0;
        assignment.continuousGroupId = 0;
        plan.assignments.push_back(assignment);
        state.setBoundary(id, planning::BoundaryRole::Break, 0);
    }
    plan.processUnits.push_back(unit);
    plan.processUnitSequence.units.push_back(unit.key);
    plan.groups.push_back({ 0, planning::ProcessGroupKind::BreakBoundary, true, 0, unit.orderedMemberEntityIds });
    plan.contentRevision = document.contentRevision();
    section.contentRevision = plan.contentRevision;
    plan.processStateRevision = state.revision();
    planning::EndCutSideAssignment endCutSide;
    endCutSide.boundaryPairId = 0;
    endCutSide.side = machining::EndCutSide::Left;
    endCutSide.userSpecified = true;
    plan.endCutSides.push_back(endCutSide);
    GProfile profile;
    GProfileRotaryAxisConfig rotary = profile.rotaryAxisConfig();
    rotary.machiningPlaneZOffset = 1.25;
    profile.setRotaryAxisConfig(rotary);
    // 轨迹服务只消费几何快照；这里按当前文档建立快照。
    auto trajectorySnapshot = captureAndCompileDocumentGeometrySnapshot
        (document, GeometryExecutionMode::Serial, {});
    if (!trajectorySnapshot.succeeded() || !trajectorySnapshot.value.has_value())
    {
        throw std::runtime_error("geometry snapshot build failed");
    }
    GGenerator generator;
    generator.setProfile(&profile);
    generator.setGenerationMode(GGenerator::GenerationMode::Mode3D);
    generator.setProcessState(&state);
    generator.setProcessPlan(&plan);
    generator.setGeometrySnapshot(&*trajectorySnapshot.value);
    generator.setTubeSectionModel(section);
    std::array<QString, 4> programs;
    for (int mode = 0; mode < 4; ++mode)
    {
        const bool positive = (mode & 1) != 0, negative = (mode & 2) != 0;
        auto config = MachiningProcessConfig::fromProfile(profile);
        config.tubeWallThickness = 6.0;
        config.cutSurfacePositiveCorrection = positive;
        config.cutSurfaceNegativeCorrection = negative;
        const auto trajectory = MachineTrajectoryService().buildRotaryTrajectory
            (*trajectorySnapshot.value, state, plan, section, config, {});
        for (const auto& d : trajectory.diagnostics)
            if (d.severity == DiagnosticSeverity::Error) std::cerr << d.userMessage.toStdString() << '\n';
        check(trajectory.succeeded() && trajectory.value.has_value());
        machine::RotaryMachinePolicy policy;
        policy.machiningPlaneZOffset = 1.25;
        for (std::size_t i = 0; i < points.size(); ++i)
        {
            geometry::Path3D source;
            source.vertices = { { points[i] }, { points[(i + 1) % points.size()] } };
            const auto poses = machine::RotaryKinematics::transform(source, policy, section, std::nullopt, {});
            check(poses.value.has_value());
            const auto& entity = trajectory.value->entities[i];
            for (std::size_t j = 0; j < 2; ++j)
            {
                const auto ray = machine::RotaryKinematics::sourceToolRay(poses.value->poses[j], policy);
                check(ray.has_value());
                const auto inner = machining::TubeSectionProjector::projectToInnerSurface(section, 6.0,
                    { ray->origin.y + 6.0 * ray->unitDirection.y, ray->origin.z + 6.0 * ray->unitDirection.z });
                check(inner.has_value());
                const auto& p = source.vertices[j].position;
                const double delta = 0.7 * (inner->x - p.y) - 0.4 * (inner->y - p.z);
                // 该断面判定为左端面：只补正向差值（向 +X），即只有正向修正开关生效。
                const double x = p.x + ((positive && delta > 0) ? delta : 0);
                near(entity.sourcePath[j].x, x);
                if (j == 1) near(entity.cuttingMoves.back().target.x, x);
            }
        }
        const auto& first = trajectory.value->entities.front();
        check(!first.approachMoves.empty());
        near(first.approachMoves.back().target.x, first.sourcePath.front().x);
        check(!trajectory.value->entities.back().overcutMoves.empty());
        generator.setCutSurfaceCorrection(6.0, positive, negative);
        const auto program = generator.buildProgramText({});
        check(program.succeeded() && program.value.has_value() && !program.value->isEmpty());
        programs[mode] = *program.value;
    }
    // 左端面只补正向差值：反向修正开关单独开启不产生任何变化。
    check(programs[0] == programs[2] && programs[1] == programs[3] && programs[0] != programs[1]);
    generator.setCutSurfaceCorrection(6.0, false, false);
    check(generator.buildProgramText({}).value == std::optional<QString>(programs[0]));
    // 手动“断面”不做任何修正：两个开关同时开启也与全关一致。
    plan.endCutSides[0].side = machining::EndCutSide::Section;
    generator.setCutSurfaceCorrection(6.0, true, true);
    check(generator.buildProgramText({}).value == std::optional<QString>(programs[0]));
    plan.endCutSides[0].side = machining::EndCutSide::Left;
    generator.setCutSurfaceCorrection(30.0, true, true);
    check(!generator.buildProgramText({}).succeeded());
    // 取消断面标记：普通加工单元不受修正影响，即使壁厚无效也不使用该工艺。
    for (const auto id : unit.orderedMemberEntityIds) state.setBoundary(id, planning::BoundaryRole::None, -1);
    plan.processStateRevision = state.revision();
    generator.setCutSurfaceCorrection(30.0, true, true);
    const auto ordinary = generator.buildProgramText({});
    check(ordinary.succeeded());
    generator.setCutSurfaceCorrection(30.0, false, false);
    check(ordinary.value == generator.buildProgramText({}).value);
    std::cout << "PASS: document -> trajectory -> G-code, signed correction, approach, overcut, disable and ordinary units.\n";
}
