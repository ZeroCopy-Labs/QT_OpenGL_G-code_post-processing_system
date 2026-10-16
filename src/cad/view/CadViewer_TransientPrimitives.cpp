// CadViewer 临时图元构建实现
#include "platform/pch.h"

#include "cad/view/CadViewer.h"

#include "cad/document/CadDocument.h"
#include "cad/items/CadItem.h"
#include "cad/view/rendering/CadPreviewBuilder.h"
#include "cad/view/rendering/CadSelectionVisualUtils.h"
#include "cad/view/rendering/SelectionRenderLayer.h"

#include <memory>
#include <vector>

std::vector<TransientPrimitive> CadViewer::buildTransientPrimitives() const
{
    return CadPreviewBuilder::buildTransientPrimitives(m_controller.drawState(), selectedEntity(), selectedEntities());
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

    return m_selectionRenderLayer->buildHandlePrimitives
    (
        makeViewportRenderContext(),
        selectedItem,
        resolveHoveredHandleIndex(handles)
    );
}