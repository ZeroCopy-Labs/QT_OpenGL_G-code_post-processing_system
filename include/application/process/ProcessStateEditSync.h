#pragma once

#include "application/process/DocumentProcessState.h"
#include "cad/editing/CadEditCompanion.h"

#include <optional>
#include <utility>
#include <vector>

namespace cadcam::process
{
    // 把 CAD 编辑引起的图元变化同步到 DocumentProcessState。
    //
    // 桌面层通过 CadEditer 的编辑伴随工厂注入本类，使 CAD 删除/替换与加工状态清理、
    // 加工单元序列恢复共享同一个 Undo / Redo 历史项，而不是各自产生一个历史项。
    //
    // 语义与迁移前保持一致：
    // - 创建时快照源图元加工状态和改动前的加工单元序列；
    // - 执行（含重做）时清除被删除/替换图元的状态，并复用首次执行后记录的单元序列；
    // - 撤销时清除替换产生的新图元状态，恢复源图元状态和改动前的单元序列。
    class ProcessStateEditSync final : public CadEditCompanion
    {
    public:
        ProcessStateEditSync
        (
            DocumentProcessState& state,
            const std::vector<geometry::EntityId>& sourceEntityIds
        );

        void sync(const CadEditedEntities& edited, bool undo) override;

    private:
        DocumentProcessState& m_state;
        std::vector<std::pair<geometry::EntityId, std::optional<EntityProcessState>>> m_sourceStates;
        std::optional<planning::ProcessUnitSequence> m_beforeSequence;
        std::optional<planning::ProcessUnitSequence> m_afterSequence;
    };
}
