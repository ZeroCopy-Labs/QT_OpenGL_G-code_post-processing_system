#include "platform/pch.h"

#include "cad/view/rendering/CadSelectionVisualUtils.h"

#include "cad/items/CadItem.h"
#include "cad/view/rendering/CadItemGeometryDetail.h"

#include <cmath>

using namespace cadview::geometrydetail;

QVector<CadSelectionHandleInfo> buildSelectionHandleInfo(const CadItem* item, float xlineHandleLength)
{
    QVector<CadSelectionHandleInfo> handles;

    if (item == nullptr || item->m_nativeEntity == nullptr)
    {
        return handles;
    }

    switch (item->m_type)
    {
    case DRW::ETYPE::POINT:
    {
        const DRW_Point* point = static_cast<const DRW_Point*>(item->m_nativeEntity);
        appendSelectionHandle(handles, QVector3D(point->basePoint.x, point->basePoint.y, point->basePoint.z), true, true, 0);
        break;
    }
    case DRW::ETYPE::LINE:
    {
        const DRW_Line* line = static_cast<const DRW_Line*>(item->m_nativeEntity);
        const QVector3D startPoint(line->basePoint.x, line->basePoint.y, line->basePoint.z);
        const QVector3D endPoint(line->secPoint.x, line->secPoint.y, line->secPoint.z);
        appendSelectionHandle(handles, startPoint, true, true, 0);
        appendSelectionHandle(handles, endPoint, false, true, 1);
        appendSelectionHandle(handles, (startPoint + endPoint) * 0.5f, false, true, 2);
        break;
    }
    case DRW::ETYPE::XLINE:
    {
        const DRW_Xline* xline = static_cast<const DRW_Xline*>(item->m_nativeEntity);
        QVector3D direction(xline->secPoint.x, xline->secPoint.y, xline->secPoint.z);

        if (direction.lengthSquared() <= kVisualEpsilon)
        {
            direction = QVector3D(1.0f, 0.0f, 0.0f);
        }
        else
        {
            direction.normalize();
        }

        const QVector3D basePoint(xline->basePoint.x, xline->basePoint.y, xline->basePoint.z);
        const float handleLength = std::isfinite(xlineHandleLength) && xlineHandleLength > kVisualEpsilon
            ? xlineHandleLength
            : 50.0f;
        appendSelectionHandle(handles, basePoint, true, true, 0);
        appendSelectionHandle(handles, basePoint + direction * handleLength, false, true, 1);
        appendSelectionHandle(handles, basePoint - direction * handleLength, false, true, 2);
        break;
    }
    case DRW::ETYPE::CIRCLE:
    {
        const DRW_Circle* circle = static_cast<const DRW_Circle*>(item->m_nativeEntity);
        appendSelectionHandle(handles, CadOcsGeometry::center(circle), true, true, 0);
        appendSelectionHandle(handles, circlePointAt(circle, 0.0), false, true, 1);
        appendSelectionHandle(handles, circlePointAt(circle, kPi * 0.5), false, true, 2);
        appendSelectionHandle(handles, circlePointAt(circle, kPi), false, true, 3);
        appendSelectionHandle(handles, circlePointAt(circle, kPi * 1.5), false, true, 4);
        break;
    }
    case DRW::ETYPE::ARC:
    {
        const DRW_Arc* arc = static_cast<const DRW_Arc*>(item->m_nativeEntity);
        appendSelectionHandle(handles, CadOcsGeometry::center(arc), true, true, 0);
        appendSelectionHandle
        (
            handles,
            arcPointAt(arc, arc->staangle),
            false,
            true,
            1,
            CadSelectionHandleShape::Triangle,
            arcTangentAt(arc, arc->staangle, false)
        );

        double endAngle = arc->endangle;

        while (endAngle <= arc->staangle)
        {
            endAngle += kTwoPi;
        }

        appendSelectionHandle(handles, arcPointAt(arc, (arc->staangle + endAngle) * 0.5), false, true, 2);
        appendSelectionHandle
        (
            handles,
            arcPointAt(arc, endAngle),
            false,
            true,
            3,
            CadSelectionHandleShape::Triangle,
            arcTangentAt(arc, endAngle, false)
        );
        break;
    }
    case DRW::ETYPE::ELLIPSE:
    {
        const DRW_Ellipse* ellipse = static_cast<const DRW_Ellipse*>(item->m_nativeEntity);
        QVector3D majorAxis;
        QVector3D minorAxis;

        if (!tryBuildEllipseAxes(ellipse, majorAxis, minorAxis))
        {
            return handles;
        }

        const QVector3D center(ellipse->basePoint.x, ellipse->basePoint.y, ellipse->basePoint.z);
        appendSelectionHandle(handles, center, true, true, 0);
        appendSelectionHandle(handles, center + majorAxis, false, true, 1);
        appendSelectionHandle(handles, center - majorAxis, false, true, 2);
        appendSelectionHandle(handles, center + minorAxis, false, true, 3);
        appendSelectionHandle(handles, center - minorAxis, false, true, 4);

        if (!isFullEllipsePath(ellipse))
        {
            appendSelectionHandle
            (
                handles,
                ellipsePointAt(ellipse, ellipse->staparam),
                false,
                true,
                5,
                CadSelectionHandleShape::Triangle,
                ellipseTangentAt(ellipse, ellipse->staparam, false)
            );
            appendSelectionHandle
            (
                handles,
                ellipsePointAt(ellipse, ellipse->endparam),
                false,
                true,
                6,
                CadSelectionHandleShape::Triangle,
                ellipseTangentAt(ellipse, ellipse->endparam, false)
            );
        }
        break;
    }
    case DRW::ETYPE::POLYLINE:
    {
        const DRW_Polyline* polyline = static_cast<const DRW_Polyline*>(item->m_nativeEntity);

        if (polyline->vertlist.empty())
        {
            return handles;
        }

        const auto& firstVertex = polyline->vertlist.front();
        appendSelectionHandle
        (
            handles,
            polylineVertexToWcs(polyline, firstVertex),
            true,
            true,
            0
        );

        for (size_t index = 1; index < polyline->vertlist.size(); ++index)
        {
            const auto& vertex = polyline->vertlist.at(index);
            appendSelectionHandle
            (
                handles,
                polylineVertexToWcs(polyline, vertex),
                false,
                true,
                static_cast<int>(index)
            );
        }
        break;
    }
    case DRW::ETYPE::LWPOLYLINE:
    {
        const DRW_LWPolyline* polyline = static_cast<const DRW_LWPolyline*>(item->m_nativeEntity);

        if (polyline->vertlist.empty())
        {
            return handles;
        }

        const auto& firstVertex = polyline->vertlist.front();
        appendSelectionHandle
        (
            handles,
            lwPolylineVertexToWcs(polyline, firstVertex),
            true,
            true,
            0
        );

        for (size_t index = 1; index < polyline->vertlist.size(); ++index)
        {
            const auto& vertex = polyline->vertlist.at(index);
            appendSelectionHandle
            (
                handles,
                lwPolylineVertexToWcs(polyline, vertex),
                false,
                true,
                static_cast<int>(index)
            );
        }
        break;
    }
    default:
        break;
    }

    return handles;
}