// CadViewer 临时图元构建实现
#include "platform/pch.h"

#include "cad/view/CadViewer.h"

#include "cad/document/CadDocument.h"
#include "cad/items/CadItem.h"
#include "cad/view/rendering/CadPreviewBuilder.h"
#include "cad/view/rendering/CadProcessVisualUtils.h"
#include "compatibility/legacy/LegacyCadItemPathBridge.h"
#include "core/machine/RotaryKinematics.h"
#include "core/machining/CutSurfaceExpectation.h"
#include "core/machining/TubeSectionProjector.h"

#include <QStringList>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace
{
    using cadcam::geometry::Path3D;
    using cadcam::geometry::Vector3d;
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

        QVector3D referenceAxis =
            std::abs(QVector3D::dotProduct(normalizedDirection, QVector3D(0.0f, 0.0f, 1.0f))) < 0.95f
            ? QVector3D(0.0f, 0.0f, 1.0f)
            : QVector3D(1.0f, 0.0f, 0.0f);

        QVector3D perpendicular = QVector3D::crossProduct(normalizedDirection, referenceAxis);

        if (perpendicular.lengthSquared() <= 1.0e-6f)
        {
            referenceAxis = QVector3D(0.0f, 1.0f, 0.0f);
            perpendicular = QVector3D::crossProduct(normalizedDirection, referenceAxis);
        }

        if (perpendicular.lengthSquared() <= 1.0e-6f)
        {
            return QVector3D(1.0f, 0.0f, 0.0f);
        }

        perpendicular.normalize();
        return perpendicular;
    }

    QVector<QVector3D> buildTriangleVertices(const QVector3D& center, QVector3D direction, float size)
    {
        if (direction.lengthSquared() <= 1.0e-6f)
        {
            direction = QVector3D(0.0f, 1.0f, 0.0f);
        }
        else
        {
            direction.normalize();
        }

        const QVector3D perpendicular = buildPerpendicularDirection(direction);

        const QVector3D tip = center + direction * size;
        const QVector3D baseCenter = center - direction * (size * 0.8f);
        const QVector3D left = baseCenter + perpendicular * (size * 0.7f);
        const QVector3D right = baseCenter - perpendicular * (size * 0.7f);
        return { tip, left, right };
    }

}

std::vector<TransientPrimitive> CadViewer::buildTransientPrimitives() const
{
    return CadPreviewBuilder::buildTransientPrimitives(m_controller.drawState(), selectedEntity(), selectedEntities());
}

void CadViewer::rebuildRotaryEndCutPreviewPaths()
{
    m_rotaryEndCutLaserWallImpactPreviewPaths.clear();
    m_rotaryEndCutExpectedSurfacePreviewPaths.clear();
    CadDocument* scene = m_sceneCoordinator.document();
    if (scene == nullptr || m_processState == nullptr
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
    for (const std::unique_ptr<CadItem>& entity : scene->m_entities)
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

std::vector<CadViewer::ProcessUnitArrowOverlay>
CadViewer::buildProcessUnitArrowOverlays() const
{
    if (!m_processVisualsVisible || !m_processDirectionVisible
        || m_processPresentation == nullptr) return {};

    CadDocument* scene = m_sceneCoordinator.document();
    if (scene == nullptr) return {};

    std::map<cadcam::geometry::EntityId, CadItem*> itemsById;
    std::set<cadcam::geometry::EntityId> selectedIds;
    for (const std::unique_ptr<CadItem>& entity : scene->m_entities)
    {
        if (entity == nullptr || entity->m_entityId == 0U) continue;
        itemsById.emplace(entity->m_entityId, entity.get());
        if (entity->m_isSelected) selectedIds.insert(entity->m_entityId);
    }

    const float pixelScale = std::max(pixelToWorldScale(), 1.0e-4f);
    const float headLength = pixelScale * 13.0f;
    std::vector<ProcessUnitArrowOverlay> overlays;
    overlays.reserve(m_processPresentation->processUnits.size());
    for (const cadcam::process::ProcessUnitPresentation& unit
        : m_processPresentation->processUnits)
    {
        const auto item = itemsById.find(unit.anchorEntityId);
        if (item == itemsById.end()) continue;

        cadcam::process::ProcessPresentationEntry anchorPresentation;
        anchorPresentation.entityId = unit.anchorEntityId;
        anchorPresentation.processOrder = unit.unitOrder;
        anchorPresentation.reverse = unit.anchorReverse;
        anchorPresentation.startParameter = unit.anchorStartParameter;
        const CadProcessVisualInfo info = buildProcessVisualInfo
            (item->second, &anchorPresentation);
        if (!info.valid || info.direction.lengthSquared() <= 1.0e-6f) continue;

        const QPoint tipScreen = worldToScreen(info.startPoint);
        const QPoint baseScreen = worldToScreen
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

bool CadViewer::hitTestProcessUnitArrow
(
    const QPoint& screenPos,
    ProcessUnitArrowOverlay* outArrow
) const
{
    const std::vector<ProcessUnitArrowOverlay> arrows = buildProcessUnitArrowOverlays();
    for (const ProcessUnitArrowOverlay& arrow : arrows)
    {
        if (!arrow.hitRect.contains(screenPos)) continue;
        if (outArrow != nullptr) *outArrow = arrow;
        return true;
    }
    return false;
}

std::vector<TransientPrimitive> CadViewer::buildProcessArrowPrimitives() const
{
    const std::vector<ProcessUnitArrowOverlay> arrows = buildProcessUnitArrowOverlays();
    std::vector<TransientPrimitive> primitives;
    primitives.reserve(arrows.size());

    const float pixelScale = std::max(pixelToWorldScale(), 1.0e-4f);
    const float headLength = pixelScale * 13.0f;
    const float headHalfWidth = pixelScale * 5.0f;
    const QVector3D viewForward = m_camera.forwardDirection().lengthSquared() > 1.0e-6f
        ? m_camera.forwardDirection().normalized()
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

std::vector<TransientPrimitive> CadViewer::buildRotaryEndCutPrimitives() const
{
    if (!m_processVisualsVisible || !m_rotaryEndCutsVisible)
    {
        return {};
    }

    CadDocument* scene = m_sceneCoordinator.document();

    if (scene == nullptr)
    {
        return {};
    }

    std::vector<TransientPrimitive> primitives;
    const float pixelScale = std::max(pixelToWorldScale(), 1.0e-4f);
    const QVector3D screenOffset = m_camera.rightDirection().normalized() * pixelScale * 1.4f;
    const bool darkBackground = m_theme.viewerBackgroundColor.lightnessF() < 0.5;
    const QVector3D laserWallImpactColor = darkBackground
        ? QVector3D(0.22f, 0.92f, 0.66f)
        : QVector3D(0.05f, 0.55f, 0.38f);

    for (const std::unique_ptr<CadItem>& entity : scene->m_entities)
    {
        if (entity == nullptr || m_processState == nullptr
            || entity->m_geometry.vertices.size() < 2)
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

std::vector<TransientPrimitive> CadViewer::buildSelectedEntityHandlePrimitives() const
{
    const CadItem* selectedItem = selectedEntity();

    if (selectedItem == nullptr)
    {
        return {};
    }

    const QVector<CadSelectionHandleInfo> handles = buildSelectionHandleInfo(selectedItem, xlineHandleWorldLength());

    if (handles.isEmpty())
    {
        return {};
    }

    const int hoveredHandleIndex = resolveHoveredHandleIndex(handles);

    TransientPrimitive basePointOuterPrimitive;
    basePointOuterPrimitive.primitiveType = GL_POINTS;
    basePointOuterPrimitive.color =
    {
        static_cast<float>(m_theme.selectedBasePointColor.redF()),
        static_cast<float>(m_theme.selectedBasePointColor.greenF()),
        static_cast<float>(m_theme.selectedBasePointColor.blueF())
    };
    basePointOuterPrimitive.pointSize = 13.0f;
    basePointOuterPrimitive.roundPoint = true;

    TransientPrimitive basePointInnerPrimitive;
    basePointInnerPrimitive.primitiveType = GL_POINTS;
    basePointInnerPrimitive.color =
    {
        static_cast<float>(m_theme.viewerBackgroundColor.redF()),
        static_cast<float>(m_theme.viewerBackgroundColor.greenF()),
        static_cast<float>(m_theme.viewerBackgroundColor.blueF())
    };
    basePointInnerPrimitive.pointSize = 6.2f;
    basePointInnerPrimitive.roundPoint = true;

    TransientPrimitive controlPointOuterPrimitive;
    controlPointOuterPrimitive.primitiveType = GL_POINTS;
    controlPointOuterPrimitive.color =
    {
        static_cast<float>(m_theme.selectedControlPointColor.redF()),
        static_cast<float>(m_theme.selectedControlPointColor.greenF()),
        static_cast<float>(m_theme.selectedControlPointColor.blueF())
    };
    controlPointOuterPrimitive.pointSize = 10.0f;
    controlPointOuterPrimitive.roundPoint = true;

    TransientPrimitive controlPointInnerPrimitive;
    controlPointInnerPrimitive.primitiveType = GL_POINTS;
    controlPointInnerPrimitive.color =
    {
        static_cast<float>(m_theme.viewerBackgroundColor.redF()),
        static_cast<float>(m_theme.viewerBackgroundColor.greenF()),
        static_cast<float>(m_theme.viewerBackgroundColor.blueF())
    };
    controlPointInnerPrimitive.pointSize = 4.4f;
    controlPointInnerPrimitive.roundPoint = true;

    TransientPrimitive trianglePrimitive;
    trianglePrimitive.primitiveType = GL_TRIANGLES;
    trianglePrimitive.color = controlPointOuterPrimitive.color;

    TransientPrimitive hoveredPointOuterPrimitive;
    hoveredPointOuterPrimitive.primitiveType = GL_POINTS;
    hoveredPointOuterPrimitive.color =
    {
        static_cast<float>(m_theme.accentColor.redF()),
        static_cast<float>(m_theme.accentColor.greenF()),
        static_cast<float>(m_theme.accentColor.blueF())
    };
    hoveredPointOuterPrimitive.pointSize = 17.0f;
    hoveredPointOuterPrimitive.roundPoint = true;

    TransientPrimitive hoveredPointInnerPrimitive;
    hoveredPointInnerPrimitive.primitiveType = GL_POINTS;
    hoveredPointInnerPrimitive.color = { 1.0f, 1.0f, 1.0f };
    hoveredPointInnerPrimitive.pointSize = 9.0f;
    hoveredPointInnerPrimitive.roundPoint = true;

    TransientPrimitive hoveredTrianglePrimitive;
    hoveredTrianglePrimitive.primitiveType = GL_TRIANGLES;
    hoveredTrianglePrimitive.color = hoveredPointOuterPrimitive.color;

    const float pixelScale = std::max(pixelToWorldScale(), 1.0e-4f);
    const float triangleSize = pixelScale * 6.5f;
    const float hoveredTriangleSize = pixelScale * 8.4f;

    for (int handleIndex = 0; handleIndex < handles.size(); ++handleIndex)
    {
        const CadSelectionHandleInfo& handle = handles.at(handleIndex);

        if (handle.shape == CadSelectionHandleShape::Triangle)
        {
            const QVector<QVector3D> triangleVertices = buildTriangleVertices(handle.position, handle.direction, triangleSize);
            trianglePrimitive.vertices += triangleVertices;

            if (handleIndex == hoveredHandleIndex)
            {
                const QVector<QVector3D> hoveredTriangleVertices = buildTriangleVertices
                (
                    handle.position,
                    handle.direction,
                    hoveredTriangleSize
                );
                hoveredTrianglePrimitive.vertices += hoveredTriangleVertices;
            }
        }
        else if (handle.isBasePoint)
        {
            basePointOuterPrimitive.vertices.push_back(handle.position);
            basePointInnerPrimitive.vertices.push_back(handle.position);
        }
        else
        {
            controlPointOuterPrimitive.vertices.push_back(handle.position);
            controlPointInnerPrimitive.vertices.push_back(handle.position);
        }

        if (handleIndex == hoveredHandleIndex)
        {
            hoveredPointOuterPrimitive.vertices.push_back(handle.position);
            hoveredPointInnerPrimitive.vertices.push_back(handle.position);
        }
    }

    std::vector<TransientPrimitive> primitives;

    if (!hoveredPointOuterPrimitive.vertices.isEmpty())
    {
        primitives.push_back(std::move(hoveredPointOuterPrimitive));
    }

    if (!controlPointOuterPrimitive.vertices.isEmpty())
    {
        primitives.push_back(std::move(controlPointOuterPrimitive));
    }

    if (!controlPointInnerPrimitive.vertices.isEmpty())
    {
        primitives.push_back(std::move(controlPointInnerPrimitive));
    }

    if (!hoveredPointInnerPrimitive.vertices.isEmpty())
    {
        primitives.push_back(std::move(hoveredPointInnerPrimitive));
    }

    if (!trianglePrimitive.vertices.isEmpty())
    {
        primitives.push_back(std::move(trianglePrimitive));
    }

    if (!hoveredTrianglePrimitive.vertices.isEmpty())
    {
        primitives.push_back(std::move(hoveredTrianglePrimitive));
    }

    if (!basePointOuterPrimitive.vertices.isEmpty())
    {
        primitives.push_back(std::move(basePointOuterPrimitive));
    }

    if (!basePointInnerPrimitive.vertices.isEmpty())
    {
        primitives.push_back(std::move(basePointInnerPrimitive));
    }

    return primitives;
}
