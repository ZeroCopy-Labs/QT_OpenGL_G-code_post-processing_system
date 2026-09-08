#pragma once

#include "application/export/GGenerator.h"
#include "application/machine/MachineTrajectoryService.h"
#include "cad/document/CadDocument.h"
#include "cad/items/CadItem.h"
#include "core/machining/TubeSectionProjector.h"
#include "core/machine/RotaryKinematics.h"
#include "drw_entities.h"
#include <array>
#include <cmath>
#include <stdexcept>

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
    plan.groups.push_back({ 0, planning::ProcessGroupKind::BreakBoundary, true, unit.orderedMemberEntityIds });
    plan.contentRevision = document.contentRevision();
    section.contentRevision = plan.contentRevision;
    plan.processStateRevision = state.revision();
    GProfile profile;
    GProfileRotaryAxisConfig rotary = profile.rotaryAxisConfig();
    rotary.machiningPlaneZOffset = 1.25;
    profile.setRotaryAxisConfig(rotary);
    GGenerator generator;
    generator.setDocument(&document);
    generator.setProfile(&profile);
    generator.setGenerationMode(GGenerator::GenerationMode::Mode3D);
    generator.setProcessState(&state);
    generator.setProcessPlan(&plan);
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
            (document, state, plan, section, config, {});
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
                const double x = p.x + (((positive && delta > 0) || (negative && delta < 0)) ? delta : 0);
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
    check(programs[0] != programs[1] && programs[0] != programs[2] && programs[1] != programs[3]);
    generator.setCutSurfaceCorrection(6.0, false, false);
    check(generator.buildProgramText({}).value == std::optional<QString>(programs[0]));
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
