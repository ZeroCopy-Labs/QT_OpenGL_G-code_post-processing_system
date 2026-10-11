#include "platform/pch.h"

#include "desktop/Gcode_postprocessing_system.h"
#include "core/diagnostics/SummaryLog.h"

#include "cad/items/CadItem.h"
#include "cad/items/CadCircleItem.h"
#include "cad/items/CadEllipseItem.h"
#include "cad/geometry/CadEllipseGeometry.h"
#include "cad/geometry/CadOcsGeometry.h"
#include "cad/view/rendering/CadProcessVisualUtils.h"
#include "application/machining/RotaryCutBoundaryAnalyzer.h"
#include "application/machining/RotaryPathTopology.h"
#include "application/planning/ProcessPlanningService.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QHash>
#include <QMessageBox>
#include <QSet>
#include <QStatusBar>
#include <QStringList>

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <vector>
#include "desktop/internal/ProcessActionSupport.h"


QVector<CadItem*> Gcode_postprocessing_system::expandedSelectedRotaryEndCut
(
    const RotaryBoundaryOperationGeometry& geometry,
    QString* errorMessage
) const
{
    BoundaryPerformanceTimer selectionTimer
        (m_boundaryAssignmentPerformanceReport != nullptr
            ? &m_boundaryAssignmentPerformanceReport->selectionExpansionMs : nullptr);
    const QVector<CadItem*> selectedItems = ui->openGLWidget->selectedEntities();

    if (selectedItems.isEmpty())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("请先选中加工断面中的一个或部分图元。系统会自动扩展相连图元。");
        }

        return {};
    }

    cadcam::core::emitSummaryLog(QStringLiteral("断面候选"), QString(), QStringLiteral("选择集：%1")
        .arg(describeRotaryPathItems(selectedItems)));
    cadcam::core::emitSummaryLog(QStringLiteral("断面候选"), QString(), QStringLiteral("sceneItems 过滤后：%1")
        .arg(describeRotaryPathItems(geometry.sceneItems)));
    QVector<CadItem*> connectedItems;
    RotaryPathLoopResult loop;
    {
        BoundaryPerformanceTimer orderingTimer
            (m_boundaryAssignmentPerformanceReport != nullptr
                ? &m_boundaryAssignmentPerformanceReport->boundaryOrderingMs : nullptr);
        if (m_boundaryAssignmentPerformanceReport != nullptr)
        {
            ++m_boundaryAssignmentPerformanceReport->topologyReuseCount;
        }
        loop = geometry.topology->extractSeededLoop(selectedItems, &connectedItems);
    }
    cadcam::core::emitSummaryLog(QStringLiteral("断面候选"), QString(), QStringLiteral("连通扩展：%1")
        .arg(describeRotaryPathItems(connectedItems)));

    if (!loop.valid || loop.usedItems.isEmpty())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = loop.errorMessage;
        }

        return {};
    }

    const RotaryCutBoundaryAnalysis analysis = analyzeBoundary
    (
        loop.usedItems,
        *geometry.topology,
        m_rotaryTubeSectionModel,
        kEndCutConnectionTolerance,
        m_boundaryAssignmentPerformanceReport
    );

    if (!analysis.valid)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = analysis.errorMessage;
        }

        return {};
    }

    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    return analysis.boundaryItems.isEmpty() ? loop.usedItems : analysis.boundaryItems;
}

bool Gcode_postprocessing_system::assignSelectedRotaryEndCut()
{
    BoundaryAssignmentPerformanceOperation performance
    (
        m_boundaryAssignmentPerformanceReport,
        QStringLiteral("AssignRotaryEndCut"),
        static_cast<std::uint64_t>(m_document.m_entities.size()),
        static_cast<std::uint64_t>(ui->openGLWidget->selectedEntities().size())
    );
    const QVector<CadItem*> selectedItems = ui->openGLWidget->selectedEntities();
    auto operationGeometry = buildRotaryBoundaryOperationGeometry(selectedItems);
    if (!operationGeometry.succeeded() || !operationGeometry.value.has_value())
    {
        QMessageBox::warning(this, QStringLiteral("指定加工断面"),
            firstDiagnosticMessage(operationGeometry.diagnostics));
        return false;
    }
    QString selectionError;
    const QVector<CadItem*> expandedItems = expandedSelectedRotaryEndCut
        (*operationGeometry.value, &selectionError);

    if (expandedItems.isEmpty())
    {
        QMessageBox::warning(this, QStringLiteral("指定加工断面"), selectionError);
        return false;
    }

    for (const CadItem* item : expandedItems)
    {
        if (item != nullptr && m_processState.stateOrDefault(item->m_entityId)
            .overrideData.boundaryRole != cadcam::planning::BoundaryRole::None)
        {
            QMessageBox::warning
            (
                this,
                QStringLiteral("指定加工断面"),
                QStringLiteral("选中的图元已属于一个加工断面边界。如需重新指定，请先清除加工断面指定。")
            );
            return false;
        }
    }

    int highestBoundaryId = -1;

    for (const std::unique_ptr<CadItem>& entity : m_document.m_entities)
    {
        if (entity == nullptr)
        {
            continue;
        }
        const auto state = m_processState.stateOrDefault(entity->m_entityId);
        if (state.overrideData.boundaryRole != cadcam::planning::BoundaryRole::None)
            highestBoundaryId = std::max(highestBoundaryId, state.overrideData.boundaryPairId);
    }

    const int boundaryId = highestBoundaryId + 1;

    {
        BoundaryPerformanceTimer stateTimer
            (m_boundaryAssignmentPerformanceReport != nullptr
                ? &m_boundaryAssignmentPerformanceReport->processStateUpdateMs : nullptr);
        m_processState.beginBatch();
        for (CadItem* item : expandedItems)
        {
            if (item == nullptr)
            {
                continue;
            }

            m_processState.setBoundary
                (item->m_entityId, cadcam::planning::BoundaryRole::Break, boundaryId);
        }
        m_processState.endBatch();
    }

    const QString message = QStringLiteral("已指定加工断面 断%1，共识别 %2 个相连图元，已通过方管周向分离验证。")
        .arg(boundaryId + 1)
        .arg(expandedItems.size());

    invalidateProcessOrdersAfterEndCutChange();
    refreshWasteProcessingExclusions(*operationGeometry.value);
    ui->openGLWidget->appendCommandMessage(message);
    updateBoundaryViewer(ui->openGLWidget, m_boundaryAssignmentPerformanceReport);
    statusBar()->showMessage(message, 5000);
    return true;
}

bool Gcode_postprocessing_system::assignSelectedWasteEndCut()
{
    BoundaryAssignmentPerformanceOperation performance
    (
        m_boundaryAssignmentPerformanceReport,
        QStringLiteral("AssignWasteEndCut"),
        static_cast<std::uint64_t>(m_document.m_entities.size()),
        static_cast<std::uint64_t>(ui->openGLWidget->selectedEntities().size())
    );
    const QVector<CadItem*> selectedItems = ui->openGLWidget->selectedEntities();
    auto operationGeometry = buildRotaryBoundaryOperationGeometry(selectedItems);
    if (!operationGeometry.succeeded() || !operationGeometry.value.has_value())
    {
        QMessageBox::warning(this, QStringLiteral("指定废面"),
            firstDiagnosticMessage(operationGeometry.diagnostics));
        return false;
    }
    QString selectionError;
    const QVector<CadItem*> expandedItems = expandedSelectedRotaryEndCut
        (*operationGeometry.value, &selectionError);

    if (expandedItems.isEmpty())
    {
        QMessageBox::warning(this, QStringLiteral("指定废面"), selectionError);
        return false;
    }

    for (const CadItem* item : expandedItems)
    {
        if (item != nullptr && m_processState.stateOrDefault(item->m_entityId)
            .overrideData.boundaryRole != cadcam::planning::BoundaryRole::None)
        {
            QMessageBox::warning(this, QStringLiteral("指定废面"), QStringLiteral("选中的图元已属于一个加工断面边界，请先清除原加工断面指定。"));
            return false;
        }
    }

    int highestBoundaryId = -1;

    for (const std::unique_ptr<CadItem>& entity : m_document.m_entities)
    {
        if (entity != nullptr)
        {
            const auto state = m_processState.stateOrDefault(entity->m_entityId);
            if (state.overrideData.boundaryRole != cadcam::planning::BoundaryRole::None)
                highestBoundaryId = std::max(highestBoundaryId, state.overrideData.boundaryPairId);
        }
    }

    const int wasteId = highestBoundaryId + 1;

    {
        BoundaryPerformanceTimer stateTimer
            (m_boundaryAssignmentPerformanceReport != nullptr
                ? &m_boundaryAssignmentPerformanceReport->processStateUpdateMs : nullptr);
        m_processState.beginBatch();
        for (CadItem* item : expandedItems)
        {
            if (item == nullptr)
            {
                continue;
            }

            m_processState.setBoundary
                (item->m_entityId, cadcam::planning::BoundaryRole::Waste, wasteId);
        }
        m_processState.endBatch();
    }

    invalidateProcessOrdersAfterEndCutChange();
    const int excludedCount = refreshWasteProcessingExclusions(*operationGeometry.value);
    const QString message = QStringLiteral("已指定废弃面 W%1，共识别 %2 个相连图元，已通过方管周向分离验证；当前废弃区共排除 %3 个图元。")
        .arg(wasteId + 1)
        .arg(expandedItems.size())
        .arg(excludedCount);
    ui->openGLWidget->appendCommandMessage(message);
    updateBoundaryViewer(ui->openGLWidget, m_boundaryAssignmentPerformanceReport);
    statusBar()->showMessage(message, 6000);
    return true;
}

bool Gcode_postprocessing_system::smartAssignSelectedRotaryEndCut()
{
    BoundaryAssignmentPerformanceOperation performance
    (
        m_boundaryAssignmentPerformanceReport,
        QStringLiteral("SmartAssignRotaryEndCut"),
        static_cast<std::uint64_t>(m_document.m_entities.size()),
        static_cast<std::uint64_t>(ui->openGLWidget->selectedEntities().size())
    );
    return assignSelectedRotaryEndCut();
}

bool Gcode_postprocessing_system::recognizeRotaryTubeSection(bool interactive)
{
    QVector<CadItem*> sceneItems;
    sceneItems.reserve(static_cast<qsizetype>(m_document.m_entities.size()));

    for (const std::unique_ptr<CadItem>& entity : m_document.m_entities)
    {
        if (entity != nullptr)
        {
            sceneItems.push_back(entity.get());
        }
    }

    RotaryTubeSectionModel recognized;
    const QVector<CadItem*> selectedItems = ui->openGLWidget->selectedEntities();

    if (!interactive)
    {
        recognized = RotaryTubeGeometryAnalyzer::findBestSectionModel
        (
            sceneItems,
            kEndCutConnectionTolerance,
            m_document.contentRevision()
        );

        QString diagnostic = QStringLiteral("自动截面识别：检查候选 %1 个，有效 %2 个，圆角候选 %3 个")
            .arg(recognized.inspectedCandidateCount)
            .arg(recognized.validCandidateCount)
            .arg(recognized.roundedCandidateCount);

        if (recognized.valid)
        {
            diagnostic += QStringLiteral("；选中 X=%1，Y长=%2，Z宽=%3，圆角数=%4，R=%5")
                .arg(recognized.centerX, 0, 'f', 3)
                .arg(recognized.yLength, 0, 'f', 3)
                .arg(recognized.zWidth, 0, 'f', 3)
                .arg(recognized.roundedCornerCount)
                .arg(recognized.cornerRadius, 0, 'f', 3);
        }

        ui->openGLWidget->appendCommandMessage(diagnostic);

        if (recognized.valid && recognized.roundedCornerCount == 0)
        {
            ui->openGLWidget->appendCommandMessage
            (
                QStringLiteral("未找到可靠圆角截面，已回退使用直角截面。")
            );
        }
    }
    else if (!selectedItems.isEmpty())
    {
        recognized = RotaryTubeGeometryAnalyzer::buildSectionModel
        (
            selectedItems,
            sceneItems,
            kEndCutConnectionTolerance,
            m_document.contentRevision()
        );
    }
    else
    {
        recognized.errorMessage = QStringLiteral("请先选择方管垂直截面中的一个或部分图元。");
    }

    if (!recognized.valid)
    {
        const QString errorMessage = recognized.errorMessage.isEmpty()
            ? QStringLiteral("未找到有效的方管垂直截面。")
            : recognized.errorMessage;

        if (interactive)
        {
            QMessageBox::warning(this, QStringLiteral("识别方管垂直截面"), errorMessage);
        }

        ui->openGLWidget->appendCommandMessage(QStringLiteral("方管垂直截面识别失败：%1").arg(errorMessage));
        statusBar()->showMessage(QStringLiteral("方管垂直截面识别失败"), 5000);
        return false;
    }

    recognized.setUserCenter(m_rotaryTubeSectionModel.userCenter);
    m_rotaryTubeSectionModel = recognized;
    invalidateProcessOrdersAfterEndCutChange();
    syncToolPanelState();
    const QString message = QStringLiteral("方管垂直截面识别完成，共提取 %1 个外轮廓点；内部线和无用支线已忽略。")
        .arg(recognized.sectionBoundary.size());
    ui->openGLWidget->appendCommandMessage(message);
    statusBar()->showMessage(message, 5000);
    return true;
}

bool Gcode_postprocessing_system::setRotaryTubeSectionUserCenter
    (std::optional<cadcam::geometry::Vector2d> center)
{
    const bool hadUserCenter = m_rotaryTubeSectionModel.userCenter.has_value();
    const cadcam::geometry::Vector2d previous =
        m_rotaryTubeSectionModel.effectiveCenter();
    if (!m_rotaryTubeSectionModel.setUserCenter(center)) return false;
    const cadcam::geometry::Vector2d current =
        m_rotaryTubeSectionModel.effectiveCenter();
    const bool centerChanged = previous.x != current.x || previous.y != current.y;
    const bool presenceChanged =
        hadUserCenter != m_rotaryTubeSectionModel.userCenter.has_value();
    if (!centerChanged && !presenceChanged) return true;

    if (centerChanged) invalidateProcessOrdersAfterEndCutChange();
    syncToolPanelState();
    syncMachiningSettingsState();
    return true;
}

bool Gcode_postprocessing_system::removeInternalMachiningPaths(bool interactive)
{
    Q_UNUSED(interactive);
    QVector<CadItem*> sceneItems;
    sceneItems.reserve(static_cast<qsizetype>(m_document.m_entities.size()));

    for (const std::unique_ptr<CadItem>& entity : m_document.m_entities)
    {
        if (entity != nullptr)
        {
            sceneItems.push_back(entity.get());
        }
    }

    const RotaryInternalPathResult result =
        RotaryTubeGeometryAnalyzer::findInternalItemsByWindow
            (m_rotaryTubeSectionModel, sceneItems);
    if (!result.sectionAvailable)
    {
        const QString message =
            QStringLiteral("未识别有效方管截面，未执行内部线条清理。");
        cadcam::core::emitSummaryLog
        (
            QStringLiteral("InternalPathWindow"),
            QStringLiteral("Status"),
            QStringLiteral("sectionAvailable=false windowCollapsed=false "
                "removedEntityCount=0 candidatePathCount=0"));
        ui->openGLWidget->appendCommandMessage(message);
        statusBar()->showMessage(message, 6000);
        return false;
    }
    if (result.windowCollapsed)
    {
        for (const Diagnostic& diagnostic : result.diagnostics)
        {
            if (!diagnostic.userMessage.isEmpty())
            {
                ui->openGLWidget->appendCommandMessage(diagnostic.userMessage);
            }
        }
        cadcam::core::emitSummaryLog
        (
            QStringLiteral("InternalPathWindow"),
            QStringLiteral("Status"),
            QStringLiteral("sectionAvailable=true windowCollapsed=true "
                "insetDistance=%1 removedEntityCount=0")
                .arg(result.insetDistance, 0, 'f', 6));
        statusBar()->showMessage
            (QStringLiteral("内部线条清理窗口已坍缩，未删除任何图元。"), 6000);
        return false;
    }

    QVector<CadItem*> removableItems;
    for (CadItem* item : result.removableItems)
    {
        if (item != nullptr)
        {
            removableItems.push_back(item);
        }
    }

    if (!removableItems.isEmpty()
        && !m_editer.deleteEntities(removableItems))
    {
        const QString message = QStringLiteral("内部线条图元删除失败。");
        ui->openGLWidget->appendCommandMessage(message);
        statusBar()->showMessage(message, 6000);
        return false;
    }

    if (!removableItems.isEmpty())
    {
        cadcam::core::emitSummaryLog
        (
            QStringLiteral("InternalPathWindow"),
            QStringLiteral("DeleteResult"),
            QStringLiteral("deletedEntityCount=%1")
                .arg(removableItems.size())
        );
        invalidateProcessOrdersAfterEndCutChange();
        refreshWasteProcessingExclusions();
        updateBoundaryViewer(ui->openGLWidget, m_boundaryAssignmentPerformanceReport);
    }

    const QString message = removableItems.isEmpty()
        ? QStringLiteral("内部线条清理完成：内缩窗口内没有与其相交的图元（候选 %1，窗口外保留 %2，跳过 %3）。")
            .arg(result.candidatePathCount)
            .arg(result.outsideWindowCount)
            .arg(result.skippedPathCount)
        : QStringLiteral("内部线条清理完成：按最大圆角半径 %1 mm 加额外内缩 %2 mm 生成窗口（YZ 半宽 %3×%4），已删除 %5 个相交图元，可按 Ctrl+Z 撤销。")
            .arg(result.insetDistance, 0, 'f', 3)
            .arg(result.windowExtraInset, 0, 'f', 3)
            .arg(result.windowHalfY, 0, 'f', 3)
            .arg(result.windowHalfZ, 0, 'f', 3)
            .arg(removableItems.size());
    cadcam::core::emitSummaryLog
    (
        QStringLiteral("InternalPathWindow"),
        QStringLiteral("Status"),
        QStringLiteral("sectionAvailable=true windowCollapsed=false "
            "insetDistance=%1 windowExtraInset=%2 windowHalfY=%3 windowHalfZ=%4 "
            "candidatePathCount=%5 skippedPathCount=%6 outsideWindowCount=%7 "
            "removedEntityCount=%8")
            .arg(result.insetDistance, 0, 'f', 6)
            .arg(result.windowExtraInset, 0, 'f', 6)
            .arg(result.windowHalfY, 0, 'f', 6)
            .arg(result.windowHalfZ, 0, 'f', 6)
            .arg(result.candidatePathCount)
            .arg(result.skippedPathCount)
            .arg(result.outsideWindowCount)
            .arg(removableItems.size()));
    for (const Diagnostic& diagnostic : result.diagnostics)
    {
        if (!diagnostic.userMessage.isEmpty())
        {
            ui->openGLWidget->appendCommandMessage(diagnostic.userMessage);
        }
    }
    ui->openGLWidget->appendCommandMessage(message);
    statusBar()->showMessage(message, 6000);
    return true;
}

bool Gcode_postprocessing_system::toggleSelectedRotaryEndCutAssignment()
{
    const QVector<CadItem*> selectedItems = ui->openGLWidget->selectedEntities();
    const bool hasUnassignedItem = std::any_of
    (
        selectedItems.begin(), selectedItems.end(), [this](const CadItem* item)
        {
            return item != nullptr && m_processState.stateOrDefault(item->m_entityId)
                .overrideData.boundaryRole == cadcam::planning::BoundaryRole::None;
        }
    );
    const bool hasAssignedItem = std::any_of
    (
        selectedItems.begin(), selectedItems.end(), [this](const CadItem* item)
        {
            return item != nullptr && m_processState.stateOrDefault(item->m_entityId)
                .overrideData.boundaryRole != cadcam::planning::BoundaryRole::None;
        }
    );
    const QString operation = selectedItems.isEmpty()
        ? QStringLiteral("ToggleRotaryEndCut")
        : (!hasUnassignedItem ? QStringLiteral("ClearSelectedRotaryEndCut")
            : (hasAssignedItem ? QStringLiteral("ReassignRotaryEndCut")
                : QStringLiteral("AssignRotaryEndCut")));
    BoundaryAssignmentPerformanceOperation performance
    (
        m_boundaryAssignmentPerformanceReport,
        operation,
        static_cast<std::uint64_t>(m_document.m_entities.size()),
        static_cast<std::uint64_t>(selectedItems.size())
    );

    if (selectedItems.isEmpty())
    {
        return false;
    }

    if (!hasUnassignedItem)
    {
        return clearSelectedRotaryEndCutAssignments();
    }

    auto operationGeometry = buildRotaryBoundaryOperationGeometry(selectedItems);
    if (!operationGeometry.succeeded() || !operationGeometry.value.has_value())
    {
        QMessageBox::warning(this, QStringLiteral("加工断面指定"),
            firstDiagnosticMessage(operationGeometry.diagnostics));
        return false;
    }
    QString errorMessage;
    const QVector<CadItem*> boundaryItems = expandedSelectedRotaryEndCut
        (*operationGeometry.value, &errorMessage);

    if (boundaryItems.isEmpty())
    {
        QMessageBox::warning(this, QStringLiteral("加工断面指定"), errorMessage);
        return false;
    }

    int nextBoundaryId = 0;

    for (const std::unique_ptr<CadItem>& entity : m_document.m_entities)
    {
        if (entity != nullptr)
        {
            const auto state = m_processState.stateOrDefault(entity->m_entityId);
            if (state.overrideData.boundaryPairId >= nextBoundaryId)
                nextBoundaryId = state.overrideData.boundaryPairId + 1;
        }
    }

    {
        BoundaryPerformanceTimer stateTimer
            (m_boundaryAssignmentPerformanceReport != nullptr
                ? &m_boundaryAssignmentPerformanceReport->processStateUpdateMs : nullptr);
        m_processState.beginBatch();
        for (CadItem* item : boundaryItems)
        {
            if (item != nullptr) m_processState.setBoundary
                (item->m_entityId, cadcam::planning::BoundaryRole::Break, nextBoundaryId);
        }
        m_processState.endBatch();
    }

    invalidateProcessOrdersAfterEndCutChange();
    refreshWasteProcessingExclusions(*operationGeometry.value);
    const QString message = QStringLiteral("已指定加工断面 %1，共 %2 个图元。")
        .arg(nextBoundaryId + 1)
        .arg(boundaryItems.size());
    ui->openGLWidget->appendCommandMessage(message);
    updateBoundaryViewer(ui->openGLWidget, m_boundaryAssignmentPerformanceReport);
    statusBar()->showMessage(message, 5000);
    return true;
}

bool Gcode_postprocessing_system::recognizeAllRotaryEndCuts(bool interactive)
{
    BoundaryAssignmentPerformanceOperation performance
    (
        m_boundaryAssignmentPerformanceReport,
        QStringLiteral("RecognizeAllRotaryEndCuts"),
        static_cast<std::uint64_t>(m_document.m_entities.size()),
        static_cast<std::uint64_t>(ui->openGLWidget->selectedEntities().size())
    );
    auto operationGeometry = buildRotaryBoundaryOperationGeometry();
    if (!operationGeometry.succeeded() || !operationGeometry.value.has_value())
    {
        const QString message = firstDiagnosticMessage(operationGeometry.diagnostics);
        ui->openGLWidget->appendCommandMessage(message);
        statusBar()->showMessage(message, 5000);
        return false;
    }
    const QVector<CadItem*>& documentItems = operationGeometry.value->sceneItems;

    if (documentItems.isEmpty())
    {
        const QString message = QStringLiteral("当前文档为空，未识别到加工断面。");
        ui->openGLWidget->appendCommandMessage(message);
        statusBar()->showMessage(message, 4000);

        if (interactive)
        {
            QMessageBox::information(this, QStringLiteral("识别加工断面"), message);
        }

        return false;
    }

    cadcam::core::emitSummaryLog(QStringLiteral("断面候选"), QString(), QStringLiteral("sceneItems 过滤后：%1")
        .arg(describeRotaryPathItems(operationGeometry.value->sceneItems)));
    int nextBoundaryId = 0;

    for (CadItem* item : documentItems)
    {
        const auto state = m_processState.stateOrDefault(item->m_entityId);
        if (state.overrideData.boundaryPairId >= nextBoundaryId)
        {
            nextBoundaryId = state.overrideData.boundaryPairId + 1;
        }
    }

    int recognizedCount = 0;
    QSet<CadItem*> attemptedBoundaryItems;

    for (CadItem* seedItem : documentItems)
    {
        if (seedItem == nullptr
            || m_processState.stateOrDefault(seedItem->m_entityId)
                .overrideData.boundaryRole != cadcam::planning::BoundaryRole::None
            || attemptedBoundaryItems.contains(seedItem))
        {
            continue;
        }

        const QVector<CadItem*> seedItems{ seedItem };
        QVector<CadItem*> connectedItems;
        RotaryPathLoopResult loop;
        {
            BoundaryPerformanceTimer orderingTimer
                (m_boundaryAssignmentPerformanceReport != nullptr
                    ? &m_boundaryAssignmentPerformanceReport->boundaryOrderingMs : nullptr);
            if (m_boundaryAssignmentPerformanceReport != nullptr)
            {
                ++m_boundaryAssignmentPerformanceReport->topologyReuseCount;
            }
            loop = operationGeometry.value->topology->extractSeededLoop
                (seedItems, &connectedItems);
        }

        if (!loop.valid || loop.usedItems.isEmpty())
        {
            cadcam::core::emitSummaryLog(QStringLiteral("自动识别加工断面"), QString(), QStringLiteral("种子 %1 提取失败：%2")
                .arg(describeRotaryPathItems(seedItems))
                .arg(loop.errorMessage));
            continue;
        }

        cadcam::core::emitSummaryLog(QStringLiteral("断面候选"), QString(), QStringLiteral("选择集：%1")
            .arg(describeRotaryPathItems(seedItems)));
        cadcam::core::emitSummaryLog(QStringLiteral("断面候选"), QString(), QStringLiteral("连通扩展：%1")
            .arg(describeRotaryPathItems(connectedItems)));

        for (CadItem* item : loop.usedItems)
        {
            attemptedBoundaryItems.insert(item);
        }

        if (std::any_of(loop.usedItems.begin(), loop.usedItems.end(), [this](const CadItem* item)
        {
            return item != nullptr && m_processState.stateOrDefault(item->m_entityId)
                .overrideData.boundaryRole != cadcam::planning::BoundaryRole::None;
        }))
        {
            continue;
        }

        const RotaryCutBoundaryAnalysis analysis = analyzeBoundary
        (
            loop.usedItems,
            *operationGeometry.value->topology,
            m_rotaryTubeSectionModel,
            kEndCutConnectionTolerance,
            m_boundaryAssignmentPerformanceReport
        );

        if (!analysis.valid)
        {
            cadcam::core::emitSummaryLog(QStringLiteral("自动识别加工断面"), QString(), QStringLiteral("周向验证失败：%1")
                .arg(analysis.errorMessage));
            continue;
        }

        const QVector<CadItem*>& recognizedItems = analysis.boundaryItems.isEmpty()
            ? loop.usedItems
            : analysis.boundaryItems;

        {
            BoundaryPerformanceTimer stateTimer
                (m_boundaryAssignmentPerformanceReport != nullptr
                    ? &m_boundaryAssignmentPerformanceReport->processStateUpdateMs : nullptr);
            m_processState.beginBatch();
            for (CadItem* item : recognizedItems)
            {
                if (item != nullptr) m_processState.setBoundary
                    (item->m_entityId, cadcam::planning::BoundaryRole::Break, nextBoundaryId);
            }
            m_processState.endBatch();
        }

        ++nextBoundaryId;
        ++recognizedCount;
    }

    const QString message = QStringLiteral("所有加工断面识别完成，共识别 %1 个有效加工断面。")
        .arg(recognizedCount);

    if (recognizedCount > 0)
    {
        invalidateProcessOrdersAfterEndCutChange();
        refreshWasteProcessingExclusions(*operationGeometry.value);
    }

    ui->openGLWidget->appendCommandMessage(message);
    updateBoundaryViewer(ui->openGLWidget, m_boundaryAssignmentPerformanceReport);
    statusBar()->showMessage(message, 5000);

    if (interactive && recognizedCount == 0)
    {
        QMessageBox::information(this, QStringLiteral("识别加工断面"), QStringLiteral("未识别到有效加工断面。"));
    }

    syncMachiningSettingsState();
    return recognizedCount > 0;
}

bool Gcode_postprocessing_system::clearSelectedRotaryEndCutAssignments()
{
    const QVector<CadItem*> selectedItems = ui->openGLWidget->selectedEntities();
    BoundaryAssignmentPerformanceOperation performance
    (
        m_boundaryAssignmentPerformanceReport,
        QStringLiteral("ClearSelectedRotaryEndCut"),
        static_cast<std::uint64_t>(m_document.m_entities.size()),
        static_cast<std::uint64_t>(selectedItems.size())
    );
    QSet<int> pairIds;

    for (const CadItem* item : selectedItems)
    {
        if (item != nullptr)
        {
            const auto state = m_processState.stateOrDefault(item->m_entityId);
            if (state.overrideData.boundaryRole != cadcam::planning::BoundaryRole::None
                && state.overrideData.boundaryPairId >= 0)
                pairIds.insert(state.overrideData.boundaryPairId);
        }
    }

    if (pairIds.isEmpty())
    {
        QMessageBox::information(this, QStringLiteral("清除加工断面指定"), QStringLiteral("请先选中一个已指定的加工断面。"));
        return false;
    }

    int clearedCount = 0;

    {
        BoundaryPerformanceTimer stateTimer
            (m_boundaryAssignmentPerformanceReport != nullptr
                ? &m_boundaryAssignmentPerformanceReport->processStateUpdateMs : nullptr);
        m_processState.beginBatch();
        for (const std::unique_ptr<CadItem>& entity : m_document.m_entities)
        {
            if (entity == nullptr)
            {
                continue;
            }
            const auto state = m_processState.stateOrDefault(entity->m_entityId);
            if (pairIds.contains(state.overrideData.boundaryPairId))
            {
                m_processState.setBoundary
                    (entity->m_entityId, cadcam::planning::BoundaryRole::None, -1);
                ++clearedCount;
            }
        }
        m_processState.endBatch();
    }

    const QString message = QStringLiteral("已清除 %1 个加工断面边界，共 %2 个图元。")
        .arg(pairIds.size())
        .arg(clearedCount);
    invalidateProcessOrdersAfterEndCutChange();
    refreshWasteProcessingExclusions();
    ui->openGLWidget->appendCommandMessage(message);
    updateBoundaryViewer(ui->openGLWidget, m_boundaryAssignmentPerformanceReport);
    statusBar()->showMessage(message, 5000);
    return true;
}

bool Gcode_postprocessing_system::clearRotaryEndCutAssignments()
{
    BoundaryAssignmentPerformanceOperation performance
    (
        m_boundaryAssignmentPerformanceReport,
        QStringLiteral("ClearAllRotaryEndCuts"),
        static_cast<std::uint64_t>(m_document.m_entities.size()),
        static_cast<std::uint64_t>(ui->openGLWidget->selectedEntities().size())
    );
    int clearedCount = 0;

    {
        BoundaryPerformanceTimer stateTimer
            (m_boundaryAssignmentPerformanceReport != nullptr
                ? &m_boundaryAssignmentPerformanceReport->processStateUpdateMs : nullptr);
        m_processState.beginBatch();
        for (const std::unique_ptr<CadItem>& entity : m_document.m_entities)
        {
            if (entity == nullptr || m_processState.stateOrDefault(entity->m_entityId)
                .overrideData.boundaryRole == cadcam::planning::BoundaryRole::None)
            {
                continue;
            }
            m_processState.setBoundary
                (entity->m_entityId, cadcam::planning::BoundaryRole::None, -1);
            ++clearedCount;
        }
        m_processState.endBatch();
    }

    if (clearedCount == 0)
    {
        statusBar()->showMessage(QStringLiteral("当前没有已指定的加工断面。"), 3000);
        return false;
    }

    const QString message = QStringLiteral("已清除 %1 个图元的加工断面指定。").arg(clearedCount);
    invalidateProcessOrdersAfterEndCutChange();
    refreshWasteProcessingExclusions();
    ui->openGLWidget->appendCommandMessage(message);
    updateBoundaryViewer(ui->openGLWidget, m_boundaryAssignmentPerformanceReport);
    statusBar()->showMessage(message, 5000);
    return true;
}