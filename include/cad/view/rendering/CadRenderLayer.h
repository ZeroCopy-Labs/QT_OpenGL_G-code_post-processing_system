// CadRenderLayer 头文件
// CAD 几何渲染层：把缓存好的 CAD 图元提交给 OpenGL 绘制。
// 只处理 CAD 几何、GPU 缓冲、显示颜色和上层给出的中立显示覆盖。
#pragma once

#include <memory>
#include <vector>

#include <QMatrix4x4>

#include "cad/view/rendering/CadRenderTypes.h"
#include "desktop/AppTheme.h"

class CadItem;
class CadSceneRenderCache;
class QOpenGLShaderProgram;

class CadRenderLayer
{
public:
    // 单帧 CAD 几何绘制所需的视口上下文。
    struct Frame
    {
        QOpenGLShaderProgram* shader = nullptr;
        QMatrix4x4 viewProjection;
        const std::vector<std::unique_ptr<CadItem>>* entities = nullptr;
        CadSceneRenderCache* renderCache = nullptr;
        RenderEntityKey selectedRenderKey;
        AppThemeColors theme;
        // 上层计算好的中立显示覆盖；为空表示全部按实体自身显示参数绘制。
        const CadRenderOverrideMap* overrides = nullptr;
    };

    // 绘制当前帧的全部 CAD 几何。
    void render(const Frame& frame) const;
};
