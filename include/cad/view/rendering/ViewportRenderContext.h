// ViewportRenderContext 头文件
// 共享视口提供给各渲染层的只读上下文：只包含 CAD / 视口事实，不含加工状态。
#pragma once

#include <functional>

#include <QFont>
#include <QMatrix4x4>
#include <QPoint>
#include <QVector3D>

#include "cad/view/rendering/CadRenderTypes.h"
#include "desktop/AppTheme.h"

struct ViewportRenderContext
{
    // 世界坐标到屏幕坐标。
    std::function<QPoint(const QVector3D&)> worldToScreen;
    // 当前相机矩阵与方向。
    QMatrix4x4 viewProjection;
    QVector3D viewForward;
    QVector3D viewRight;
    // 1 个屏幕像素对应的世界长度。
    float pixelToWorldScale = 1.0f;
    // 视口尺寸与覆盖层字体。
    int viewportWidth = 1;
    int viewportHeight = 1;
    QFont overlayFont;
    AppThemeColors theme;
    // 视图交互是否空闲；非空闲时暂停覆盖层。
    bool interactionIdle = true;
    // 共享视口当前的选中主键。
    RenderEntityKey primarySelectionKey;
    // 构造线手柄的世界长度。
    float xlineHandleWorldLength = 50.0f;
};
