#pragma once

#include <QVector3D>

// 选择夹点的形状与位置描述。
// 属于 CAD 视图的交互类型，与加工可视化无关，供编辑器与控制器共同使用。
enum class CadSelectionHandleShape
{
    RoundPoint,
    Triangle
};

struct CadSelectionHandleInfo
{
    QVector3D position;
    bool isBasePoint = false;
    bool editable = false;
    int pointIndex = -1;
    CadSelectionHandleShape shape = CadSelectionHandleShape::RoundPoint;
    QVector3D direction;
};
