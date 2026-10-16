#pragma once

#include <QVector>
#include <QVector3D>

#include "application/process/DocumentProcessState.h"
#include "application/process/ProcessPresentationSnapshot.h"
#include "cad/view/rendering/CadSelectionHandle.h"

class CadItem;

struct CadProcessVisualInfo
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

enum class CadProcessExclusionVisual
{
    None,
    PlannedExclusion
};

bool isProcessVisualizable(const CadItem* item);

CadProcessExclusionVisual resolveProcessExclusionVisual
(
    const CadItem* item,
    const cadcam::process::ProcessPresentationSnapshot* presentation
);

CadProcessVisualInfo buildProcessVisualInfo
(
    const CadItem* item,
    const cadcam::process::ProcessPresentationEntry* presentation = nullptr
);

QVector<CadSelectionHandleInfo> buildSelectionHandleInfo(const CadItem* item, float xlineHandleLength = 50.0f);
