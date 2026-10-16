#pragma once

#include "core/geometry/GeometryTypes.h"

#include <functional>
#include <memory>
#include <vector>

// 一次 CAD 编辑完成后的实体身份变化。
// 只使用稳定的 Core 图元身份，不涉及任何加工业务类型。
struct CadEditedEntities
{
    std::vector<cadcam::geometry::EntityId> removedEntityIds;
    std::vector<cadcam::geometry::EntityId> addedEntityIds;
};

// 应用层可注入的编辑伴随器。
// CAD 命令执行和撤销完成后各调用一次，使应用层自己的状态同步与 CAD 改动共享
// 同一个 Undo / Redo 历史项；CadEditer 不解释伴随器维护什么状态。
class CadEditCompanion
{
public:
    virtual ~CadEditCompanion() = default;

    // @param edited 本次 CAD 改动移除和新增的图元身份
    // @param undo true 表示当前处于撤销，false 表示执行或重做
    virtual void sync(const CadEditedEntities& edited, bool undo) = 0;
};

// 为一次编辑创建伴随器。
// @param sourceEntityIds 本次编辑涉及（删除或替换）的原始图元身份
// @return 返回 nullptr 表示本次编辑不需要伴随同步
using CadEditCompanionFactory = std::function
<
    std::unique_ptr<CadEditCompanion>
    (
        const std::vector<cadcam::geometry::EntityId>& sourceEntityIds
    )
>;
