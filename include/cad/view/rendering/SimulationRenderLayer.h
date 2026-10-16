// SimulationRenderLayer 头文件
// 仿真可视化层：当前机床位置、当前运动、已完成轨迹与仿真状态文字。
// 只消费 Simulation 已经整理好的展示数据，不读取 CAD、CamJob、ProcessPlan 或加工状态。
#pragma once

#include <optional>
#include <vector>

#include <QPainter>

#include "application/simulation/SimulationTypes.h"
#include "cad/view/rendering/CadRenderTypes.h"
#include "cad/view/rendering/ViewportRenderContext.h"

class SimulationRenderLayer
{
public:
    // 设置本次仿真会话的轨迹展示数据；只保存副本，不引用控制器。
    void setTrajectory(const cadcam::simulation::SimulationTrajectory& trajectory);
    void clearTrajectory();

    // 每帧的仿真状态。
    void setSnapshot(const cadcam::simulation::SimulationSnapshot& snapshot);
    const cadcam::simulation::SimulationSnapshot& snapshot() const { return m_snapshot; }

    void setVisible(bool visible);
    bool visible() const { return m_visible; }
    bool hasContent() const { return m_trajectory.has_value(); }

    // 世界空间图元：已完成轨迹、当前运动段与刀具标记。
    std::vector<TransientPrimitive> buildPrimitives(const ViewportRenderContext& context) const;

    // 屏幕空间仿真状态文字。
    void paintStatusOverlay(QPainter& painter, const ViewportRenderContext& context) const;

private:
    std::optional<cadcam::simulation::SimulationTrajectory> m_trajectory;
    cadcam::simulation::SimulationSnapshot m_snapshot;
    bool m_visible = true;
};
