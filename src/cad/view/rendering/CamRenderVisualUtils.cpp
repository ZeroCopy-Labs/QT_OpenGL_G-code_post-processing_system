#include "platform/pch.h"

#include "cad/view/rendering/CamRenderVisualUtils.h"

#include "cad/items/CadItem.h"
#include "cad/view/rendering/CadItemGeometryDetail.h"

#include <cmath>

using namespace cadview::geometrydetail;


namespace
{
    CamProcessExclusionVisual resolveCamProcessExclusionVisualByEntityId
    (
        cadcam::geometry::EntityId entityId,
        const cadcam::process::ProcessPresentationSnapshot* presentation
    )
    {
        const cadcam::process::ProcessPresentationEntry* entry = presentation != nullptr
            ? presentation->find(entityId) : nullptr;
        return entry != nullptr && entry->excluded
            ? CamProcessExclusionVisual::PlannedExclusion
            : CamProcessExclusionVisual::None;
    }
}

CamProcessExclusionVisual resolveCamProcessExclusionVisual
(
    const CadItem* item,
    const cadcam::process::ProcessPresentationSnapshot* presentation
)
{
    if (item == nullptr)
    {
        return CamProcessExclusionVisual::None;
    }

    const cadcam::geometry::EntityId processEntityId = item->m_entityId;
    if (processEntityId == 0)
    {
        return CamProcessExclusionVisual::None;
    }

    return resolveCamProcessExclusionVisualByEntityId
        (processEntityId, presentation);
}

bool isProcessVisualizable(const CadItem* item)
{
    if (item == nullptr)
    {
        return false;
    }

    switch (item->m_type)
    {
    case DRW::ETYPE::LINE:
    case DRW::ETYPE::ARC:
    case DRW::ETYPE::CIRCLE:
    case DRW::ETYPE::ELLIPSE:
    case DRW::ETYPE::POLYLINE:
    case DRW::ETYPE::LWPOLYLINE:
    case DRW::ETYPE::SPLINE:
        return true;
    default:
        return false;
    }
}

CamProcessVisualInfo buildCamProcessVisualInfo
(
    const CadItem* item,
    const cadcam::process::ProcessPresentationEntry* presentation
)
{
    CamProcessVisualInfo info;

    if (item == nullptr || (presentation != nullptr && presentation->excluded))
    {
        return info;
    }

    info.processOrder = presentation != nullptr ? presentation->processOrder : -1;
    info.isReverse = presentation != nullptr && presentation->reverse;
    const std::optional<double> startParameter = presentation != nullptr
        ? presentation->startParameter : std::nullopt;

    if (!isProcessVisualizable(item) || item->m_nativeEntity == nullptr)
    {
        return info;
    }

    QVector3D preferredAnchor;
    const bool hasGeometryAnchor = tryBuildGeometryAnchor(item, preferredAnchor);

    switch (item->m_type)
    {
    case DRW::ETYPE::LINE:
    {
        const DRW_Line* line = static_cast<const DRW_Line*>(item->m_nativeEntity);
        info.forwardStartPoint = QVector3D(line->basePoint.x, line->basePoint.y, line->basePoint.z);
        info.forwardEndPoint = QVector3D(line->secPoint.x, line->secPoint.y, line->secPoint.z);
        info.labelAnchor = (info.forwardStartPoint + info.forwardEndPoint) * 0.5f;
        info.direction = normalizeOrZero(info.forwardEndPoint - info.forwardStartPoint);
        break;
    }
    case DRW::ETYPE::ARC:
    {
        const DRW_Arc* arc = static_cast<const DRW_Arc*>(item->m_nativeEntity);
        info.forwardStartPoint = arcPointAt(arc, arc->staangle);
        info.forwardEndPoint = arcPointAt(arc, arc->endangle);
        info.labelAnchor = hasGeometryAnchor ? preferredAnchor : (info.forwardStartPoint + info.forwardEndPoint) * 0.5f;
        info.direction = arcTangentAt(arc, arc->staangle, false);
        break;
    }
    case DRW::ETYPE::CIRCLE:
    {
        const DRW_Circle* circle = static_cast<const DRW_Circle*>(item->m_nativeEntity);
        const double circleStartParameter = effectiveCircleStartParameter(startParameter);
        info.closedPath = true;
        info.forwardStartPoint = circlePointAt(circle, circleStartParameter);
        info.forwardEndPoint = info.forwardStartPoint;
        info.labelAnchor = CadOcsGeometry::center(circle);
        info.direction = circleTangentAt(circle, circleStartParameter, false);
        break;
    }
    case DRW::ETYPE::ELLIPSE:
    {
        const DRW_Ellipse* ellipse = static_cast<const DRW_Ellipse*>(item->m_nativeEntity);
        double startParam = ellipse->staparam;
        double endParam = ellipse->endparam;
        info.closedPath = isFullEllipsePath(ellipse);

        if (info.closedPath)
        {
            startParam = effectiveClosedEllipseStartParameter(startParameter, ellipse);
            endParam = startParam;
        }
        else
        {
            while (endParam <= startParam)
            {
                endParam += kTwoPi;
            }
        }

        info.forwardStartPoint = ellipsePointAt(ellipse, startParam);
        info.forwardEndPoint = ellipsePointAt(ellipse, endParam);
        info.labelAnchor = info.closedPath
            ? QVector3D(ellipse->basePoint.x, ellipse->basePoint.y, ellipse->basePoint.z)
            : (hasGeometryAnchor ? preferredAnchor : (info.forwardStartPoint + info.forwardEndPoint) * 0.5f);
        info.direction = ellipseTangentAt(ellipse, startParam, false);
        break;
    }
    case DRW::ETYPE::POLYLINE:
    {
        const DRW_Polyline* polyline = static_cast<const DRW_Polyline*>(item->m_nativeEntity);

        if (polyline->vertlist.empty())
        {
            return info;
        }

        info.closedPath = (polyline->flags & 1) != 0;
        const size_t seamIndex = info.closedPath
            ? effectiveClosedPolylineStartIndex(startParameter, polyline->vertlist.size())
            : 0;
        const auto& firstVertex = polyline->vertlist.at(seamIndex);
        const auto& lastVertex = polyline->vertlist.back();
        info.forwardStartPoint = polylineVertexToWcs(polyline, firstVertex);
        info.forwardEndPoint = info.closedPath
            ? info.forwardStartPoint
            : polylineVertexToWcs(polyline, lastVertex);
        info.labelAnchor = hasGeometryAnchor ? preferredAnchor : info.forwardStartPoint;
        info.direction = info.closedPath
            ? polylineForwardStartTangentAt(polyline, seamIndex)
            : polylineForwardStartTangent(polyline);
        break;
    }
    case DRW::ETYPE::LWPOLYLINE:
    {
        const DRW_LWPolyline* polyline = static_cast<const DRW_LWPolyline*>(item->m_nativeEntity);

        if (polyline->vertlist.empty())
        {
            return info;
        }

        info.closedPath = (polyline->flags & 1) != 0;
        const size_t seamIndex = info.closedPath
            ? effectiveClosedPolylineStartIndex(startParameter, polyline->vertlist.size())
            : 0;
        const auto& firstVertex = polyline->vertlist.at(seamIndex);
        const auto& lastVertex = polyline->vertlist.back();
        info.forwardStartPoint = lwPolylineVertexToWcs(polyline, firstVertex);
        info.forwardEndPoint = info.closedPath
            ? info.forwardStartPoint
            : lwPolylineVertexToWcs(polyline, lastVertex);
        info.labelAnchor = hasGeometryAnchor ? preferredAnchor : info.forwardStartPoint;
        info.direction = info.closedPath
            ? lwPolylineForwardStartTangentAt(polyline, seamIndex)
            : lwPolylineForwardStartTangent(polyline);
        break;
    }
    case DRW::ETYPE::SPLINE:
    {
        const QVector<QVector3D>& vertices = item->m_geometry.vertices;
        if (vertices.size() < 2)
        {
            return info;
        }
        const DRW_Spline* spline = static_cast<const DRW_Spline*>(item->m_nativeEntity);
        info.closedPath = (spline->flags & (1 | 2)) != 0;
        info.forwardStartPoint = vertices.constFirst();
        info.forwardEndPoint = info.closedPath
            ? info.forwardStartPoint : vertices.constLast();
        info.labelAnchor = hasGeometryAnchor ? preferredAnchor : info.forwardStartPoint;
        info.direction = normalizeOrZero(vertices.at(1) - vertices.constFirst());
        break;
    }
    default:
        return info;
    }

    info.startPoint = info.isReverse ? info.forwardEndPoint : info.forwardStartPoint;
    info.endPoint = info.isReverse ? info.forwardStartPoint : info.forwardEndPoint;

    if (info.isReverse)
    {
        switch (item->m_type)
        {
        case DRW::ETYPE::LINE:
            info.direction = normalizeOrZero(info.endPoint - info.startPoint);
            break;
        case DRW::ETYPE::ARC:
        {
            const DRW_Arc* arc = static_cast<const DRW_Arc*>(item->m_nativeEntity);
            info.direction = arcTangentAt(arc, arc->endangle, true);
            break;
        }
        case DRW::ETYPE::CIRCLE:
            info.direction = circleTangentAt(static_cast<const DRW_Circle*>(item->m_nativeEntity), effectiveCircleStartParameter(startParameter), true);
            break;
        case DRW::ETYPE::ELLIPSE:
        {
            const DRW_Ellipse* ellipse = static_cast<const DRW_Ellipse*>(item->m_nativeEntity);
            const double parameter = info.closedPath
                ? effectiveClosedEllipseStartParameter(startParameter, ellipse)
                : ellipse->endparam;
            info.direction = ellipseTangentAt(ellipse, parameter, true);
            break;
        }
        case DRW::ETYPE::POLYLINE:
        {
            const DRW_Polyline* polyline = static_cast<const DRW_Polyline*>(item->m_nativeEntity);
            info.direction = info.closedPath
                ? polylineReverseStartTangentAt(polyline, effectiveClosedPolylineStartIndex(startParameter, polyline->vertlist.size()))
                : polylineReverseStartTangent(polyline);
            break;
        }
        case DRW::ETYPE::LWPOLYLINE:
        {
            const DRW_LWPolyline* polyline = static_cast<const DRW_LWPolyline*>(item->m_nativeEntity);
            info.direction = info.closedPath
                ? lwPolylineReverseStartTangentAt(polyline, effectiveClosedPolylineStartIndex(startParameter, polyline->vertlist.size()))
                : lwPolylineReverseStartTangent(polyline);
            break;
        }
        case DRW::ETYPE::SPLINE:
        {
            const QVector<QVector3D>& vertices = item->m_geometry.vertices;
            if (vertices.size() >= 2)
            {
                const int startIndex = vertices.size() - 1;
                info.direction = normalizeOrZero
                    (vertices.at(startIndex - 1) - vertices.at(startIndex));
            }
            break;
        }
        default:
            break;
        }
    }

    if (info.direction.lengthSquared() <= kVisualEpsilon)
    {
        info.direction = normalizeOrZero(info.endPoint - info.startPoint);
    }

    info.valid = true;
    return info;
}