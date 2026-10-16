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


int Gcode_postprocessing_system::refreshWasteProcessingExclusions()
{
    const bool hasBoundary = std::any_of
    (
        m_document.m_entities.begin(), m_document.m_entities.end(),
        [this](const std::unique_ptr<CadItem>& entity)
        {
            if (entity == nullptr) return false;
            const auto state = m_camJob.processState().stateOrDefault(entity->m_entityId);
            return state.overrideData.boundaryRole != cadcam::planning::BoundaryRole::None
                && state.overrideData.boundaryPairId >= 0;
        }
    );
    if (!hasBoundary)
    {
        syncMachiningSettingsState();
        return 0;
    }

    auto operationGeometry = buildRotaryBoundaryOperationGeometry();
    if (!operationGeometry.succeeded() || !operationGeometry.value.has_value())
    {
        syncMachiningSettingsState();
        int excludedCount = 0;
        for (const std::unique_ptr<CadItem>& entity : m_document.m_entities)
        {
            if (entity != nullptr && m_camJob.processState().stateOrDefault(entity->m_entityId)
                .overrideData.boundaryRole == cadcam::planning::BoundaryRole::Waste)
            {
                ++excludedCount;
            }
        }
        return excludedCount;
    }
    return refreshWasteProcessingExclusions(*operationGeometry.value);
}

int Gcode_postprocessing_system::refreshWasteProcessingExclusions
    (const RotaryBoundaryOperationGeometry& geometry)
{
    BoundaryPerformanceTimer wasteRefreshTimer
        (m_boundaryAssignmentPerformanceReport != nullptr
            ? &m_boundaryAssignmentPerformanceReport->wasteRefreshMs : nullptr);
    if (m_boundaryAssignmentPerformanceReport != nullptr)
    {
        ++m_boundaryAssignmentPerformanceReport->topologyReuseCount;
    }
    struct BoundaryGroup
    {
        cadcam::planning::BoundaryRole role = cadcam::planning::BoundaryRole::None;
        QVector<CadItem*> items;
    };

    std::map<int, BoundaryGroup> boundaryGroups;
    for (CadItem* entity : geometry.sceneItems)
    {
        if (entity == nullptr)
        {
            continue;
        }

        const auto state = m_camJob.processState().stateOrDefault(entity->m_entityId);

        if (state.overrideData.boundaryRole == cadcam::planning::BoundaryRole::None
            || state.overrideData.boundaryPairId < 0)
        {
            continue;
        }

        const int roleIndex = static_cast<int>(state.overrideData.boundaryRole);
        const int key = state.overrideData.boundaryPairId * 4 + roleIndex;
        BoundaryGroup& group = boundaryGroups[key];
        group.role = state.overrideData.boundaryRole;
        group.items.push_back(entity);
    }

    if (m_boundaryAssignmentPerformanceReport != nullptr)
    {
        m_boundaryAssignmentPerformanceReport->boundaryGroupCount = std::max
        (
            m_boundaryAssignmentPerformanceReport->boundaryGroupCount,
            static_cast<std::uint64_t>(boundaryGroups.size())
        );
    }

    struct BoundaryPosition
    {
        cadcam::planning::BoundaryRole role = cadcam::planning::BoundaryRole::None;
        double minimumX = std::numeric_limits<double>::max();
        RotaryCutBoundaryAnalysis analysis;
    };

    std::vector<BoundaryPosition> boundaries;
    boundaries.reserve(boundaryGroups.size());

    for (const auto& [key, group] : boundaryGroups)
    {
        Q_UNUSED(key);
        BoundaryPosition boundary;
        boundary.role = group.role;
        boundary.analysis = analyzeBoundary
        (
            group.items,
            *geometry.topology,
            m_rotaryTubeSectionModel,
            kEndCutConnectionTolerance,
            m_boundaryAssignmentPerformanceReport
        );

        if (!boundary.analysis.valid || boundary.analysis.orderedPath.isEmpty())
        {
            continue;
        }

        for (const QVector3D& point : boundary.analysis.orderedPath)
        {
            boundary.minimumX = std::min(boundary.minimumX, static_cast<double>(point.x()));
        }

        boundaries.push_back(std::move(boundary));
    }

    {
        BoundaryPerformanceTimer orderingTimer
            (m_boundaryAssignmentPerformanceReport != nullptr
                ? &m_boundaryAssignmentPerformanceReport->boundaryOrderingMs : nullptr);
        std::stable_sort(boundaries.begin(), boundaries.end(),
            [](const BoundaryPosition& left, const BoundaryPosition& right)
            {
                return left.minimumX < right.minimumX;
            });
    }

    int excludedCount = 0;
    QHash<CadItem*, const RotaryPathTopologyRecord*> topologyRecordByItem;
    for (const RotaryPathTopologyRecord& record : geometry.topology->records())
    {
        if (record.sourceItem != nullptr)
        {
            topologyRecordByItem.insert(record.sourceItem, &record);
        }
    }

    for (CadItem* entity : geometry.sceneItems)
    {
        if (entity == nullptr)
        {
            continue;
        }

        const auto state = m_camJob.processState().stateOrDefault(entity->m_entityId);
        if (state.overrideData.boundaryRole == cadcam::planning::BoundaryRole::Waste)
        {
            ++excludedCount;
            continue;
        }

        if (state.overrideData.boundaryRole != cadcam::planning::BoundaryRole::None
            || boundaries.size() < 2)
        {
            continue;
        }

        const auto topologyRecord = topologyRecordByItem.constFind(entity);
        if (topologyRecord == topologyRecordByItem.cend()
            || (*topologyRecord)->points.isEmpty())
        {
            continue;
        }
        const QVector<QVector3D>& pathPoints = (*topologyRecord)->points;
        if (m_boundaryAssignmentPerformanceReport != nullptr)
        {
            ++m_boundaryAssignmentPerformanceReport->reusedPathCount;
        }

        size_t intervalIndex = boundaries.size();
        bool crossesBoundary = false;
        if (m_boundaryAssignmentPerformanceReport != nullptr)
        {
            ++m_boundaryAssignmentPerformanceReport->classifiedEntityCount;
        }

        {
            BoundaryPerformanceTimer classificationTimer
                (m_boundaryAssignmentPerformanceReport != nullptr
                    ? &m_boundaryAssignmentPerformanceReport->pointClassificationMs : nullptr);
            for (size_t boundaryIndex = 0; boundaryIndex < boundaries.size(); ++boundaryIndex)
            {
                bool hasPointBefore = false;
                bool hasPointAfter = false;

                for (const QVector3D& point : pathPoints)
                {
                    if (m_boundaryAssignmentPerformanceReport != nullptr)
                    {
                        ++m_boundaryAssignmentPerformanceReport->classificationCallCount;
                        ++m_boundaryAssignmentPerformanceReport->samplePointCount;
                    }
                    const RotaryBoundarySide side =
                        RotaryCutBoundaryAnalyzer::classifyPointRelativeToBoundary
                        (boundaries[boundaryIndex].analysis, point,
                            kEndCutConnectionTolerance);

                    if (side == RotaryBoundarySide::Ambiguous)
                    {
                        crossesBoundary = true;
                        break;
                    }

                    hasPointBefore = hasPointBefore || side == RotaryBoundarySide::Before;
                    hasPointAfter = hasPointAfter || side == RotaryBoundarySide::After;
                }

                if (crossesBoundary || (hasPointBefore && hasPointAfter))
                {
                    crossesBoundary = true;
                    break;
                }

                if (!hasPointAfter)
                {
                    intervalIndex = boundaryIndex;
                    break;
                }
            }
        }

        if (crossesBoundary || intervalIndex == 0 || intervalIndex >= boundaries.size())
        {
            continue;
        }

        if (boundaries[intervalIndex - 1].role == cadcam::planning::BoundaryRole::Waste
            || boundaries[intervalIndex].role == cadcam::planning::BoundaryRole::Waste)
        {
            ++excludedCount;
        }
    }

    syncMachiningSettingsState();
    return excludedCount;
}