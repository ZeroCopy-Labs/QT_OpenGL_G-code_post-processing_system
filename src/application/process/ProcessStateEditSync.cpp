#include "platform/pch.h"

#include "application/process/ProcessStateEditSync.h"

namespace cadcam::process
{
    ProcessStateEditSync::ProcessStateEditSync
    (
        DocumentProcessState& state,
        const std::vector<geometry::EntityId>& sourceEntityIds
    )
        : m_state(state)
    {
        m_sourceStates.reserve(sourceEntityIds.size());

        for (geometry::EntityId entityId : sourceEntityIds)
        {
            std::optional<EntityProcessState> saved;

            if (const EntityProcessState* state2 = m_state.find(entityId))
            {
                saved = *state2;
            }

            m_sourceStates.emplace_back(entityId, std::move(saved));
        }

        m_beforeSequence = m_state.processUnitSequence();
    }

    void ProcessStateEditSync::sync(const CadEditedEntities& edited, bool undo)
    {
        m_state.beginBatch();

        if (!undo)
        {
            // 执行或重做：清除被删除/替换图元的加工状态，并在重做时恢复首次执行后的单元序列。
            for (geometry::EntityId entityId : edited.removedEntityIds)
            {
                m_state.erase(entityId);
            }

            if (m_afterSequence.has_value())
            {
                m_state.setProcessUnitSequence(m_afterSequence->units);
            }

            m_state.endBatch();

            if (!m_afterSequence.has_value())
            {
                m_afterSequence = m_state.processUnitSequence();
            }

            return;
        }

        // 撤销：清除替换产生的新图元状态，恢复源图元加工状态和改动前的单元序列。
        for (geometry::EntityId entityId : edited.removedEntityIds)
        {
            m_state.erase(entityId);
        }

        for (const auto& entry : m_sourceStates)
        {
            if (entry.second.has_value())
            {
                m_state.setState(entry.first, *entry.second);
            }
        }

        if (m_beforeSequence.has_value())
        {
            m_state.setProcessUnitSequence(m_beforeSequence->units);
        }

        m_state.endBatch();
    }
}
