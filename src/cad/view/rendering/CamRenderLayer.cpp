// CamRenderLayer 实现文件
// CAM 可视化：加工顺序编号、方向箭头、排除淡化、旋转端面标签与预览、加工单元命中与选择扩展。
#include "platform/pch.h"

#include "cad/view/rendering/CamRenderLayer.h"

#include "application/process/DocumentProcessState.h"
#include "cad/items/CadItem.h"
#include "cad/view/CadViewerUtils.h"
#include "cad/view/rendering/CamRenderVisualUtils.h"
#include "compatibility/legacy/LegacyCadItemPathBridge.h"
#include "core/machining/CutSurfaceExpectation.h"
#include "core/machining/TubeSectionProjector.h"

#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QPen>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <utility>
#include <vector>

namespace
{
    using cadcam::geometry::Path3D;
    using cadcam::geometry::Vector3d;

    QColor withMaximumAlpha(QColor color, int maximumAlpha)
    {
        color.setAlpha(std::min(color.alpha(), maximumAlpha));
        return color;
    }

    QFont overlayLabelFont(const QFont& baseFont)
    {
        QFont labelFont = baseFont;
        labelFont.setPointSize(9);
        labelFont.setBold(true);
        return labelFont;
    }

    std::optional<Path3D> buildPrecisePreviewPath(const CadItem& item)
    {
        cadcam::geometry::SamplingPolicy policy =
            LegacyCadItemPathBridge::legacySamplingPolicy(item);
        policy.fullTurnSegments = std::max(policy.fullTurnSegments, 512);
        policy.minimumBulgeSegments = std::max(policy.minimumBulgeSegments, 16);
        policy.maximumAngularStep = 6.28318530717958647692 / 512.0;

        cadcam::geometry::PathCompileOptions options;
        const auto result = LegacyCadItemPathBridge::compile
        (
            item,
            policy,
            options,
            createOperationContext(QStringLiteral("build-cut-surface-preview"))
        );
        if (!result.succeeded() || !result.value.has_value()) return std::nullopt;
        return std::move(*result.value);
    }

    QVector<QVector3D> previewVertices(const Path3D& path)
    {
        QVector<QVector3D> vertices;
        vertices.reserve(static_cast<qsizetype>(path.vertices.size() + (path.closed ? 1U : 0U)));
        for (const auto& vertex : path.vertices)
        {
            vertices.push_back(QVector3D(static_cast<float>(vertex.position.x),
                static_cast<float>(vertex.position.y), static_cast<float>(vertex.position.z)));
        }
        if (path.closed && !vertices.isEmpty()) vertices.push_back(vertices.front());
        return vertices;
    }

    QVector3D buildPerpendicularDirection(const QVector3D& direction)
    {
        QVector3D normalizedDirection = direction;

        if (normalizedDirection.lengthSquared() <= 1.0e-6f)
        {
            return QVector3D(1.0f, 0.0f, 0.0f);
        }

        normalizedDirection.normalize();

        const QVector3D axis = std::abs(normalizedDirection.z()) < 0.9f
            ? QVector3D(0.0f, 0.0f, 1.0f)
            : QVector3D(1.0f, 0.0f, 0.0f);
        QVector3D perpendicular = QVector3D::crossProduct(normalizedDirection, axis);

        if (perpendicular.lengthSquared() <= 1.0e-6f)
        {
            perpendicular = QVector3D::crossProduct(normalizedDirection, QVector3D(0.0f, 1.0f, 0.0f));
        }

        if (perpendicular.lengthSquared() <= 1.0e-6f)
        {
            perpendicular = QVector3D(1.0f, 0.0f, 0.0f);
        }
        else
        {
            perpendicular.normalize();
        }

        return perpendicular;
    }
}

CamRenderLayer::CamRenderLayer(QObject* parent)
    : QObject(parent)
{
}

const std::vector<std::unique_ptr<CadItem>>& CamRenderLayer::entities() const
{
    // 未绑定文档时使用空容器，保证可视化查询始终安全。
    static const std::vector<std::unique_ptr<CadItem>> kEmptyEntities;
    return m_entities != nullptr ? *m_entities : kEmptyEntities;
}

void CamRenderLayer::setSceneEntities(const std::vector<std::unique_ptr<CadItem>>* entities)
{
    m_entities = entities;
    m_previewPathsDirty = true;
}

void CamRenderLayer::setProcessState(const cadcam::process::DocumentProcessState* processState)
{
    m_processState = processState;
}

void CamRenderLayer::setPresentation(cadcam::process::ProcessPresentationSnapshot presentation)
{
    m_presentation = std::move(presentation);
    m_hoveredProcessUnitKey.reset();
}

void CamRenderLayer::clearPresentation()
{
    m_presentation.reset();
    m_hoveredProcessUnitKey.reset();
    m_rotaryEndCutLaserWallImpactPreviewPaths.clear();
    m_rotaryEndCutExpectedSurfacePreviewPaths.clear();
}

bool CamRenderLayer::hasPresentation() const
{
    return m_presentation.has_value();
}

void CamRenderLayer::setVisualsVisible(bool visible)
{
    m_visualsVisible = visible;
    m_hoveredProcessUnitKey.reset();
}

void CamRenderLayer::setDirectionVisible(bool visible)
{
    m_directionVisible = visible;
}

void CamRenderLayer::setOrderVisible(bool visible)
{
    m_orderVisible = visible;
    m_hoveredProcessUnitKey.reset();
}

void CamRenderLayer::setRotaryEndCutsVisible(bool visible)
{
    m_rotaryEndCutsVisible = visible;
}

void CamRenderLayer::setRotaryLaserWallImpactVisible(bool visible)
{
    m_rotaryLaserWallImpactVisible = visible;
}

void CamRenderLayer::setRotaryExpectedSurfaceVisible(bool visible)
{
    m_rotaryExpectedSurfaceVisible = visible;
}

void CamRenderLayer::setExcludedEntitiesDimmed(bool dimmed)
{
    m_excludedEntitiesDimmed = dimmed;
}

void CamRenderLayer::setRotaryEndCutPreview
(
    const std::optional<cadcam::machining::TubeSectionModel>& section,
    double wallThickness,
    const cadcam::machine::RotaryMachinePolicy& machinePolicy
)
{
    m_rotaryEndCutPreviewSection = section;
    m_rotaryTubeWallThickness =
        std::isfinite(wallThickness) && wallThickness > 0.0
            ? wallThickness : 0.0;
    m_rotaryEndCutPreviewMachinePolicy = machinePolicy;
    m_previewPathsDirty = true;
}

void CamRenderLayer::clearHover()
{
    m_hoveredProcessUnitKey.reset();
}

CadRenderOverrideMap CamRenderLayer::buildRenderOverrides(const ViewportRenderContext& context) const
{
    CadRenderOverrideMap overrides;

    if (!m_visualsVisible || !m_excludedEntitiesDimmed || !m_presentation.has_value())
    {
        return overrides;
    }

    std::vector<const CadItem*> excludedItems;
    for (const std::unique_ptr<CadItem>& entity : entities())
    {
        if (entity == nullptr) continue;
        if (resolveCamProcessExclusionVisual(entity.get(), &*m_presentation)
            != CamProcessExclusionVisual::None)
        {
            excludedItems.push_back(entity.get());
        }
    }

    if (excludedItems.empty())
    {
        return overrides;
    }

    CadRenderOverride override;
    override.hasColor = true;
    override.color = context.theme.dark
        ? QVector3D(0.58f, 0.48f, 0.34f)
        : QVector3D(0.48f, 0.39f, 0.28f);
    override.opacity = 0.42f;

    for (const CadItem* item : excludedItems)
    {
        const RenderEntityKey renderKey = CadViewerUtils::toRenderEntityKey(item);
        if (renderKey.valid())
        {
            overrides.emplace(renderKey, override);
        }
    }

    return overrides;
}

std::vector<TransientPrimitive> CamRenderLayer::buildOverlayPrimitives(const ViewportRenderContext& context)
{
    if (!m_visualsVisible || !context.interactionIdle)
    {
        return {};
    }

    refreshPreviewPaths(context);

    std::vector<TransientPrimitive> primitives = buildRotaryEndCutPrimitives(context);
    const std::vector<TransientPrimitive> directionPrimitives = buildProcessArrowPrimitives(context);
    primitives.insert(primitives.end(), directionPrimitives.begin(), directionPrimitives.end());
    return primitives;
}

void CamRenderLayer::paintScreenOverlays(QPainter& painter, const ViewportRenderContext& context)
{
    if (!m_visualsVisible || !context.interactionIdle)
    {
        return;
    }

    refreshPreviewPaths(context);
    renderProcessOrderLabels(painter, context);
    renderRotaryEndCutLabels(painter, context);
}

void CamRenderLayer::updateHover(const ViewportRenderContext& context, const QPoint& screenPos)
{
    ProcessOrderLabelOverlay hoveredLabel;
    if (hitTestProcessOrderLabel(context, screenPos, &hoveredLabel))
    {
        m_hoveredProcessUnitKey = std::move(hoveredLabel.unitKey);
    }
    else
    {
        m_hoveredProcessUnitKey.reset();
    }
}

bool CamRenderLayer::handleClick
(
    const ViewportRenderContext& context,
    const QPoint& screenPos,
    Qt::KeyboardModifiers modifiers
)
{
    ProcessOrderLabelOverlay clickedLabel;

    if (!hitTestProcessOrderLabel(context, screenPos, &clickedLabel))
    {
        return false;
    }

    if ((modifiers & Qt::ControlModifier) != 0)
    {
        if (!m_presentation.has_value())
        {
            return false;
        }

        std::set<cadcam::geometry::EntityId> selectedEntityIds;
        for (const std::unique_ptr<CadItem>& entity : entities())
        {
            if (entity != nullptr && entity->m_isSelected && entity->m_entityId != 0)
            {
                selectedEntityIds.insert(entity->m_entityId);
            }
        }

        QVector<cadcam::planning::ProcessUnitKey> selectedUnitKeys;
        for (const cadcam::process::ProcessUnitPresentation& unit
            : m_presentation->processUnits)
        {
            const bool selected = std::any_of
            (
                unit.key.memberEntityIds.cbegin(),
                unit.key.memberEntityIds.cend(),
                [&selectedEntityIds](cadcam::geometry::EntityId entityId)
                {
                    return selectedEntityIds.find(entityId) != selectedEntityIds.end();
                }
            );
            if (selected)
            {
                selectedUnitKeys.push_back(unit.key);
            }
        }

        emit processUnitMoveToBackRequested(selectedUnitKeys, clickedLabel.unitKey);
        return true;
    }

    const cadcam::process::ProcessUnitPresentation* clickedUnit = nullptr;
    if (m_presentation.has_value())
    {
        const auto clickedUnitIterator = std::find_if
        (
            m_presentation->processUnits.cbegin(),
            m_presentation->processUnits.cend(),
            [&clickedLabel](const cadcam::process::ProcessUnitPresentation& unit)
            {
                return unit.key == clickedLabel.unitKey;
            }
        );
        if (clickedUnitIterator != m_presentation->processUnits.cend())
        {
            clickedUnit = &*clickedUnitIterator;
        }
    }

    std::map<cadcam::geometry::EntityId, RenderEntityKey> renderKeysByEntityId;
    for (const std::unique_ptr<CadItem>& entity : entities())
    {
        if (entity != nullptr && entity->m_entityId != 0)
        {
            renderKeysByEntityId.emplace
            (
                entity->m_entityId,
                CadViewerUtils::toRenderEntityKey(entity.get())
            );
        }
    }

    const std::vector<cadcam::geometry::EntityId>& orderedMemberEntityIds =
        clickedUnit != nullptr
        ? clickedUnit->orderedMemberEntityIds
        : clickedLabel.unitKey.memberEntityIds;
    QSet<RenderEntityKey> unitRenderKeys;
    RenderEntityKey preferredRenderKey;
    for (cadcam::geometry::EntityId entityId : orderedMemberEntityIds)
    {
        const auto renderKeyIterator = renderKeysByEntityId.find(entityId);
        if (renderKeyIterator == renderKeysByEntityId.end())
        {
            continue;
        }

        const RenderEntityKey renderKey = renderKeyIterator->second;
        unitRenderKeys.insert(renderKey);
        if (!preferredRenderKey.valid())
        {
            preferredRenderKey = renderKey;
        }
    }

    if (unitRenderKeys.isEmpty())
    {
        return false;
    }

    if ((modifiers & Qt::ShiftModifier) != 0)
    {
        QSet<RenderEntityKey> currentSelectionKeys;
        for (const std::unique_ptr<CadItem>& entity : entities())
        {
            if (entity != nullptr && entity->m_isSelected)
            {
                currentSelectionKeys.insert(CadViewerUtils::toRenderEntityKey(entity.get()));
            }
        }

        const bool allSelected = std::all_of
        (
            unitRenderKeys.cbegin(),
            unitRenderKeys.cend(),
            [&currentSelectionKeys](RenderEntityKey renderKey)
            {
                return currentSelectionKeys.contains(renderKey);
            }
        );
        QSet<RenderEntityKey> updatedRenderKeys = currentSelectionKeys;
        RenderEntityKey updatedPrimaryRenderKey = context.primarySelectionKey;

        if (allSelected)
        {
            for (RenderEntityKey renderKey : unitRenderKeys)
            {
                updatedRenderKeys.remove(renderKey);
            }

            if (updatedPrimaryRenderKey.valid()
                && !updatedRenderKeys.contains(updatedPrimaryRenderKey))
            {
                updatedPrimaryRenderKey = {};
            }
        }
        else
        {
            for (RenderEntityKey renderKey : unitRenderKeys)
            {
                updatedRenderKeys.insert(renderKey);
            }
            updatedPrimaryRenderKey = preferredRenderKey;
        }

        emit selectionRequested(updatedRenderKeys, updatedPrimaryRenderKey, false);

        if (allSelected)
        {
            emit commandMessageRequested(QStringLiteral("已取消选择加工单元 %1。")
                .arg(clickedLabel.unitOrder + 1));
        }
        else
        {
            std::set<std::vector<cadcam::geometry::EntityId>> selectedUnitKeys;
            if (m_presentation.has_value())
            {
                for (const cadcam::process::ProcessUnitPresentation& unit
                    : m_presentation->processUnits)
                {
                    const bool selected = std::any_of
                    (
                        unit.orderedMemberEntityIds.cbegin(),
                        unit.orderedMemberEntityIds.cend(),
                        [&renderKeysByEntityId, &updatedRenderKeys]
                        (cadcam::geometry::EntityId entityId)
                        {
                            const auto found = renderKeysByEntityId.find(entityId);
                            return found != renderKeysByEntityId.end()
                                && updatedRenderKeys.contains(found->second);
                        }
                    );
                    if (selected)
                    {
                        selectedUnitKeys.insert(unit.key.memberEntityIds);
                    }
                }
            }

            emit commandMessageRequested
            (
                QStringLiteral("已追加选择加工单元 %1，共 %2 个加工单元。")
                    .arg(clickedLabel.unitOrder + 1)
                    .arg(selectedUnitKeys.size())
            );
        }
        return true;
    }

    emit selectionRequested(unitRenderKeys, preferredRenderKey, false);
    emit commandMessageRequested(QStringLiteral("已选中加工单元 %1，共 %2 个图元。")
        .arg(clickedLabel.unitOrder + 1)
        .arg(unitRenderKeys.size()));
    return true;
}

bool CamRenderLayer::handleDoubleClick(const ViewportRenderContext& context, const QPoint& screenPos)
{
    ProcessOrderLabelOverlay clickedLabel;

    if (hitTestProcessOrderLabel(context, screenPos, &clickedLabel))
    {
        emit processUnitReverseRequested(clickedLabel.unitKey);
        return true;
    }

    ProcessUnitArrowOverlay clickedArrow;
    if (hitTestProcessUnitArrow(context, screenPos, &clickedArrow))
    {
        emit processUnitReverseRequested(clickedArrow.unitKey);
        return true;
    }
    return false;
}

bool CamRenderLayer::handleContextMenuPress
(
    const ViewportRenderContext& context,
    const QPoint& screenPos,
    const QPoint& globalPos
)
{
    ProcessOrderLabelOverlay clickedLabel;
    if (hitTestProcessOrderLabel(context, screenPos, &clickedLabel))
    {
        handleClick(context, screenPos, Qt::NoModifier);
        emit processUnitContextMenuRequested(clickedLabel.unitKey, globalPos);
        return true;
    }

    ProcessUnitArrowOverlay clickedArrow;
    if (hitTestProcessUnitArrow(context, screenPos, &clickedArrow))
    {
        emit processUnitContextMenuRequested(clickedArrow.unitKey, globalPos);
        return true;
    }
    return false;
}

CamRenderLayer::SelectionExpansion CamRenderLayer::expandRenderKeysToProcessUnits
(
    const ViewportRenderContext& context,
    const QSet<RenderEntityKey>& renderKeys
) const
{
    Q_UNUSED(context);

    SelectionExpansion expansion;
    expansion.expandedRenderKeys = renderKeys;
    expansion.unassignedRenderKeys = renderKeys;

    if (!m_presentation.has_value() || renderKeys.isEmpty())
    {
        return expansion;
    }

    QHash<cadcam::geometry::EntityId, RenderEntityKey> renderKeyByEntityId;
    renderKeyByEntityId.reserve(static_cast<qsizetype>(entities().size()));

    for (const std::unique_ptr<CadItem>& entity : entities())
    {
        if (entity == nullptr || entity->m_entityId == 0)
        {
            continue;
        }

        const RenderEntityKey renderKey = CadViewerUtils::toRenderEntityKey(entity.get());

        if (renderKey.valid())
        {
            renderKeyByEntityId.insert(entity->m_entityId, renderKey);
        }
    }

    for (const cadcam::process::ProcessUnitPresentation& processUnit
        : m_presentation->processUnits)
    {
        bool unitHit = false;

        for (cadcam::geometry::EntityId entityId : processUnit.orderedMemberEntityIds)
        {
            const auto iterator = renderKeyByEntityId.constFind(entityId);

            if (iterator != renderKeyByEntityId.cend() && renderKeys.contains(iterator.value()))
            {
                unitHit = true;
                break;
            }
        }

        if (!unitHit)
        {
            continue;
        }

        QSet<RenderEntityKey> unitRenderKeys;
        unitRenderKeys.reserve
        (
            static_cast<qsizetype>(processUnit.orderedMemberEntityIds.size())
        );

        for (cadcam::geometry::EntityId entityId : processUnit.orderedMemberEntityIds)
        {
            const auto iterator = renderKeyByEntityId.constFind(entityId);

            if (iterator == renderKeyByEntityId.cend())
            {
                continue;
            }

            unitRenderKeys.insert(iterator.value());
            expansion.expandedRenderKeys.insert(iterator.value());
            expansion.unassignedRenderKeys.remove(iterator.value());
        }

        if (!unitRenderKeys.isEmpty())
        {
            expansion.processUnitRenderKeyGroups.push_back(std::move(unitRenderKeys));
        }
    }

    return expansion;
}

void CamRenderLayer::refreshPreviewPaths(const ViewportRenderContext& context)
{
    if (!m_previewPathsDirty)
    {
        return;
    }

    m_previewPathsDirty = false;
    rebuildRotaryEndCutPreviewPaths(context);
}

void CamRenderLayer::rebuildRotaryEndCutPreviewPaths(const ViewportRenderContext& context)
{
    Q_UNUSED(context);

    m_rotaryEndCutLaserWallImpactPreviewPaths.clear();
    m_rotaryEndCutExpectedSurfacePreviewPaths.clear();
    if (m_processState == nullptr
        || !m_rotaryEndCutPreviewSection.has_value()
        || !std::isfinite(m_rotaryTubeWallThickness)
        || m_rotaryTubeWallThickness <= 0.0)
    {
        emit rotaryEndCutPreviewStatusChanged(QStringLiteral("需有效方管截面、加工断面和大于 0 的方管厚度。"));
        return;
    }

    if (!cadcam::machining::TubeSectionProjector::projectToInnerSurface
        (*m_rotaryEndCutPreviewSection, m_rotaryTubeWallThickness, { 0.0, 0.0 }).has_value())
    {
        emit rotaryEndCutPreviewStatusChanged(QStringLiteral(
            "无法建立内截面：方管厚度必须小于 Y 长、Z 宽的一半，且截面几何必须有效。"));
        return;
    }

    struct BoundaryPreview
    {
        std::vector<Path3D> sources;
        std::vector<Path3D> endpoints;
        bool complete = true;
    };
    std::map<int, BoundaryPreview> boundaries;
    for (const std::unique_ptr<CadItem>& entity : entities())
    {
        if (entity == nullptr) continue;
        const auto state = m_processState->stateOrDefault(entity->m_entityId);
        if (state.overrideData.boundaryRole != cadcam::planning::BoundaryRole::Break
            || state.overrideData.boundaryPairId < 0)
        {
            continue;
        }

        auto& boundary = boundaries[state.overrideData.boundaryPairId];
        const std::optional<Path3D> path = buildPrecisePreviewPath(*entity);
        if (!path.has_value() || path->vertices.size() < 2U)
        {
            boundary.complete = false;
            continue;
        }

        const auto solved = cadcam::machine::RotaryKinematics::transform
        (
            *path,
            m_rotaryEndCutPreviewMachinePolicy,
            m_rotaryEndCutPreviewSection,
            std::nullopt,
            createOperationContext(QStringLiteral("solve-laser-wall-impact-preview"))
        );
        if (!solved.succeeded() || !solved.value.has_value()
            || solved.value->poses.size() != path->vertices.size())
        {
            boundary.complete = false;
            continue;
        }

        Path3D endpointPath = *path;
        std::size_t index = 0;
        bool mapped = true;
        for (const cadcam::machine::MachinePose4D& pose : solved.value->poses)
        {
            const auto toolRay = cadcam::machine::RotaryKinematics::sourceToolRay
                (pose, m_rotaryEndCutPreviewMachinePolicy);
            if (!toolRay.has_value())
            {
                mapped = false;
                break;
            }
            Vector3d endpoint
            {
                toolRay->origin.x
                    + toolRay->unitDirection.x * m_rotaryTubeWallThickness,
                toolRay->origin.y
                    + toolRay->unitDirection.y * m_rotaryTubeWallThickness,
                toolRay->origin.z
                    + toolRay->unitDirection.z * m_rotaryTubeWallThickness
            };
            // 两条走线共用内轮廓上的 YZ 落点，避免厚壁圆角的反向弯曲。
            const auto innerPoint = cadcam::machining::TubeSectionProjector::projectToInnerSurface
                (*m_rotaryEndCutPreviewSection, m_rotaryTubeWallThickness, { endpoint.y, endpoint.z });
            if (!innerPoint.has_value())
            {
                mapped = false;
                break;
            }
            endpoint.y = innerPoint->x;
            endpoint.z = innerPoint->y;
            endpointPath.vertices[index++].position = endpoint;
        }

        if (mapped)
        {
            m_rotaryEndCutLaserWallImpactPreviewPaths.push_back
                (previewVertices(cadcam::machining::TubeSectionProjector::connectInnerCorners
                    (*m_rotaryEndCutPreviewSection, m_rotaryTubeWallThickness, endpointPath)));
            boundary.sources.push_back(*path);
            boundary.endpoints.push_back(std::move(endpointPath));
        }
        else boundary.complete = false;
    }

    QStringList messages;
    int expectedCount = 0;
    for (const auto& entry : boundaries)
    {
        const auto& boundary = entry.second;
        if (!boundary.complete)
        {
            messages.push_back(QStringLiteral("断面 断%1：路径采样或刀头姿态解算失败。")
                .arg(entry.first + 1));
            continue;
        }
        const auto expected = cadcam::machining::CutSurfaceExpectation::build
            (*m_rotaryEndCutPreviewSection, boundary.sources, boundary.endpoints);
        if (!expected.errorMessage.isEmpty())
        {
            messages.push_back(QStringLiteral("断面 断%1：%2").arg(entry.first + 1).arg(expected.errorMessage));
            continue;
        }
        for (const auto& path : expected.paths)
            m_rotaryEndCutExpectedSurfacePreviewPaths.push_back(previewVertices(
                cadcam::machining::TubeSectionProjector::connectInnerCorners
                    (*m_rotaryEndCutPreviewSection, m_rotaryTubeWallThickness, path, expected.xSlopes)));
        ++expectedCount;
    }
    messages.prepend(QStringLiteral("期望走线：%1 / %2 个断面可用。")
        .arg(expectedCount).arg(boundaries.size()));
    emit rotaryEndCutPreviewStatusChanged(messages.join(QLatin1Char('\n')));
}

std::vector<CamRenderLayer::ProcessOrderLabelOverlay>
CamRenderLayer::buildProcessOrderLabelOverlays(const ViewportRenderContext& context) const
{
    // Compatibility projection of the current ProcessPlan. Not a planning or NC source of truth.
    if (!m_visualsVisible || !m_orderVisible || !context.interactionIdle || !m_presentation.has_value())
    {
        return {};
    }

    std::map<cadcam::geometry::EntityId, CadItem*> entitiesById;
    for (const std::unique_ptr<CadItem>& entity : entities())
    {
        if (entity != nullptr && entity->m_entityId != 0)
        {
            entitiesById.emplace(entity->m_entityId, entity.get());
        }
    }

    std::vector<ProcessOrderLabelOverlay> labels;
    labels.reserve(m_presentation->processUnits.size());

    const QFontMetrics metrics(overlayLabelFont(context.overlayFont));
    std::vector<QRect> occupiedRects;
    occupiedRects.reserve(m_presentation->processUnits.size());

    for (const cadcam::process::ProcessUnitPresentation& unit
        : m_presentation->processUnits)
    {
        const auto anchorIterator = entitiesById.find(unit.anchorEntityId);
        if (unit.unitOrder < 0
            || unit.orderedMemberEntityIds.empty()
            || anchorIterator == entitiesById.end())
        {
            continue;
        }

        CadItem* anchorEntity = anchorIterator->second;
        const auto* presentation = m_presentation->find(unit.anchorEntityId);
        if (resolveCamProcessExclusionVisual(anchorEntity, &*m_presentation)
            != CamProcessExclusionVisual::None)
        {
            continue;
        }
        const CamProcessVisualInfo info = buildCamProcessVisualInfo(anchorEntity, presentation);

        if (!info.valid)
        {
            continue;
        }

        const QPoint screenPoint = context.worldToScreen(info.labelAnchor);

        if (screenPoint.x() < -24 || screenPoint.x() > context.viewportWidth + 24
            || screenPoint.y() < -24 || screenPoint.y() > context.viewportHeight + 24)
        {
            continue;
        }

        ProcessOrderLabelOverlay label;
        label.unitKey = unit.key;
        label.unitOrder = unit.unitOrder;
        label.hovered = m_hoveredProcessUnitKey.has_value()
            && *m_hoveredProcessUnitKey == unit.key;
        label.selected = std::any_of
        (
            unit.orderedMemberEntityIds.cbegin(),
            unit.orderedMemberEntityIds.cend(),
            [&entitiesById](cadcam::geometry::EntityId entityId)
            {
                const auto found = entitiesById.find(entityId);
                return found != entitiesById.end() && found->second->m_isSelected;
            }
        );
        label.center = screenPoint;
        label.text = QString::number(unit.unitOrder + 1);

        const QRect textRect = metrics.boundingRect(label.text);
        label.bubbleRect = QRect
        (
            label.center.x() - textRect.width() / 2 - 7,
            label.center.y() - textRect.height() / 2 - 4,
            textRect.width() + 14,
            textRect.height() + 8
        );

        if (!label.selected)
        {
            bool overlaps = false;

            for (const QRect& occupiedRect : occupiedRects)
            {
                if (label.bubbleRect.adjusted(-2, -2, 2, 2).intersects(occupiedRect))
                {
                    overlaps = true;
                    break;
                }
            }

            if (overlaps)
            {
                continue;
            }
        }

        occupiedRects.push_back(label.bubbleRect);
        labels.push_back(std::move(label));
    }

    std::sort
    (
        labels.begin(),
        labels.end(),
        [](const ProcessOrderLabelOverlay& left, const ProcessOrderLabelOverlay& right)
        {
            if (left.selected != right.selected)
            {
                return left.selected && !right.selected;
            }

            return left.unitOrder < right.unitOrder;
        }
    );

    return labels;
}

std::vector<CamRenderLayer::ProcessUnitArrowOverlay>
CamRenderLayer::buildProcessUnitArrowOverlays(const ViewportRenderContext& context) const
{
    if (!m_visualsVisible || !m_directionVisible || !m_presentation.has_value()) return {};

    std::map<cadcam::geometry::EntityId, CadItem*> itemsById;
    std::set<cadcam::geometry::EntityId> selectedIds;
    for (const std::unique_ptr<CadItem>& entity : entities())
    {
        if (entity == nullptr || entity->m_entityId == 0U) continue;
        itemsById.emplace(entity->m_entityId, entity.get());
        if (entity->m_isSelected) selectedIds.insert(entity->m_entityId);
    }

    const float pixelScale = std::max(context.pixelToWorldScale, 1.0e-4f);
    const float headLength = pixelScale * 13.0f;
    std::vector<ProcessUnitArrowOverlay> overlays;
    overlays.reserve(m_presentation->processUnits.size());
    for (const cadcam::process::ProcessUnitPresentation& unit
        : m_presentation->processUnits)
    {
        const auto item = itemsById.find(unit.anchorEntityId);
        if (item == itemsById.end()) continue;

        cadcam::process::ProcessPresentationEntry anchorPresentation;
        anchorPresentation.entityId = unit.anchorEntityId;
        anchorPresentation.processOrder = unit.unitOrder;
        anchorPresentation.reverse = unit.anchorReverse;
        anchorPresentation.startParameter = unit.anchorStartParameter;
        const CamProcessVisualInfo info = buildCamProcessVisualInfo
            (item->second, &anchorPresentation);
        if (!info.valid || info.direction.lengthSquared() <= 1.0e-6f) continue;

        const QPoint tipScreen = context.worldToScreen(info.startPoint);
        const QPoint baseScreen = context.worldToScreen
            (info.startPoint - info.direction * headLength);
        if (std::hypot
            (
                static_cast<double>(tipScreen.x() - baseScreen.x()),
                static_cast<double>(tipScreen.y() - baseScreen.y())
            ) < 3.0) continue;

        const bool selected = std::any_of
        (
            unit.key.memberEntityIds.begin(), unit.key.memberEntityIds.end(),
            [&selectedIds](cadcam::geometry::EntityId id)
            { return selectedIds.find(id) != selectedIds.end(); }
        );
        overlays.push_back
        ({
            unit.key,
            unit.unitOrder,
            unit.anchorReverse,
            selected,
            info.startPoint,
            info.direction,
            QRect(tipScreen, baseScreen).normalized().adjusted(-7, -7, 7, 7)
        });
    }
    return overlays;
}

bool CamRenderLayer::hitTestProcessOrderLabel
(
    const ViewportRenderContext& context,
    const QPoint& screenPos,
    ProcessOrderLabelOverlay* outLabel
) const
{
    const std::vector<ProcessOrderLabelOverlay> labels = buildProcessOrderLabelOverlays(context);

    for (const ProcessOrderLabelOverlay& label : labels)
    {
        if (label.bubbleRect.adjusted(-2, -2, 2, 2).contains(screenPos))
        {
            if (outLabel != nullptr)
            {
                *outLabel = label;
            }

            return true;
        }
    }

    return false;
}

bool CamRenderLayer::hitTestProcessUnitArrow
(
    const ViewportRenderContext& context,
    const QPoint& screenPos,
    ProcessUnitArrowOverlay* outArrow
) const
{
    const std::vector<ProcessUnitArrowOverlay> arrows = buildProcessUnitArrowOverlays(context);
    for (const ProcessUnitArrowOverlay& arrow : arrows)
    {
        if (!arrow.hitRect.contains(screenPos)) continue;
        if (outArrow != nullptr) *outArrow = arrow;
        return true;
    }
    return false;
}

void CamRenderLayer::renderProcessOrderLabels(QPainter& painter, const ViewportRenderContext& context) const
{
    const std::vector<ProcessOrderLabelOverlay> labels = buildProcessOrderLabelOverlays(context);

    if (labels.empty())
    {
        return;
    }

    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setFont(overlayLabelFont(context.overlayFont));

    for (const ProcessOrderLabelOverlay& label : labels)
    {
        const bool emphasized = label.hovered || label.selected;
        const QColor baseFillColor = emphasized
            ? context.theme.selectedProcessLabelFillColor : context.theme.processLabelFillColor;
        const QColor baseBorderColor = emphasized
            ? context.theme.selectedProcessLabelBorderColor : context.theme.processLabelBorderColor;
        const QColor textColor = emphasized
            ? context.theme.selectedProcessLabelTextColor : context.theme.processLabelTextColor;
        const QColor fillColor = withMaximumAlpha(baseFillColor, emphasized ? 148 : 72);
        const QColor borderColor = withMaximumAlpha(baseBorderColor, emphasized ? 220 : 164);

        painter.setPen(QPen(borderColor, emphasized ? 1.4 : 1.0));
        painter.setBrush(fillColor);
        painter.drawRoundedRect(label.bubbleRect, 6.0, 6.0);
        painter.setPen(textColor);
        painter.drawText(label.bubbleRect, Qt::AlignCenter, label.text);
    }
}

void CamRenderLayer::renderRotaryEndCutLabels(QPainter& painter, const ViewportRenderContext& context) const
{
    if (!m_visualsVisible || !m_rotaryEndCutsVisible || m_processState == nullptr)
    {
        return;
    }

    struct CutLabelData
    {
        QVector3D pointSum;
        int pointCount = 0;
        int pairId = -1;
        cadcam::planning::BoundaryRole role = cadcam::planning::BoundaryRole::None;
    };

    std::map<int, CutLabelData> labels;

    for (const std::unique_ptr<CadItem>& entity : entities())
    {
        if (entity == nullptr)
        {
            continue;
        }

        const auto state = m_processState->stateOrDefault(entity->m_entityId);
        if (state.overrideData.boundaryPairId < 0
            || state.overrideData.boundaryRole == cadcam::planning::BoundaryRole::None) continue;

        const int key = state.overrideData.boundaryPairId * 4
            + static_cast<int>(state.overrideData.boundaryRole);
        CutLabelData& label = labels[key];
        label.pairId = state.overrideData.boundaryPairId;
        label.role = state.overrideData.boundaryRole;

        for (const QVector3D& point : entity->m_geometry.vertices)
        {
            label.pointSum += point;
            ++label.pointCount;
        }
    }

    if (labels.empty())
    {
        return;
    }

    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    const QFont labelFont = overlayLabelFont(context.overlayFont);
    painter.setFont(labelFont);
    const QFontMetrics metrics(labelFont);

    for (const auto& [key, label] : labels)
    {
        Q_UNUSED(key);

        if (label.pointCount <= 0)
        {
            continue;
        }

        const QPoint center = context.worldToScreen
            (label.pointSum / static_cast<float>(label.pointCount));

        if (center.x() < -30 || center.x() > context.viewportWidth + 30
            || center.y() < -30 || center.y() > context.viewportHeight + 30)
        {
            continue;
        }

        const QString roleText = label.role == cadcam::planning::BoundaryRole::Waste
            ? QStringLiteral("W")
            : QStringLiteral("断");
        const QString text = QStringLiteral("%1%2")
            .arg(roleText)
            .arg(label.pairId + 1);
        const QRect textRect = metrics.boundingRect(text);
        const QRect bubbleRect
        (
            center.x() - textRect.width() / 2 - 8,
            center.y() - textRect.height() / 2 - 5,
            textRect.width() + 16,
            textRect.height() + 10
        );
        const QColor accent = label.role == cadcam::planning::BoundaryRole::Waste
            ? QColor(242, 151, 36)
            : QColor(46, 166, 242);
        const QColor fill = withMaximumAlpha(context.theme.viewerBackgroundColor, 66);
        const QColor border = withMaximumAlpha(accent, 168);
        const QColor textColor = withMaximumAlpha(accent.lighter(125), 224);
        painter.setPen(QPen(border, 1.4));
        painter.setBrush(fill);
        painter.drawRoundedRect(bubbleRect, 7.0, 7.0);
        painter.setPen(textColor);
        painter.drawText(bubbleRect, Qt::AlignCenter, text);
    }
}

std::vector<TransientPrimitive> CamRenderLayer::buildProcessArrowPrimitives(const ViewportRenderContext& context) const
{
    const std::vector<ProcessUnitArrowOverlay> arrows = buildProcessUnitArrowOverlays(context);
    std::vector<TransientPrimitive> primitives;
    primitives.reserve(arrows.size());

    const float pixelScale = std::max(context.pixelToWorldScale, 1.0e-4f);
    const float headLength = pixelScale * 13.0f;
    const float headHalfWidth = pixelScale * 5.0f;
    const QVector3D viewForward = context.viewForward.lengthSquared() > 1.0e-6f
        ? context.viewForward.normalized()
        : QVector3D(0.0f, 0.0f, -1.0f);

    for (const ProcessUnitArrowOverlay& arrow : arrows)
    {
        QVector3D perpendicular = QVector3D::crossProduct(viewForward, arrow.direction);

        if (perpendicular.lengthSquared() <= 1.0e-6f)
        {
            perpendicular = QVector3D(-arrow.direction.y(), arrow.direction.x(), 0.0f);
        }

        if (perpendicular.lengthSquared() <= 1.0e-6f)
        {
            perpendicular = buildPerpendicularDirection(arrow.direction);
        }

        if (perpendicular.lengthSquared() > 1.0e-6f)
        {
            perpendicular.normalize();
        }

        if (perpendicular.lengthSquared() <= 1.0e-6f)
        {
            continue;
        }

        const QVector3D tip = arrow.startPoint;
        const QVector3D headBase = tip - arrow.direction * headLength;
        const QVector3D notch = tip - arrow.direction * (headLength * 0.72f);

        QVector3D arrowColor;
        if (arrow.selected)
        {
            arrowColor = QVector3D(1.0f, 0.78f, 0.30f);
        }
        else if (arrow.unitOrder >= 0)
        {
            arrowColor = arrow.reverse ? QVector3D(1.0f, 0.42f, 0.28f) : QVector3D(0.12f, 0.92f, 0.72f);
        }
        else
        {
            arrowColor = arrow.reverse ? QVector3D(0.82f, 0.34f, 0.26f) : QVector3D(0.34f, 0.78f, 0.58f);
        }

        TransientPrimitive headPrimitive;
        headPrimitive.primitiveType = GL_TRIANGLES;
        headPrimitive.color = arrowColor;
        headPrimitive.vertices =
        {
            tip,
            headBase + perpendicular * headHalfWidth,
            notch,

            tip,
            notch,
            headBase - perpendicular * headHalfWidth
        };
        primitives.push_back(std::move(headPrimitive));
    }

    return primitives;
}

std::vector<TransientPrimitive> CamRenderLayer::buildRotaryEndCutPrimitives(const ViewportRenderContext& context) const
{
    if (!m_visualsVisible || !m_rotaryEndCutsVisible || m_processState == nullptr)
    {
        return {};
    }

    std::vector<TransientPrimitive> primitives;
    const float pixelScale = std::max(context.pixelToWorldScale, 1.0e-4f);
    const QVector3D screenOffset = context.viewRight.normalized() * pixelScale * 1.4f;
    const bool darkBackground = context.theme.viewerBackgroundColor.lightnessF() < 0.5;
    const QVector3D laserWallImpactColor = darkBackground
        ? QVector3D(0.22f, 0.92f, 0.66f)
        : QVector3D(0.05f, 0.55f, 0.38f);

    for (const std::unique_ptr<CadItem>& entity : entities())
    {
        if (entity == nullptr || entity->m_geometry.vertices.size() < 2)
        {
            continue;
        }

        const auto state = m_processState->stateOrDefault(entity->m_entityId);
        if (state.overrideData.boundaryRole == cadcam::planning::BoundaryRole::None
            || state.overrideData.boundaryPairId < 0) continue;

        const QVector3D color = state.overrideData.boundaryRole == cadcam::planning::BoundaryRole::Waste
            ? QVector3D(1.0f, 0.60f, 0.12f)
            : QVector3D(0.18f, 0.68f, 1.0f);

        for (int layer = -1; layer <= 1; ++layer)
        {
            TransientPrimitive path;
            path.primitiveType = GL_LINE_STRIP;
            path.color = color;
            path.opacity = layer == 0 ? 0.92f : 0.22f;
            path.vertices.reserve(entity->m_geometry.vertices.size());

            for (const QVector3D& point : entity->m_geometry.vertices)
            {
                path.vertices.push_back(point + screenOffset * static_cast<float>(layer));
            }

            primitives.push_back(std::move(path));
        }

    }

    const auto appendPreviewPaths = [&](const std::vector<QVector<QVector3D>>& paths,
        const QVector3D& color, bool visible)
    {
        if (!visible) return;
        for (const auto& vertices : paths)
        {
            if (vertices.size() < 2) continue;
            TransientPrimitive path;
            path.primitiveType = GL_LINE_STRIP;
            path.color = color;
            path.opacity = 0.86f;
            path.vertices = vertices;
            primitives.push_back(std::move(path));
        }
    };
    appendPreviewPaths(m_rotaryEndCutLaserWallImpactPreviewPaths,
        laserWallImpactColor, m_rotaryLaserWallImpactVisible);
    appendPreviewPaths(m_rotaryEndCutExpectedSurfacePreviewPaths,
        darkBackground ? QVector3D(1.0f, 0.45f, 0.92f) : QVector3D(0.65f, 0.12f, 0.65f),
        m_rotaryExpectedSurfaceVisible);

    return primitives;
}
