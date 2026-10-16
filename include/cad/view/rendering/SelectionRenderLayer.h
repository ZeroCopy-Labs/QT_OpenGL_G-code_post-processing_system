// SelectionRenderLayer 头文件
// CAD 选择可视化层：选中与框选候选高亮、框选窗口预览、选中手柄、吸附高亮、重叠手柄弹框。
// 只处理 CAD 选择与交互显示，不含加工状态、加工计划或加工单元。
#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <QFontMetrics>
#include <QMatrix4x4>
#include <QPainter>
#include <QPoint>
#include <QRect>
#include <QSet>
#include <QSize>
#include <QVector>
#include <QVector3D>

#include "cad/view/rendering/CadRenderTypes.h"
#include "cad/view/rendering/CadSelectionHandle.h"
#include "cad/view/rendering/ViewportRenderContext.h"
#include "desktop/AppTheme.h"

class CadItem;

class SelectionRenderLayer
{
public:

    // 选中实体与框选候选的叠加高亮（屏幕空间）。
    void renderSelectionOverlays
    (
        QPainter& painter,
        const ViewportRenderContext& context,
        const std::vector<std::unique_ptr<CadItem>>& entities,
        const QSet<RenderEntityKey>& selectedRenderKeys,
        const QSet<RenderEntityKey>& windowPreviewRenderKeys
    ) const;

    // 框选窗口预览（屏幕空间）。
    void renderSelectionWindow
    (
        QPainter& painter,
        const QRect& selectionRect,
        bool crossingSelection
    ) const;

    // 选中图元的基点/控制点手柄（世界空间图元）。
    std::vector<TransientPrimitive> buildHandlePrimitives
    (
        const ViewportRenderContext& context,
        const CadItem* selectedItem,
        int hoveredHandleIndex
    ) const;

    // 吸附命中高亮（世界空间图元）。
    std::vector<TransientPrimitive> buildSnapHighlightPrimitives
    (
        const ViewportRenderContext& context,
        const QVector3D& snappedPosition,
        bool objectSnap
    ) const;

    // 重叠手柄选择框布局。
    struct HandlePopupLayout
    {
        QRect panelRect;
        QVector<QRect> rowRects;
        QVector<QString> rowTexts;
        QRect badgeRect;
    };

    bool computeHandlePopupLayout
    (
        const CadItem* selectedItem,
        const QVector<CadSelectionHandleInfo>& handles,
        const QVector<int>& candidateIndices,
        int activeCandidateOrdinal,
        const QPoint& cursorScreenPos,
        const QSize& viewportSize,
        const QFontMetrics& metrics,
        HandlePopupLayout& layout
    ) const;

    // 重叠手柄选择框（屏幕空间）。
    void renderHandlePopup
    (
        QPainter& painter,
        const ViewportRenderContext& context,
        const HandlePopupLayout& layout,
        int candidateCount,
        int activeCandidateOrdinal,
        bool popupVisible
    ) const;
};
