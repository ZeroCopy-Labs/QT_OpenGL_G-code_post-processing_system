#pragma once

#include "application/geometry/GeometrySnapshot.h"
#include "application/process/DocumentProcessState.h"

#include <cstdint>
#include <optional>

namespace cadcam::cam
{
    // 一次 CAM 加工任务的工艺设置。
    // 这些参数属于加工任务输入，与 CAD 几何编辑无关。
    struct CamJobSettings
    {
        // 方管壁厚覆盖值；未设置时按截面圆角半径推导。
        std::optional<double> rotaryTubeWallThicknessOverride;

        // 加工断面正向（+X）修正开关。
        bool cutSurfacePositiveCorrection = false;

        // 加工断面负向（−X）修正开关。
        bool cutSurfaceNegativeCorrection = false;
    };

    // 当前一次 CAM 加工任务的聚合边界。
    //
    // 拥有 CAD 几何快照、加工输入状态、加工设置和当前有效的加工计划，但不拥有 CAD 文档或编辑器，
    // 也不负责 CAD 编辑、几何计算、快照构建、排序算法、轨迹计算、G-code 输出、视图绘制和配置持久化。
    //
    // 几何快照是 CAD → CAM 的数据边界：它描述 CAM 看到的 CAD 几何，与文档内容 revision 对应。
    // 加工计划是派生结果：CAD 内容或加工状态变化后即失效，需要重新规划才能再次有效。
    class CamJob
    {
    public:
        process::DocumentProcessState& processState() { return m_processState; }
        const process::DocumentProcessState& processState() const { return m_processState; }

        CamJobSettings& settings() { return m_settings; }
        const CamJobSettings& settings() const { return m_settings; }

        const std::optional<GeometrySnapshot>& geometrySnapshot() const { return m_geometrySnapshot; }

        bool hasGeometrySnapshot() const { return m_geometrySnapshot.has_value(); }

        // 几何快照是否与给定文档内容 revision 对应。
        bool isGeometrySnapshotCurrent(std::uint64_t contentRevision) const;

        // 只接收已编译完成的快照；构建快照由应用层编排负责。
        void setGeometrySnapshot(GeometrySnapshot snapshot);

        void invalidateGeometrySnapshot();

        const std::optional<planning::ProcessPlan>& processPlan() const { return m_processPlan; }

        bool hasProcessPlan() const { return m_processPlan.has_value(); }

        // 当前加工计划是否匹配给定的文档内容 revision 与当前加工状态 revision。
        bool isProcessPlanCurrent(std::uint64_t contentRevision) const;

        void setProcessPlan(planning::ProcessPlan plan);
        void invalidateProcessPlan();

    private:
        process::DocumentProcessState m_processState;
        CamJobSettings m_settings;
        std::optional<GeometrySnapshot> m_geometrySnapshot;
        std::optional<planning::ProcessPlan> m_processPlan;
    };
}
