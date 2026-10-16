#pragma once

#include <QVector>

#include "cad/view/rendering/CadSelectionHandle.h"

class CadItem;

// CAD 选择可视化工具：由 CAD 图元推导基点、控制点和方向手柄。
// 不涉及加工状态、加工计划、加工单元或展示快照。
QVector<CadSelectionHandleInfo> buildSelectionHandleInfo
(
    const CadItem* item,
    float xlineHandleLength = 50.0f
);