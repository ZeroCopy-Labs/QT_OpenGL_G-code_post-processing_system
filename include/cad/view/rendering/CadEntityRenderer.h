// CadEntityRenderer 头文件
// 声明 CadEntityRenderer 模块，对外暴露当前组件的核心类型、接口和协作边界。
// 实体渲染模块，负责把缓存好的图元数据提交给 OpenGL 绘制。
#pragma once

#include <memory>
#include <vector>

#include <QMatrix4x4>
#include <QOpenGLShaderProgram>

#include "desktop/AppTheme.h"
#include "cad/view/rendering/CadRenderTypes.h"

class CadItem;
class CadSceneRenderCache;

// CAD 实体底层绘制：只消费 CAD 图元、GPU 缓冲和中立的显示覆盖。
class CadEntityRenderer
{
public:
    // 绘制场景实体
    // 遍历场景中的所有图元，按缓存好的 GPU 资源逐个发起绘制
    // selectedRenderKey 用于在绘制阶段直接叠加选中高亮效果
    // overrides 由上层计算，只描述最终颜色与透明度
    // @param shader 通用绘制 Shader
    // @param mvp 当前视图使用的模型视图投影矩阵
    // @param entities 场景实体列表
    // @param sceneRenderCache 实体对应的 GPU 缓冲缓存
    // @param selectedRenderKey 当前选中的 Viewer 实体键
    // @param theme 当前主题颜色
    // @param overrides 实体显示覆盖，可为空
    static void renderEntities
    (
        QOpenGLShaderProgram& shader,
        const QMatrix4x4& mvp,
        const std::vector<std::unique_ptr<CadItem>>& entities,
        CadSceneRenderCache& sceneRenderCache,
        RenderEntityKey selectedRenderKey,
        const AppThemeColors& theme,
        const CadRenderOverrideMap* overrides
    );
};