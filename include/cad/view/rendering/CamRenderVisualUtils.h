#pragma once

#include <QVector>
#include <QVector3D>

#include "application/process/ProcessPresentationSnapshot.h"

class CadItem;

// CAM 可视化的几何信息：加工顺序、方向、起点/终点与标签锚点。
struct CamProcessVisualInfo
{
    bool valid = false;
    bool closedPath = false;
    int processOrder = -1;
    bool isReverse = false;
    QVector3D forwardStartPoint;
    QVector3D forwardEndPoint;
    QVector3D startPoint;
    QVector3D endPoint;
    QVector3D direction;
    QVector3D labelAnchor;
};

// 加工展示中的排除可视化状态。
enum class CamProcessExclusionVisual
{
    None,
    PlannedExclusion
};

// 该图元是否属于可生成加工可视化的几何类型。
bool isProcessVisualizable(const CadItem* item);

// 图元在加工展示快照中是否被排除。
CamProcessExclusionVisual resolveCamProcessExclusionVisual
(
    const CadItem* item,
    const cadcam::process::ProcessPresentationSnapshot* presentation
);

// 由 CAD 几何和加工展示条目推导方向可视化信息。
CamProcessVisualInfo buildCamProcessVisualInfo
(
    const CadItem* item,
    const cadcam::process::ProcessPresentationEntry* presentation = nullptr
);