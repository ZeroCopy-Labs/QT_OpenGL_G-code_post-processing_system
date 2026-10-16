#include "platform/pch.h"

#include "cad/view/rendering/CadRenderLayer.h"

#include "cad/view/rendering/CadEntityRenderer.h"

void CadRenderLayer::render(const Frame& frame) const
{
    if (frame.shader == nullptr || frame.entities == nullptr || frame.renderCache == nullptr)
    {
        return;
    }

    CadEntityRenderer::renderEntities
    (
        *frame.shader,
        frame.viewProjection,
        *frame.entities,
        *frame.renderCache,
        frame.selectedRenderKey,
        frame.theme,
        frame.overrides
    );
}
