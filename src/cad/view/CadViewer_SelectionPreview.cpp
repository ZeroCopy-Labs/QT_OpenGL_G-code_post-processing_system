// CadViewer 框选预览与选中高亮实现
#include "platform/pch.h"

#include "cad/view/CadViewer.h"

#include "cad/document/CadDocument.h"
#include "cad/view/interaction/CadEntityPicker.h"
#include "cad/items/CadItem.h"
#include "cad/view/CadViewerUtils.h"
#include "cad/view/rendering/CamRenderLayer.h"
#include "cad/view/rendering/SelectionRenderLayer.h"

#include <QPainter>
#include <QPen>
#include <QPolygonF>

#include <memory>
#include <vector>

namespace
{
    constexpr int kWindowSelectionMinimumPixels = 2;

    QRect normalizedSelectionRect(const QPoint& anchorScreenPos, const QPoint& currentScreenPos)
    {
        return QRect(anchorScreenPos, currentScreenPos).normalized();
    }
}

void CadViewer::updateSelectionWindowPreviewCandidates()
{
    m_windowPreviewRenderKeys.clear();

    if (!m_selectionWindowPreview.visible)
    {
        return;
    }

    CadDocument* scene = m_sceneCoordinator.document();

    if (scene == nullptr)
    {
        return;
    }

    const QRect selectionRect = normalizedSelectionRect
    (
        m_selectionWindowPreview.anchorScreenPos,
        m_selectionWindowPreview.currentScreenPos
    );

    if (selectionRect.width() < kWindowSelectionMinimumPixels || selectionRect.height() < kWindowSelectionMinimumPixels)
    {
        return;
    }

    const std::vector<RenderEntityKey> previewRenderKeys = CadEntityPicker::pickEntitiesByWindow
    (
        scene->m_entities,
        m_camera.viewProjectionMatrix(aspectRatio()),
        m_viewportWidth,
        m_viewportHeight,
        QRectF(selectionRect),
        m_selectionWindowPreview.crossingSelection
    );

    for (RenderEntityKey renderKey : previewRenderKeys)
    {
        if (renderKey.valid())
        {
            m_windowPreviewRenderKeys.insert(renderKey);
        }
    }

    // 加工单元预览扩展同样由 CAM 可视化层完成。
    if (m_selectionWindowPreview.processUnitSelection)
    {
        m_windowPreviewRenderKeys = m_camRenderLayer->expandRenderKeysToProcessUnits
        (
            makeViewportRenderContext(),
            m_windowPreviewRenderKeys
        ).expandedRenderKeys;
    }
}

void CadViewer::renderEntitySelectionOverlays()
{
    CadDocument* scene = m_sceneCoordinator.document();

    if (scene == nullptr || interactionMode() != ViewInteractionMode::Idle)
    {
        return;
    }

    if (m_selectedRenderKeys.isEmpty() && m_windowPreviewRenderKeys.isEmpty())
    {
        return;
    }

    QPainter painter(this);
    m_selectionRenderLayer->renderSelectionOverlays
    (
        painter,
        makeViewportRenderContext(),
        scene->m_entities,
        m_selectedRenderKeys,
        m_windowPreviewRenderKeys
    );
}

void CadViewer::renderCamScreenOverlays()
{
    if (interactionMode() != ViewInteractionMode::Idle)
    {
        return;
    }

    QPainter painter(this);
    m_camRenderLayer->paintScreenOverlays(painter, makeViewportRenderContext());
}

void CadViewer::renderSelectionWindowPreview()
{
    if (!m_selectionWindowPreview.visible || interactionMode() != ViewInteractionMode::Idle)
    {
        return;
    }

    const QRect selectionRect = normalizedSelectionRect
    (
        m_selectionWindowPreview.anchorScreenPos,
        m_selectionWindowPreview.currentScreenPos
    );

    if (selectionRect.width() < kWindowSelectionMinimumPixels || selectionRect.height() < kWindowSelectionMinimumPixels)
    {
        return;
    }

    QPainter painter(this);
    m_selectionRenderLayer->renderSelectionWindow
    (
        painter,
        selectionRect,
        m_selectionWindowPreview.crossingSelection
    );
}