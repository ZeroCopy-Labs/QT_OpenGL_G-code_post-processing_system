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


void Gcode_postprocessing_system::invalidateProcessOrdersAfterEndCutChange()
{
    invalidateCurrentProcessPlan();
}

void Gcode_postprocessing_system::invalidateCurrentProcessPlan()
{
    {
        BoundaryPerformanceTimer stateTimer
            (m_boundaryAssignmentPerformanceReport != nullptr
                ? &m_boundaryAssignmentPerformanceReport->processStateUpdateMs : nullptr);
        m_currentProcessPlan.reset();
        m_processPresentation.reset();
    }
    if (ui != nullptr && ui->openGLWidget != nullptr)
    {
        BoundaryPerformanceTimer viewerTimer
            (m_boundaryAssignmentPerformanceReport != nullptr
                ? &m_boundaryAssignmentPerformanceReport->viewerRefreshMs : nullptr);
        ui->openGLWidget->setProcessPresentation(nullptr);
        ui->openGLWidget->update();
    }
}

bool Gcode_postprocessing_system::applyProcessUnitSequenceToCurrentPlan
(
    const cadcam::planning::ProcessUnitSequence& sequence,
    QString* errorMessage
)
{
    const auto fail = [errorMessage](const QString& message)
    {
        if (errorMessage != nullptr) *errorMessage = message;
        return false;
    };

    if (!m_currentProcessPlan.has_value()
        || m_currentProcessPlan->contentRevision != m_document.contentRevision()
        || m_currentProcessPlan->processStateRevision != m_processState.revision()
        || m_currentProcessPlan->processUnitSequence.units
            != m_processState.processUnitSequence().units)
    {
        return fail(QStringLiteral("当前加工计划已失效，请重新排序后再调整加工单元。"));
    }

    cadcam::planning::ProcessUnitSequence targetSequence = sequence;
    targetSequence.revision = m_processState.processUnitSequence().revision + 1U;
    const OperationContext context = createOperationContext
        (QStringLiteral("ReorderCurrentProcessUnitSequence"));
    ProcessPlanningService service;
    auto reordered = service.reorderPlanByUnitSequence
        (*m_currentProcessPlan, targetSequence, context);
    if (!reordered.succeeded() || !reordered.value.has_value())
    {
        QString message = QStringLiteral("加工单元重排不符合当前加工约束。");
        for (const Diagnostic& diagnostic : reordered.diagnostics)
        {
            if (!diagnostic.userMessage.trimmed().isEmpty())
            {
                message = diagnostic.userMessage;
                break;
            }
        }
        return fail(message);
    }

    const std::uint64_t expectedProcessRevision = m_processState.revision() + 1U;
    cadcam::planning::ProcessPlan candidatePlan = std::move(*reordered.value);
    candidatePlan.processStateRevision = expectedProcessRevision;
    auto candidatePresentation = cadcam::process::ProcessPresentationSnapshot::build
        (candidatePlan, context);
    if (!candidatePresentation.succeeded() || !candidatePresentation.value.has_value())
    {
        return fail(QStringLiteral("加工单元重排后的显示快照构建失败。"));
    }

    if (!m_processState.setProcessUnitSequence(sequence.units))
    {
        return fail(QStringLiteral("加工单元序列未发生变化或状态更新失败。"));
    }

    candidatePlan.processUnitSequence = m_processState.processUnitSequence();
    candidatePlan.processStateRevision = m_processState.revision();
    candidatePresentation.value->processStateRevision = m_processState.revision();
    m_currentProcessPlan = std::move(candidatePlan);
    m_processPresentation = std::move(*candidatePresentation.value);
    ui->openGLWidget->setProcessPresentation(&*m_processPresentation);
    ui->openGLWidget->update();
    return true;
}

bool Gcode_postprocessing_system::applyProcessUnitTraversalToCurrentPlan
(
    const cadcam::planning::ProcessUnitKey& key,
    const cadcam::process::ProcessUnitTraversalOverride& traversal,
    const std::optional<cadcam::process::ProcessUnitTraversalOverride>& storedOverride,
    QString* errorMessage
)
{
    const auto fail = [errorMessage](const QString& message)
    {
        if (errorMessage != nullptr) *errorMessage = message;
        return false;
    };
    if (!m_currentProcessPlan.has_value()
        || m_currentProcessPlan->contentRevision != m_document.contentRevision()
        || m_currentProcessPlan->processStateRevision != m_processState.revision()
        || m_currentProcessPlan->processUnitSequence.units
            != m_processState.processUnitSequence().units)
    {
        return fail(QStringLiteral("当前加工计划已失效，请重新排序后再调整加工单元。"));
    }

    std::optional<cadcam::process::ProcessUnitTraversalOverride> currentStored;
    if (const auto* value = m_processState.findProcessUnitTraversalOverride(key))
        currentStored = *value;
    const bool stateWillChange = currentStored != storedOverride;
    const std::uint64_t expectedProcessRevision = m_processState.revision()
        + (stateWillChange ? 1U : 0U);

    const OperationContext context = createOperationContext
        (QStringLiteral("ReverseCurrentProcessUnitTraversal"));
    ProcessPlanningService service;
    auto updated = service.applyPlanUnitTraversal
        (*m_currentProcessPlan, key, traversal, context);
    if (!updated.succeeded() || !updated.value.has_value())
    {
        QString message = QStringLiteral("加工单元整组反向不符合当前加工约束。");
        for (const Diagnostic& diagnostic : updated.diagnostics)
        {
            if (!diagnostic.userMessage.trimmed().isEmpty())
            {
                message = diagnostic.userMessage;
                break;
            }
        }
        return fail(message);
    }

    cadcam::planning::ProcessPlan candidatePlan = std::move(*updated.value);
    candidatePlan.processStateRevision = expectedProcessRevision;
    auto candidatePresentation = cadcam::process::ProcessPresentationSnapshot::build
        (candidatePlan, context);
    if (!candidatePresentation.succeeded() || !candidatePresentation.value.has_value())
    {
        return fail(QStringLiteral("加工单元反向后的显示快照构建失败。"));
    }

    if (stateWillChange
        && !m_processState.setProcessUnitTraversalOverride(key, storedOverride))
    {
        return fail(QStringLiteral("加工单元遍历覆盖更新失败。"));
    }

    candidatePlan.processStateRevision = m_processState.revision();
    candidatePresentation.value->processStateRevision = m_processState.revision();
    m_currentProcessPlan = std::move(candidatePlan);
    m_processPresentation = std::move(*candidatePresentation.value);
    ui->openGLWidget->setProcessPresentation(&*m_processPresentation);
    ui->openGLWidget->update();
    return true;
}

void Gcode_postprocessing_system::handleProcessUnitReverseRequest
    (const cadcam::planning::ProcessUnitKey& unitKey)
{
    const auto announce = [this](const QString& message)
    {
        ui->openGLWidget->appendCommandMessage(message);
        ui->openGLWidget->refreshCommandPrompt();
        statusBar()->showMessage(message, 5000);
    };
    if (!m_currentProcessPlan.has_value())
    {
        announce(QStringLiteral("当前没有有效加工计划，请先排序。"));
        return;
    }

    const auto unit = std::find_if
    (
        m_currentProcessPlan->processUnits.begin(),
        m_currentProcessPlan->processUnits.end(),
        [&unitKey](const cadcam::planning::ProcessUnit& candidate)
        { return candidate.key == unitKey; }
    );
    if (unit == m_currentProcessPlan->processUnits.end())
    {
        announce(QStringLiteral("目标加工单元不属于当前计划。"));
        return;
    }

    std::map<cadcam::geometry::EntityId, const cadcam::planning::ProcessAssignment*>
        assignmentsByEntity;
    for (const cadcam::planning::ProcessAssignment& assignment
        : m_currentProcessPlan->assignments)
        assignmentsByEntity.emplace(assignment.entityId, &assignment);

    cadcam::process::ProcessUnitTraversalOverride beforeTraversal;
    beforeTraversal.members.reserve(unit->orderedMemberEntityIds.size());
    for (const cadcam::geometry::EntityId entityId : unit->orderedMemberEntityIds)
    {
        const auto assignment = assignmentsByEntity.find(entityId);
        if (assignment == assignmentsByEntity.end())
        {
            announce(QStringLiteral("加工单元成员缺少计划分配，无法整组反向。"));
            return;
        }
        beforeTraversal.members.push_back
        ({ entityId, assignment->second->reverse, assignment->second->startParameter });
    }

    cadcam::process::ProcessUnitTraversalOverride afterTraversal = beforeTraversal;
    std::reverse(afterTraversal.members.begin(), afterTraversal.members.end());
    for (auto& member : afterTraversal.members) member.reverse = !member.reverse;

    std::optional<cadcam::process::ProcessUnitTraversalOverride> beforeStored;
    if (const auto* value = m_processState.findProcessUnitTraversalOverride(unitKey))
        beforeStored = *value;
    const std::optional<cadcam::process::ProcessUnitTraversalOverride> afterStored =
        afterTraversal;

    const auto applyTraversal = [this, announce, unitKey]
    (
        const cadcam::process::ProcessUnitTraversalOverride& traversal,
        const std::optional<cadcam::process::ProcessUnitTraversalOverride>& stored
    )
    {
        QString errorMessage;
        const bool success = applyProcessUnitTraversalToCurrentPlan
            (unitKey, traversal, stored, &errorMessage);
        if (!success) announce(errorMessage);
        return success;
    };
    const bool applied = m_editer.executeUndoableAction
    (
        [applyTraversal, afterTraversal, afterStored]()
        {
            return applyTraversal(afterTraversal, afterStored);
        },
        [applyTraversal, beforeTraversal, beforeStored]()
        {
            return applyTraversal(beforeTraversal, beforeStored);
        }
    );
    if (applied)
        announce(QStringLiteral("加工单元已整组反向，起点与方向显示已刷新。"));
}

namespace
{
    QString endCutSideDisplayName(cadcam::machining::EndCutSide side)
    {
        switch (side)
        {
        case cadcam::machining::EndCutSide::Left:
            return QStringLiteral("左端面");
        case cadcam::machining::EndCutSide::Right:
            return QStringLiteral("右端面");
        case cadcam::machining::EndCutSide::Section:
        default:
            return QStringLiteral("断面");
        }
    }

    // 面板直接标出该端面实际生效的修正方向，便于与工艺规则核对。
    QString endCutSideDirectionText(cadcam::machining::EndCutSide side)
    {
        switch (side)
        {
        case cadcam::machining::EndCutSide::Left:
            return QStringLiteral("+X");
        case cadcam::machining::EndCutSide::Right:
            return QStringLiteral("−X");
        case cadcam::machining::EndCutSide::Section:
        default:
            return QStringLiteral("不修正");
        }
    }
}

QString Gcode_postprocessing_system::rotaryEndCutSideSummary() const
{
    std::map<int, std::optional<cadcam::machining::EndCutSide>> explicitSides;
    for (const std::unique_ptr<CadItem>& entity : m_document.m_entities)
    {
        if (entity == nullptr)
        {
            continue;
        }

        const auto state = m_processState.stateOrDefault(entity->m_entityId);
        if (state.overrideData.boundaryRole != cadcam::planning::BoundaryRole::Break
            || state.overrideData.boundaryPairId < 0)
        {
            continue;
        }

        explicitSides[state.overrideData.boundaryPairId] = state.overrideData.endCutSide;
    }

    if (explicitSides.empty())
    {
        return QStringLiteral("未指定加工断面");
    }

    std::map<int, cadcam::machining::EndCutSide> plannedSides;
    std::map<int, int> unitOrderByPairId;
    if (m_currentProcessPlan.has_value())
    {
        for (const cadcam::planning::EndCutSideAssignment& assignment
            : m_currentProcessPlan->endCutSides)
        {
            plannedSides.emplace(assignment.boundaryPairId, assignment.side);
        }

        for (const cadcam::planning::ProcessGroup& group : m_currentProcessPlan->groups)
        {
            if (group.kind != cadcam::planning::ProcessGroupKind::BreakBoundary
                || group.boundaryPairId < 0)
            {
                continue;
            }

            std::vector<cadcam::geometry::EntityId> groupIds = group.entityIds;
            std::sort(groupIds.begin(), groupIds.end());
            groupIds.erase(std::unique(groupIds.begin(), groupIds.end()), groupIds.end());
            for (std::size_t index = 0;
                index < m_currentProcessPlan->processUnits.size(); ++index)
            {
                if (m_currentProcessPlan->processUnits[index].key.memberEntityIds == groupIds)
                {
                    unitOrderByPairId[group.boundaryPairId] = static_cast<int>(index) + 1;
                    break;
                }
            }
        }
    }

    QStringList parts;
    for (const auto& [pairId, explicitSide] : explicitSides)
    {
        const auto planned = plannedSides.find(pairId);
        QString sideText;
        if (explicitSide.has_value())
        {
            sideText = QStringLiteral("%1（%2，手动）")
                .arg(endCutSideDisplayName(*explicitSide), endCutSideDirectionText(*explicitSide));
        }
        else if (planned != plannedSides.end())
        {
            sideText = QStringLiteral("%1（%2，自动）")
                .arg(endCutSideDisplayName(planned->second), endCutSideDirectionText(planned->second));
        }
        else
        {
            sideText = QStringLiteral("待排序");
        }

        const auto unitOrder = unitOrderByPairId.find(pairId);
        // 断面编号与画布断面标签一致，统一由 1 开始；加工单元编号另按当前序列位置给出。
        parts.push_back(unitOrder != unitOrderByPairId.end()
            ? QStringLiteral("断%1：%2，加工单元%3")
                .arg(pairId + 1).arg(sideText).arg(unitOrder->second)
            : QStringLiteral("断%1：%2").arg(pairId + 1).arg(sideText));
    }
    return parts.join(QStringLiteral("；"));
}

void Gcode_postprocessing_system::handleProcessUnitEndCutSideRequest
    (const cadcam::planning::ProcessUnitKey& unitKey, const QPoint& globalPos)
{
    const auto announce = [this](const QString& message)
    {
        ui->openGLWidget->appendCommandMessage(message);
        ui->openGLWidget->refreshCommandPrompt();
        statusBar()->showMessage(message, 5000);
    };
    if (!m_currentProcessPlan.has_value())
    {
        announce(QStringLiteral("当前没有有效加工计划，请先排序。"));
        return;
    }

    const auto unit = std::find_if
    (
        m_currentProcessPlan->processUnits.begin(),
        m_currentProcessPlan->processUnits.end(),
        [&unitKey](const cadcam::planning::ProcessUnit& candidate)
        { return candidate.key == unitKey; }
    );
    if (unit == m_currentProcessPlan->processUnits.end())
    {
        announce(QStringLiteral("目标加工单元不属于当前计划。"));
        return;
    }

    int pairId = -1;
    std::vector<cadcam::geometry::EntityId> boundaryEntityIds;
    for (const cadcam::geometry::EntityId entityId : unit->key.memberEntityIds)
    {
        const auto state = m_processState.stateOrDefault(entityId);
        if (state.overrideData.boundaryRole != cadcam::planning::BoundaryRole::Break
            || state.overrideData.boundaryPairId < 0)
        {
            announce(QStringLiteral("所选加工单元不是加工断面，无法设置端面语义。"));
            return;
        }
        if (pairId < 0)
        {
            pairId = state.overrideData.boundaryPairId;
        }
        else if (pairId != state.overrideData.boundaryPairId)
        {
            announce(QStringLiteral("所选加工单元包含多个加工断面，无法设置端面语义。"));
            return;
        }
        boundaryEntityIds.push_back(entityId);
    }

    if (boundaryEntityIds.empty()) return;

    std::optional<cadcam::machining::EndCutSide> current;
    for (const cadcam::geometry::EntityId entityId : boundaryEntityIds)
    {
        const auto state = m_processState.stateOrDefault(entityId);
        if (state.overrideData.endCutSide.has_value())
        {
            current = state.overrideData.endCutSide;
            break;
        }
    }

    QMenu menu(this);
    QAction* autoAction = menu.addAction(QStringLiteral("自动（按 X 方向顺序判定）"));
    QAction* sectionAction = menu.addAction(QStringLiteral("断面（不修正）"));
    QAction* leftAction = menu.addAction(QStringLiteral("左端面（+X 修正）"));
    QAction* rightAction = menu.addAction(QStringLiteral("右端面（−X 修正）"));
    for (QAction* action : { autoAction, sectionAction, leftAction, rightAction })
    {
        action->setCheckable(true);
    }
    autoAction->setChecked(!current.has_value());
    sectionAction->setChecked(current == cadcam::machining::EndCutSide::Section);
    leftAction->setChecked(current == cadcam::machining::EndCutSide::Left);
    rightAction->setChecked(current == cadcam::machining::EndCutSide::Right);
    menu.addSeparator();
    QAction* infoAction = menu.addAction
        (QStringLiteral("加工断面 断%1（%2 个图元）").arg(pairId + 1).arg(boundaryEntityIds.size()));
    infoAction->setEnabled(false);

    QAction* chosen = menu.exec(globalPos);
    if (chosen == nullptr || chosen == infoAction) return;

    std::optional<cadcam::machining::EndCutSide> target;
    if (chosen == sectionAction)
    {
        target = cadcam::machining::EndCutSide::Section;
    }
    else if (chosen == leftAction)
    {
        target = cadcam::machining::EndCutSide::Left;
    }
    else if (chosen == rightAction)
    {
        target = cadcam::machining::EndCutSide::Right;
    }

    std::map<cadcam::geometry::EntityId, std::optional<cadcam::machining::EndCutSide>> beforeSides;
    std::map<cadcam::geometry::EntityId, std::optional<cadcam::machining::EndCutSide>> afterSides;
    for (const cadcam::geometry::EntityId entityId : boundaryEntityIds)
    {
        beforeSides[entityId] = m_processState.stateOrDefault(entityId).overrideData.endCutSide;
        afterSides[entityId] = target;
    }

    const auto applySides = [this, announce, pairId]
    (
        const std::map<cadcam::geometry::EntityId,
            std::optional<cadcam::machining::EndCutSide>>& sides,
        bool announceResult
    )
    {
        m_processState.beginBatch();
        bool success = true;
        for (const auto& [entityId, side] : sides)
        {
            success = m_processState.setEndCutSide(entityId, side) && success;
        }
        m_processState.endBatch();
        if (!success)
        {
            announce(QStringLiteral("端面语义设置失败：请确认该加工单元仍是加工断面。"));
            return false;
        }

        invalidateProcessOrdersAfterEndCutChange();
        syncToolPanelState();
        if (announceResult)
        {
            announce(QStringLiteral("加工断面 断%1 的端面语义已更新，请重新排序后再导出。").arg(pairId + 1));
        }
        return true;
    };

    const bool applied = m_editer.executeUndoableAction
    (
        [applySides, afterSides]() { return applySides(afterSides, true); },
        [applySides, beforeSides]() { return applySides(beforeSides, false); }
    );
    if (!applied)
    {
        syncMachiningSettingsState();
    }
}

void Gcode_postprocessing_system::handleProcessUnitMoveToBackRequest
(
    const QVector<cadcam::planning::ProcessUnitKey>& selectedUnitKeys,
    const cadcam::planning::ProcessUnitKey& targetUnitKey
)
{
    const auto announce = [this](const QString& message)
    {
        ui->openGLWidget->appendCommandMessage(message);
        ui->openGLWidget->refreshCommandPrompt();
        statusBar()->showMessage(message, 5000);
    };

    if (!m_currentProcessPlan.has_value())
    {
        announce(QStringLiteral("当前没有有效加工计划，请先排序。"));
        return;
    }

    const cadcam::planning::ProcessUnitSequence before =
        m_processState.processUnitSequence();
    std::map<std::vector<cadcam::geometry::EntityId>, std::size_t> indicesByKey;
    for (std::size_t index = 0; index < before.units.size(); ++index)
    {
        indicesByKey.emplace(before.units[index].memberEntityIds, index);
    }

    std::set<std::size_t> selectedIndices;
    for (const cadcam::planning::ProcessUnitKey& key : selectedUnitKeys)
    {
        const auto found = indicesByKey.find(key.memberEntityIds);
        if (found == indicesByKey.end())
        {
            announce(QStringLiteral("选中的加工单元不属于当前权威序列。"));
            return;
        }
        selectedIndices.insert(found->second);
    }

    const auto target = indicesByKey.find(targetUnitKey.memberEntityIds);
    if (selectedIndices.empty() || target == indicesByKey.end()
        || selectedIndices.find(target->second) == selectedIndices.end())
    {
        announce(QStringLiteral("请先选中包含目标标签的连续加工单元范围。"));
        return;
    }

    const std::size_t rangeFirst = *selectedIndices.begin();
    const std::size_t rangeLast = *selectedIndices.rbegin();
    if (rangeLast - rangeFirst + 1U != selectedIndices.size())
    {
        announce(QStringLiteral("选中的加工单元编号不连续，无法执行块内移尾。"));
        return;
    }
    if (target->second == rangeLast)
    {
        announce(QStringLiteral("目标加工单元已经位于所选范围末位。"));
        return;
    }

    cadcam::planning::ProcessUnitSequence after = before;
    const cadcam::planning::ProcessUnitKey moved = after.units[target->second];
    after.units.erase(after.units.begin() + static_cast<std::ptrdiff_t>(target->second));
    after.units.insert(after.units.begin() + static_cast<std::ptrdiff_t>(rangeLast), moved);
    ++after.revision;

    const auto applySequence = [this, announce]
        (const cadcam::planning::ProcessUnitSequence& requested)
    {
        QString errorMessage;
        const bool success = applyProcessUnitSequenceToCurrentPlan
            (requested, &errorMessage);
        if (!success) announce(errorMessage);
        return success;
    };
    const bool applied = m_editer.executeUndoableAction
    (
        [applySequence, after]()
        {
            return applySequence(after);
        },
        [applySequence, before]()
        {
            return applySequence(before);
        }
    );
    if (!applied)
    {
        return;
    }

    announce(QStringLiteral("加工单元 %1 已移动到所选范围末位，编号已连续刷新。")
        .arg(static_cast<qulonglong>(target->second + 1U)));
}