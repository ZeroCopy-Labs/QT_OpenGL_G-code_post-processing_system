#pragma once

#include "ui/dialogs/ToolTransferDialog.h"
#include "ui/dialogs/GProfileDialog.h"
#include "infrastructure/config/MachiningParameterStore.h"
#include "application/geometry/DocumentGeometrySnapshotBuilder.h"
#include "application/machine/MachineTrajectoryService.h"
#include "application/planning/ProcessPlanningService.h"
#include "application/export/GGenerator.h"
#include "cad/document/CadDocument.h"
#include "cad/items/CadItem.h"
#include "drw_entities.h"
#include <QCheckBox>
#include <QApplication>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QTimer>
#include <QPixmap>
#include <QFontDatabase>
#include <limits>
#include <iostream>
#include <stdexcept>

inline void toolTransferTests()
{
    using namespace cadcam;
    const auto check = [](bool ok) { if (!ok) throw std::runtime_error("tool transfer regression"); };
    const auto near = [&](double a, double b) { check(std::abs(a - b) < 1.0e-7); };
    machine::RotaryTransferRequest request;
    request.previousProcessUnitIndex = 0;
    request.nextProcessUnitIndex = 1;
    request.previousOwnerZone = request.nextOwnerZone = static_cast<machining::TubeZone16>(0);
    request.policy.sameZone.minimumRetractHeight = 2;
    request.policy.sameZone.minimumApproachHeight = 3;
    request.policy.sameZone.automaticTotalHeight = false;
    request.policy.sameZoneTransferClearance = 10;
    request.nextCutStart = { 100, 0, 0, 0 };
    auto preview = machine::RotaryTransferPlanner::preview(request, {});
    check(preview.value.has_value() && preview.value->targets.size() == 5);
    const double xs[] = { 0, 25, 75, 100, 100 }, zs[] = { 2, 10, 10, 3, 0 };
    for (std::size_t i = 0; i < 5; ++i) { near(preview.value->targets[i].x, xs[i]); near(preview.value->targets[i].z, zs[i]); }
    near(preview.value->finalApproachOrigin.z, 3);
    for (bool linked : { false, true })
    for (double distance : { 0.0, 0.001, 100.0 })
    for (double endZ : { -5.0, 0.0, 5.0 })
    for (double ratio : { 0.0, 0.25, 0.5 })
    {
        request.policy.coordinatedTransferEnabled = linked;
        request.nextCutStart = { distance, distance * 0.5, endZ, 0 };
        request.policy.sameZone.departureRatio = request.policy.sameZone.approachRatio = ratio;
        preview = machine::RotaryTransferPlanner::preview(request, {});
        check(preview.succeeded() && preview.value.has_value());
        auto previous = request.previousCutEnd;
        for (const auto& point : preview.value->targets)
        {
            check(point.x >= previous.x && point.x <= distance);
            near(point.aDegrees, 0);
            if (point.x != previous.x || point.y != previous.y)
            {
                if (point.z >= previous.z) check(previous.z >= 2);
                else check(point.z >= endZ + 3);
                if (!linked) near(point.z, std::max(0.0, endZ) + 10);
            }
            previous = point;
        }
        near(previous.x, distance); near(previous.z, endZ);
        check(preview.value->targets.size() <= (linked ? 5U : 3U));
    }
    request.policy.sameZone.automaticTotalHeight = true;
    near(request.policy.sameZone.totalHeight(100), 3);
    request.policy.sameZone.departureRatio = 0.8;
    request.policy.sameZone.approachRatio = 0.3;
    check(!machine::RotaryTransferPlanner::preview(request, {}).succeeded());
    request.policy.sameZone.departureRatio = 0.25;
    request.policy.sameZone.approachRatio = 0.25;
    request.policy.sameZone.automaticTotalHeight = false;
    request.policy.sameZoneTransferClearance = 1;
    check(!machine::RotaryTransferPlanner::preview(request, {}).succeeded());
    request.policy.sameZoneTransferClearance = 10;
    request.policy.sameZone.minimumRetractHeight = std::numeric_limits<double>::quiet_NaN();
    check(!machine::RotaryTransferPlanner::preview(request, {}).succeeded());

    auto cross = request;
    cross.policy.sameZone = {};
    cross.nextCutStart = { 100, 0, 0, 90 };
    cross.nextOwnerZone = static_cast<machining::TubeZone16>(4);
    cross.rotationSafeMachineZ = 60;
    auto crossPreview = machine::RotaryTransferPlanner::preview(cross, {});
    check(crossPreview.succeeded() && crossPreview.value->targets.size() == 3);
    near(crossPreview.value->targets[0].x, 25);
    near(crossPreview.value->targets[0].z, 60);
    near(crossPreview.value->targets[1].x, 75);
    near(crossPreview.value->targets[1].aDegrees, 90);

    GProfileToolTransferConfig config;
    config.sameZone.minimumRetractHeight = 2;
    config.sameZone.minimumApproachHeight = 3;
    config.sameZone.automaticTotalHeight = false;
    config.sameZoneTransferClearance = 10;
    const auto json = config.toJson();
    check(GProfileToolTransferConfig::fromJson(json).toJson() == json);
    const auto legacy = GProfileToolTransferConfig::fromJson({ { QStringLiteral("sameZoneTransferClearance"), 7.0 } });
    check(!legacy.sameZone.automaticTotalHeight);
    near(legacy.sameZone.totalHeight(legacy.sameZoneTransferClearance), 7);
    MachiningParameterStore::save(config, {});
    GProfile stored;
    MachiningParameterStore::applyTo(stored);
    check(stored.toolTransferConfig().toJson() == json);

    ToolTransferDialog dialog(config);
    auto* automatic = dialog.findChild<QCheckBox*>(QStringLiteral("automaticTotalHeight"));
    auto* height = dialog.findChild<QDoubleSpinBox*>(QStringLiteral("totalTransferHeight"));
    auto* depart = dialog.findChild<QDoubleSpinBox*>(QStringLiteral("departureRatio"));
    auto* approach = dialog.findChild<QDoubleSpinBox*>(QStringLiteral("approachRatio"));
    check(automatic && height && depart && approach);
    automatic->setChecked(true);
    near(height->value(), 3);
    check(!height->isEnabled());
    automatic->setChecked(false);
    height->setValue(1);
    near(height->value(), 3);
    height->setValue(10);
    depart->setValue(80); approach->setValue(30);
    check(!dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->isEnabled());
    depart->setValue(25); approach->setValue(25);
    check(dialog.examplePreview().value->targets.size() == 5);
    const int fontId = QFontDatabase::addApplicationFont(QStringLiteral("C:/Windows/Fonts/msyh.ttc"));
    if (fontId >= 0) dialog.setFont(QFont(QFontDatabase::applicationFontFamilies(fontId).front(), 9));
    dialog.show();
    QApplication::processEvents();
    check(dialog.grab().save(QStringLiteral("tool-transfer-preview.png")));
    dialog.hide();

    // 子对话框取消不修改父配置，确认后仍需父对话框确认。
    GProfileDialog parent(stored, {}, {}, buildAppThemeColors(AppThemeMode::Light));
    auto* button = parent.findChild<QPushButton*>(QStringLiteral("configureToolTransfer"));
    check(button != nullptr);
    QTimer::singleShot(0, [&]()
    {
        auto* child = dynamic_cast<ToolTransferDialog*>(QApplication::activeModalWidget());
        if (!child) return;
        child->findChild<QDoubleSpinBox*>(QStringLiteral("minimumRetractHeight"))->setValue(8);
        child->reject();
    });
    button->click();
    static_cast<QDialog&>(parent).accept();
    near(parent.profile().toolTransferConfig().sameZone.minimumRetractHeight, 2);

    // 真实文档 -> 懒旋转同区位计划（含空移签名） -> 轨迹 -> G-code。
    CadDocument document;
    process::DocumentProcessState state;
    for (double x : { 0.0, 100.0 })
    {
        auto line = std::make_unique<DRW_Line>();
        line->basePoint = DRW_Coord(x, 0, 30);
        line->secPoint = DRW_Coord(x + 10, 0, 30);
        document.appendEntity(std::move(line));
    }
    machining::TubeSectionModel section;
    section.contentRevision = document.contentRevision();
    section.geometry.yLength = 80;
    section.geometry.zWidth = 60;
    section.geometry.boundary = { { -40, -30 }, { 40, -30 }, { 40, 30 }, { -40, 30 } };
    for (bool linked : { false, true })
    for (double ratio : { 0.25, 0.5 })
    {
        planning::ProcessPlanningPolicy policy;
        policy.orderingStrategy = planning::ProcessOrderingStrategy::LazyRotation;
        policy.sameZoneTransferClearance = config.sameZoneTransferClearance;
        policy.sameZone = config.sameZone;
        policy.sameZone.departureRatio = policy.sameZone.approachRatio = ratio;
        policy.coordinatedTransferEnabled = linked;
        auto snapshot = captureAndCompileDocumentGeometrySnapshot
            (document, GeometryExecutionMode::Serial, {});
        check(snapshot.succeeded() && snapshot.value.has_value());
        auto plan = ProcessPlanningService().buildRotaryPlan
            (*snapshot.value, state, section, policy, {});
        check(plan.succeeded() && plan.value.has_value());
        auto processConfig = MachiningProcessConfig::fromProfile(stored);
        processConfig.sameZone = policy.sameZone;
        processConfig.coordinatedTransferEnabled = linked;
        auto trajectory = MachineTrajectoryService().buildRotaryTrajectory
            (document, state, *plan.value, section, processConfig, {});
        if (!trajectory.succeeded())
            for (const auto& diagnostic : trajectory.diagnostics)
                std::cerr << diagnostic.userMessage.toStdString() << '\n';
        check(trajectory.succeeded() && trajectory.value.has_value());
        const auto& moves = trajectory.value->entities.back().approachMoves;
        check(moves.size() == (linked ? (ratio == 0.5 ? 4U : 5U) : 3U));
        near(moves.front().target.z, linked ? 32 : 40);
        near(moves.back().target.z, 30);
        if (linked)
        {
            // 同区位转移的形状参数变化不会让轨迹层拒绝构建（它消费计划签名并按当前参数重组阶段），
            // 整体失效依赖桌面层“修改配置即清除计划”。此处固定当前行为，避免被误认为强校验。
            auto changed = processConfig;
            changed.sameZoneTransferClearance = processConfig.sameZoneTransferClearance + 10.0;
            changed.sameZone.departureRatio = 0.1;
            changed.sameZone.approachRatio = 0.1;
            auto changedTrajectory = MachineTrajectoryService().buildRotaryTrajectory
                (document, state, *plan.value, section, changed, {});
            check(changedTrajectory.succeeded() && changedTrajectory.value.has_value());
        }
        auto transfer = config;
        transfer.sameZone = policy.sameZone;
        transfer.coordinatedTransferEnabled = linked;
        stored.setToolTransferConfig(transfer);
        GGenerator generator;
        generator.setDocument(&document);
        generator.setProfile(&stored);
        generator.setProcessState(&state);
        generator.setProcessPlan(&*plan.value);
        generator.setTubeSectionModel(section);
        generator.setGenerationMode(GGenerator::GenerationMode::Mode3D);
        check(generator.buildProgramText({}).succeeded());
    }
    std::cout << "PASS: five-stage transfer, 54 degenerate/offset/ratio cases, persistence and dialog constraints.\n";
}
