// CamRenderLayer 头文件
// CAM 可视化层：加工顺序编号、方向箭头、排除淡化、旋转端面标签与预览、加工单元命中与选择扩展。
// 与 CAD 层共享同一个 OpenGL 视口；只读取加工状态，只产生交互请求。
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include <QObject>
#include <QFont>
#include <QPoint>
#include <QRect>
#include <QSet>
#include <QString>
#include <QVector>
#include <QVector3D>

#include "application/process/ProcessPresentationSnapshot.h"
#include "cad/view/rendering/CadRenderTypes.h"
#include "cad/view/rendering/ViewportRenderContext.h"
#include "core/machine/RotaryKinematics.h"
#include "core/machining/TubeSection.h"
#include "core/planning/ProcessPlan.h"
#include "desktop/AppTheme.h"

class CadItem;
class QPainter;

namespace cadcam::process
{
    class DocumentProcessState;
}

// CAM 可视化的状态与绘制入口。
// 生命周期：有效 ProcessPlan 生成展示快照后由桌面层写入；计划失效时清除。
class CamRenderLayer : public QObject
{
    Q_OBJECT

public:

    // 加工单元选择扩展结果：RenderEntityKey 分组，不含 CAD 与加工状态修改。
    struct SelectionExpansion
    {
        QSet<RenderEntityKey> expandedRenderKeys;
        std::vector<QSet<RenderEntityKey>> processUnitRenderKeyGroups;
        QSet<RenderEntityKey> unassignedRenderKeys;
    };

    explicit CamRenderLayer(QObject* parent = nullptr);

    // 绑定当前文档的 CAD 图元（只读）；文档切换时由共享视口重新绑定。
    void setSceneEntities(const std::vector<std::unique_ptr<CadItem>>* entities);

    // ---- 只读输入 ----

    // 加工状态由 CamJob 持有，本层只读。
    void setProcessState(const cadcam::process::DocumentProcessState* processState);

    // 接管展示快照所有权：计划有效时写入，计划失效时清除。
    void setPresentation(cadcam::process::ProcessPresentationSnapshot presentation);
    void clearPresentation();
    bool hasPresentation() const;

    // ---- 画布显示开关（持久化仍由桌面层设置管理） ----

    void setVisualsVisible(bool visible);
    void setDirectionVisible(bool visible);
    void setOrderVisible(bool visible);
    void setRotaryEndCutsVisible(bool visible);
    void setRotaryLaserWallImpactVisible(bool visible);
    void setRotaryExpectedSurfaceVisible(bool visible);
    void setExcludedEntitiesDimmed(bool dimmed);
    void setRotaryEndCutPreview
    (
        const std::optional<cadcam::machining::TubeSectionModel>& section,
        double wallThickness,
        const cadcam::machine::RotaryMachinePolicy& machinePolicy
    );

    // ---- 每帧可视化 ----

    // 被排除图元的显示覆盖（颜色与透明度）；无覆盖时返回空表。
    CadRenderOverrideMap buildRenderOverrides(const ViewportRenderContext& context) const;

    // 加工方向箭头、旋转端面与预览走线。
    std::vector<TransientPrimitive> buildOverlayPrimitives(const ViewportRenderContext& context);

    // 加工顺序编号与端面标签（屏幕空间）。
    void paintScreenOverlays(QPainter& painter, const ViewportRenderContext& context);

    // ---- 命中与交互 ----

    void updateHover(const ViewportRenderContext& context, const QPoint& screenPos);
    void clearHover();

    // 加工顺序标签单击：整组选择或请求块内移尾。
    bool handleClick(const ViewportRenderContext& context, const QPoint& screenPos, Qt::KeyboardModifiers modifiers);

    // 加工顺序标签或箭头双击：请求整组反向。
    bool handleDoubleClick(const ViewportRenderContext& context, const QPoint& screenPos);

    // 命中标签或箭头时上报单元键，供桌面层创建端面语义菜单。
    bool handleContextMenuPress
    (
        const ViewportRenderContext& context,
        const QPoint& screenPos,
        const QPoint& globalPos
    );

    // 把命中图元扩展为完整加工单元选择。
    SelectionExpansion expandRenderKeysToProcessUnits
    (
        const ViewportRenderContext& context,
        const QSet<RenderEntityKey>& renderKeys
    ) const;

signals:
    // 块内移尾请求：桌面层负责修改加工单元序列。
    void processUnitMoveToBackRequested
    (
        QVector<cadcam::planning::ProcessUnitKey> selectedUnitKeys,
        cadcam::planning::ProcessUnitKey targetUnitKey
    );
    void processUnitReverseRequested(cadcam::planning::ProcessUnitKey unitKey);
    void processUnitContextMenuRequested(cadcam::planning::ProcessUnitKey unitKey, const QPoint& globalPos);

    // 共享视口执行的选择变更请求。
    void selectionRequested
    (
        QSet<RenderEntityKey> renderKeys,
        RenderEntityKey preferredRenderKey,
        bool toggle
    );
    void commandMessageRequested(const QString& message);

    // 旋转端面预览状态文本，由桌面层显示。
    void rotaryEndCutPreviewStatusChanged(const QString& status);

private:
    struct ProcessOrderLabelOverlay
    {
        cadcam::planning::ProcessUnitKey unitKey;
        int unitOrder = -1;
        bool hovered = false;
        bool selected = false;
        QPoint center;
        QRect bubbleRect;
        QString text;
    };

    struct ProcessUnitArrowOverlay
    {
        cadcam::planning::ProcessUnitKey unitKey;
        int unitOrder = -1;
        bool reverse = false;
        bool selected = false;
        QVector3D startPoint;
        QVector3D direction;
        QRect hitRect;
    };

    std::vector<ProcessOrderLabelOverlay> buildProcessOrderLabelOverlays(const ViewportRenderContext& context) const;
    std::vector<ProcessUnitArrowOverlay> buildProcessUnitArrowOverlays(const ViewportRenderContext& context) const;
    bool hitTestProcessOrderLabel
    (
        const ViewportRenderContext& context,
        const QPoint& screenPos,
        ProcessOrderLabelOverlay* outLabel = nullptr
    ) const;
    bool hitTestProcessUnitArrow
    (
        const ViewportRenderContext& context,
        const QPoint& screenPos,
        ProcessUnitArrowOverlay* outArrow = nullptr
    ) const;

    // 已绑定的 CAD 图元；未绑定时返回空容器。
    const std::vector<std::unique_ptr<CadItem>>& entities() const;

    // 按需重建旋转端面预览走线。
    void refreshPreviewPaths(const ViewportRenderContext& context);

    // 根据当前截面和加工断面状态重建激光内壁作用点及期望断面内侧走线。
    void rebuildRotaryEndCutPreviewPaths(const ViewportRenderContext& context);
    void renderProcessOrderLabels(QPainter& painter, const ViewportRenderContext& context) const;
    void renderRotaryEndCutLabels(QPainter& painter, const ViewportRenderContext& context) const;
    std::vector<TransientPrimitive> buildProcessArrowPrimitives(const ViewportRenderContext& context) const;
    std::vector<TransientPrimitive> buildRotaryEndCutPrimitives(const ViewportRenderContext& context) const;

    const std::vector<std::unique_ptr<CadItem>>* m_entities = nullptr;
    const cadcam::process::DocumentProcessState* m_processState = nullptr;
    std::optional<cadcam::process::ProcessPresentationSnapshot> m_presentation;
    std::optional<cadcam::planning::ProcessUnitKey> m_hoveredProcessUnitKey;

    bool m_visualsVisible = true;
    bool m_directionVisible = true;
    bool m_orderVisible = true;
    bool m_rotaryEndCutsVisible = true;
    bool m_rotaryLaserWallImpactVisible = true;
    bool m_rotaryExpectedSurfaceVisible = true;
    bool m_excludedEntitiesDimmed = true;
    std::optional<cadcam::machining::TubeSectionModel> m_rotaryEndCutPreviewSection;
    double m_rotaryTubeWallThickness = 0.0;
    cadcam::machine::RotaryMachinePolicy m_rotaryEndCutPreviewMachinePolicy;
    std::vector<QVector<QVector3D>> m_rotaryEndCutLaserWallImpactPreviewPaths;
    std::vector<QVector<QVector3D>> m_rotaryEndCutExpectedSurfacePreviewPaths;
    bool m_previewPathsDirty = true;
};
