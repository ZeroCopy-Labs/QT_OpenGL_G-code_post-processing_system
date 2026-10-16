// SelectionRenderLayer 实现文件
// CAD 选择可视化：选中与框选候选高亮、框选窗口预览、选中手柄、吸附高亮、重叠手柄选择框。
#include "platform/pch.h"

#include "cad/view/rendering/SelectionRenderLayer.h"

#include "cad/view/rendering/CadSelectionVisualUtils.h"

#include "cad/items/CadItem.h"
#include "cad/view/CadViewerUtils.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <QFontMetrics>
#include <QPen>
#include <QPolygonF>

namespace
{
    // 构造与给定方向垂直的单位向量。
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
    // 构造屏幕朝向的三角形手柄顶点。
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


    QString handleTypeDisplayName(const CadItem* selectedItem, const CadSelectionHandleInfo& handle)
    {
        if (handle.isBasePoint)
        {
            return QStringLiteral("基点");
        }

        if (selectedItem == nullptr)
        {
            return QStringLiteral("拉伸点");
        }

        switch (selectedItem->m_type)
        {
        case DRW::ETYPE::POINT:
            return QStringLiteral("点位控制点");
        case DRW::ETYPE::LINE:
            return handle.pointIndex == 2 ? QStringLiteral("中心移动点") : QStringLiteral("端点拉伸点");
        case DRW::ETYPE::XLINE:
            return (handle.pointIndex == 1 || handle.pointIndex == 2)
                ? QStringLiteral("方向控制点")
                : QStringLiteral("拉伸点");
        case DRW::ETYPE::CIRCLE:
            return QStringLiteral("半径控制点");
        case DRW::ETYPE::ARC:
            if (handle.pointIndex == 1)
            {
                return QStringLiteral("起点拉伸点");
            }

            if (handle.pointIndex == 2)
            {
                return QStringLiteral("半径控制点");
            }

            if (handle.pointIndex == 3)
            {
                return QStringLiteral("终点拉伸点");
            }

            return QStringLiteral("拉伸点");
        case DRW::ETYPE::ELLIPSE:
            if (handle.pointIndex == 1 || handle.pointIndex == 2)
            {
                return QStringLiteral("长轴控制点");
            }

            if (handle.pointIndex == 3 || handle.pointIndex == 4)
            {
                return QStringLiteral("短轴控制点");
            }

            if (handle.pointIndex == 5)
            {
                return QStringLiteral("起点拉伸点");
            }

            if (handle.pointIndex == 6)
            {
                return QStringLiteral("终点拉伸点");
            }

            return QStringLiteral("拉伸点");
        case DRW::ETYPE::POLYLINE:
        case DRW::ETYPE::LWPOLYLINE:
            return QStringLiteral("顶点拉伸点");
        default:
            return QStringLiteral("拉伸点");
        }
    }

    QString overlappedHandleRowText
    (
        const CadItem* selectedItem,
        const CadSelectionHandleInfo& handle,
        int ordinal
    )
    {
        return QStringLiteral("%1. %2")
            .arg(ordinal + 1)
            .arg(handleTypeDisplayName(selectedItem, handle));
    }

}

void SelectionRenderLayer::renderSelectionOverlays
(
    QPainter& painter,
    const ViewportRenderContext& context,
    const std::vector<std::unique_ptr<CadItem>>& entities,
    const QSet<RenderEntityKey>& selectedRenderKeys,
    const QSet<RenderEntityKey>& windowPreviewRenderKeys
) const
{
    if (selectedRenderKeys.isEmpty() && windowPreviewRenderKeys.isEmpty())
    {
        return;
    }

    painter.setRenderHint(QPainter::Antialiasing, true);

    for (const std::unique_ptr<CadItem>& entity : entities)
    {
        if (entity == nullptr)
        {
            continue;
        }

        const RenderEntityKey renderKey = CadViewerUtils::toRenderEntityKey(entity.get());
        const bool committedSelected = selectedRenderKeys.contains(renderKey);
        const bool previewSelected = windowPreviewRenderKeys.contains(renderKey);

        if (!committedSelected && !previewSelected)
        {
            continue;
        }

        const QVector<QVector3D>& worldVertices = entity->m_geometry.vertices;

        if (worldVertices.isEmpty())
        {
            continue;
        }

        QPolygonF projectedPolyline;
        projectedPolyline.reserve(worldVertices.size());
        bool validProjection = true;

        for (const QVector3D& worldVertex : worldVertices)
        {
            const QPointF screenPoint = CadViewerUtils::projectToScreen
            (
                worldVertex,
                context.viewProjection,
                context.viewportWidth,
                context.viewportHeight
            );

            if (!qIsFinite(screenPoint.x()) || !qIsFinite(screenPoint.y()))
            {
                validProjection = false;
                break;
            }

            projectedPolyline << screenPoint;
        }

        if (!validProjection || projectedPolyline.isEmpty())
        {
            continue;
        }

        const QColor haloColor = committedSelected
            ? QColor(60, 174, 255, 48)
            : (context.theme.dark ? QColor(255, 255, 255, 52) : QColor(42, 78, 104, 48));
        const QColor glowColor = committedSelected
            ? QColor(76, 184, 255, 105)
            : QColor(255, 255, 255, 125);
        const QColor coreColor = committedSelected
            ? QColor(105, 202, 255, 195)
            : QColor(255, 255, 255, 235);

        QPen haloPen(haloColor, 7.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        QPen glowPen(glowColor, 3.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        QPen corePen(coreColor, 1.25, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);

        painter.setBrush(Qt::NoBrush);

        if (projectedPolyline.size() == 1)
        {
            const QPointF center = projectedPolyline.front();
            painter.setPen(haloPen);
            painter.drawEllipse(center, 8.0, 8.0);
            painter.setPen(glowPen);
            painter.drawEllipse(center, 6.0, 6.0);
            painter.setPen(corePen);
            painter.drawEllipse(center, 4.0, 4.0);
        }
        else
        {
            painter.setPen(haloPen);
            painter.drawPolyline(projectedPolyline);

            painter.setPen(glowPen);
            painter.drawPolyline(projectedPolyline);

            painter.setPen(corePen);
            painter.drawPolyline(projectedPolyline);
        }
    }
}

void SelectionRenderLayer::renderSelectionWindow
(
    QPainter& painter,
    const QRect& selectionRect,
    bool crossingSelection
) const
{
    const QColor borderColor = crossingSelection
        ? QColor(66, 205, 104, 220)
        : QColor(74, 152, 250, 220);
    const QColor fillColor = crossingSelection
        ? QColor(66, 205, 104, 42)
        : QColor(74, 152, 250, 42);

    painter.setRenderHint(QPainter::Antialiasing, false);

    QPen borderPen(borderColor, 1.0, Qt::DashLine);
    borderPen.setDashPattern({ 7.0, 4.0 });

    painter.setPen(borderPen);
    painter.setBrush(fillColor);
    painter.drawRect(selectionRect);
}


std::vector<TransientPrimitive> SelectionRenderLayer::buildHandlePrimitives
(
    const ViewportRenderContext& context,
    const CadItem* selectedItem,
    int hoveredHandleIndex
) const
{
    if (selectedItem == nullptr)
    {
        return {};
    }

    const QVector<CadSelectionHandleInfo> handles = buildSelectionHandleInfo
    (
        selectedItem,
        context.xlineHandleWorldLength
    );

    if (handles.isEmpty())
    {
        return {};
    }

    TransientPrimitive basePointOuterPrimitive;
    basePointOuterPrimitive.primitiveType = GL_POINTS;
    basePointOuterPrimitive.color =
    {
        static_cast<float>(context.theme.selectedBasePointColor.redF()),
        static_cast<float>(context.theme.selectedBasePointColor.greenF()),
        static_cast<float>(context.theme.selectedBasePointColor.blueF())
    };
    basePointOuterPrimitive.pointSize = 13.0f;
    basePointOuterPrimitive.roundPoint = true;

    TransientPrimitive basePointInnerPrimitive;
    basePointInnerPrimitive.primitiveType = GL_POINTS;
    basePointInnerPrimitive.color =
    {
        static_cast<float>(context.theme.viewerBackgroundColor.redF()),
        static_cast<float>(context.theme.viewerBackgroundColor.greenF()),
        static_cast<float>(context.theme.viewerBackgroundColor.blueF())
    };
    basePointInnerPrimitive.pointSize = 6.2f;
    basePointInnerPrimitive.roundPoint = true;

    TransientPrimitive controlPointOuterPrimitive;
    controlPointOuterPrimitive.primitiveType = GL_POINTS;
    controlPointOuterPrimitive.color =
    {
        static_cast<float>(context.theme.selectedControlPointColor.redF()),
        static_cast<float>(context.theme.selectedControlPointColor.greenF()),
        static_cast<float>(context.theme.selectedControlPointColor.blueF())
    };
    controlPointOuterPrimitive.pointSize = 10.0f;
    controlPointOuterPrimitive.roundPoint = true;

    TransientPrimitive controlPointInnerPrimitive;
    controlPointInnerPrimitive.primitiveType = GL_POINTS;
    controlPointInnerPrimitive.color =
    {
        static_cast<float>(context.theme.viewerBackgroundColor.redF()),
        static_cast<float>(context.theme.viewerBackgroundColor.greenF()),
        static_cast<float>(context.theme.viewerBackgroundColor.blueF())
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
        static_cast<float>(context.theme.accentColor.redF()),
        static_cast<float>(context.theme.accentColor.greenF()),
        static_cast<float>(context.theme.accentColor.blueF())
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

    const float pixelScale = std::max(context.pixelToWorldScale, 1.0e-4f);
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

std::vector<TransientPrimitive> SelectionRenderLayer::buildSnapHighlightPrimitives
(
    const ViewportRenderContext& context,
    const QVector3D& snappedPosition,
    bool objectSnap
) const
{

    const QColor outerColor = objectSnap ? context.theme.selectedControlPointColor : context.theme.accentColor;
    const QColor innerColor = context.theme.viewerBackgroundColor;

    TransientPrimitive outerPoint;
    outerPoint.primitiveType = GL_POINTS;
    outerPoint.color =
    {
        static_cast<float>(outerColor.redF()),
        static_cast<float>(outerColor.greenF()),
        static_cast<float>(outerColor.blueF())
    };
    outerPoint.pointSize = objectSnap ? 15.0f : 13.0f;
    outerPoint.roundPoint = true;
    outerPoint.vertices = { snappedPosition };

    TransientPrimitive innerPoint;
    innerPoint.primitiveType = GL_POINTS;
    innerPoint.color =
    {
        static_cast<float>(innerColor.redF()),
        static_cast<float>(innerColor.greenF()),
        static_cast<float>(innerColor.blueF())
    };
    innerPoint.pointSize = objectSnap ? 7.0f : 5.5f;
    innerPoint.roundPoint = true;
    innerPoint.vertices = { snappedPosition };

    return { outerPoint, innerPoint };
}

bool SelectionRenderLayer::computeHandlePopupLayout
(
    const CadItem* selectedItem,
    const QVector<CadSelectionHandleInfo>& handles,
    const QVector<int>& candidateIndices,
    int activeCandidateOrdinal,
    const QPoint& cursorScreenPos,
    const QSize& viewportSize,
    const QFontMetrics& metrics,
    HandlePopupLayout& layout
) const
{
    if (candidateIndices.size() < 2 || viewportSize.width() <= 0 || viewportSize.height() <= 0)
    {
        return false;
    }

    layout.rowTexts.clear();
    layout.rowTexts.reserve(candidateIndices.size());
    int maxTextWidth = 0;

    for (int ordinal = 0; ordinal < candidateIndices.size(); ++ordinal)
    {
        const int handleIndex = candidateIndices.at(ordinal);

        if (handleIndex < 0 || handleIndex >= handles.size())
        {
            continue;
        }

        const CadSelectionHandleInfo& handle = handles.at(handleIndex);
        const QString rowText = overlappedHandleRowText(selectedItem, handle, ordinal);
        layout.rowTexts.push_back(rowText);
        maxTextWidth = std::max(maxTextWidth, metrics.horizontalAdvance(rowText));
    }

    if (layout.rowTexts.size() < 2)
    {
        return false;
    }

    const int panelPaddingX = 14;
    const int panelPaddingY = 10;
    const int rowHeight = std::max(22, metrics.height() + 8);
    const int desiredPanelWidth = maxTextWidth + panelPaddingX * 2 + 24;
    const int maxPanelWidth = std::max(140, viewportSize.width() - 16);
    const int panelWidth = std::min(desiredPanelWidth, maxPanelWidth);
    const int panelHeight = panelPaddingY * 2 + rowHeight * layout.rowTexts.size();
    QPoint panelTopLeft = cursorScreenPos + QPoint(18, 16);

    if (panelTopLeft.x() + panelWidth > viewportSize.width() - 8)
    {
        panelTopLeft.setX(std::max(8, viewportSize.width() - panelWidth - 8));
    }

    if (panelTopLeft.y() + panelHeight > viewportSize.height() - 8)
    {
        panelTopLeft.setY(std::max(8, viewportSize.height() - panelHeight - 8));
    }

    layout.panelRect = QRect(panelTopLeft, QSize(panelWidth, panelHeight));
    layout.rowRects.clear();
    layout.rowRects.reserve(layout.rowTexts.size());

    for (int ordinal = 0; ordinal < layout.rowTexts.size(); ++ordinal)
    {
        const QRect rowRect
        (
            panelTopLeft.x() + panelPaddingX,
            panelTopLeft.y() + panelPaddingY + ordinal * rowHeight,
            panelWidth - panelPaddingX * 2,
            rowHeight
        );
        layout.rowRects.push_back(rowRect);
    }

    const int badgeRadius = 9;
    const QPoint badgeCenter = cursorScreenPos + QPoint(13, -13);
    layout.badgeRect = QRect
    (
        badgeCenter.x() - badgeRadius,
        badgeCenter.y() - badgeRadius,
        badgeRadius * 2,
        badgeRadius * 2
    );

    Q_UNUSED(activeCandidateOrdinal);
    return true;
}

void SelectionRenderLayer::renderHandlePopup
(
    QPainter& painter,
    const ViewportRenderContext& context,
    const HandlePopupLayout& layout,
    int candidateCount,
    int activeCandidateOrdinal,
    bool popupVisible
) const
{
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    QFont popupFont = painter.font();
    popupFont.setPointSize(9);
    painter.setFont(popupFont);
    const QFontMetrics metrics(popupFont);

    painter.setPen(QPen(QColor(15, 22, 30, 210), 1.0));
    painter.setBrush(QColor(255, 186, 54, 235));
    painter.drawEllipse(layout.badgeRect);
    painter.setPen(QColor(18, 24, 33));
    painter.drawText(layout.badgeRect, Qt::AlignCenter, QString::number(candidateCount));

    if (!popupVisible)
    {
        return;
    }

    painter.setPen(QPen(QColor(94, 176, 255, 230), 1.0));
    painter.setBrush(QColor(20, 26, 34, 228));
    painter.drawRoundedRect(layout.panelRect, 7.0, 7.0);

    const int rowCount = layout.rowRects.size();

    if (rowCount <= 0)
    {
        return;
    }

    const int activeOrdinal = std::clamp(activeCandidateOrdinal, 0, rowCount - 1);

    for (int ordinal = 0; ordinal < rowCount; ++ordinal)
    {
        const QRect rowRect = layout.rowRects.at(ordinal);
        const QString rowText = ordinal < layout.rowTexts.size()
            ? layout.rowTexts.at(ordinal)
            : QStringLiteral("%1. 拉伸点").arg(ordinal + 1);
        const QString displayText = metrics.elidedText(rowText, Qt::ElideRight, std::max(20, rowRect.width() - 8));

        if (ordinal == activeOrdinal)
        {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(84, 166, 255, 96));
            painter.drawRoundedRect(rowRect.adjusted(0, 0, 0, -1), 4.0, 4.0);
        }

        painter.setPen(ordinal == activeOrdinal ? QColor(255, 248, 227) : QColor(220, 230, 242));
        painter.drawText(rowRect.adjusted(4, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft, displayText);
    }

    const QString hintText = QStringLiteral("Tab/Shift+Tab 切换");
    const int hintTop = layout.panelRect.bottom() + 5;
    QRect hintRect
    (
        layout.panelRect.left(),
        hintTop,
        layout.panelRect.width(),
        metrics.height() + 2
    );

    if (hintRect.bottom() > context.viewportHeight - 4)
    {
        hintRect.moveTop(layout.panelRect.top() - hintRect.height() - 4);
    }

    painter.setPen(QColor(150, 172, 194, 228));
    painter.drawText(hintRect, Qt::AlignLeft | Qt::AlignVCenter, hintText);
}