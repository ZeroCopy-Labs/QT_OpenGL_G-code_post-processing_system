#pragma once

#include "infrastructure/config/GProfile.h"
#include "core/diagnostics/OperationContext.h"
#include "core/diagnostics/OperationResult.h"
#include "core/machining/TubeSection.h"
#include "core/planning/ProcessPlan.h"
#include "application/geometry/GeometrySnapshot.h"
#include "application/process/DocumentProcessState.h"

#include <QString>

// NC 导出的编排层：组织 NC 生成与后处理调用，并传递诊断。
//
// 只负责调用顺序和参数组织：
// - 几何来自几何快照；
// - NC 生成由 NcProgramService 完成；
// - 文本由 GCodePostProcessor 完成；
// - 文件写入由 GCodeFileWriter 完成，由桌面层负责路径选择与用户交互。
class GGenerator
{
public:
    enum class GenerationMode
    {
        Mode2D,
        Mode3D
    };

public:
    GGenerator();

    void setProfile(GProfile* profile);
    GProfile* profile() const;

    void setGenerationMode(GenerationMode generationMode);
    GenerationMode generationMode() const;

    void setRotaryTubeCenter(double centerY, double centerZ, bool valid);
    void setProcessPlan(const cadcam::planning::ProcessPlan* processPlan);
    void setProcessState(const cadcam::process::DocumentProcessState* processState);

    // 几何快照：NC 生成的几何输入，不再从 CAD 文档重新捕获几何。
    void setGeometrySnapshot(const GeometrySnapshot* snapshot);
    void setCutSurfaceCorrection(double wallThickness, bool positive, bool negative)
    {
        m_tubeWallThickness = wallThickness;
        m_cutSurfacePositiveCorrection = positive;
        m_cutSurfaceNegativeCorrection = negative;
    }
    void setTubeSectionModel
    (
        const std::optional<cadcam::machining::TubeSectionModel>& tubeSectionModel
    );

    OperationResult<QString> buildProgramText(const OperationContext& context) const;

private:
    OperationResult<QString> buildRotaryProgramText(const OperationContext& context) const;

    GProfile m_defaultProfile;
    GProfile* m_profile = nullptr;
    GenerationMode m_generationMode = GenerationMode::Mode2D;
    double m_rotaryTubeCenterY = 0.0;
    double m_tubeWallThickness = 0.0;
    bool m_cutSurfacePositiveCorrection = false;
    bool m_cutSurfaceNegativeCorrection = false;
    double m_rotaryTubeCenterZ = 0.0;
    bool m_rotaryTubeCenterValid = false;
    const cadcam::planning::ProcessPlan* m_processPlan = nullptr;
    const cadcam::process::DocumentProcessState* m_processState = nullptr;
    const GeometrySnapshot* m_geometrySnapshot = nullptr;
    std::optional<cadcam::machining::TubeSectionModel> m_tubeSectionModel;
};
