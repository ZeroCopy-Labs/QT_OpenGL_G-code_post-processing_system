#include "platform/pch.h"

#include "application/export/GGenerator.h"
#include "application/nc/NcProgramService.h"
#include "infrastructure/nc/GCodePostProcessor.h"

namespace
{

    Diagnostic makeGeneratorDiagnostic
    (
        const OperationContext& context,
        DiagnosticCode code,
        DiagnosticSeverity severity,
        const QString& operation,
        const QString& stage,
        const QString& userMessage,
        const QString& technicalDetail = QString(),
        const QVariantMap& diagnosticContext = QVariantMap()
    )
    {
        Diagnostic diagnostic;
        diagnostic.code = code;
        diagnostic.severity = severity;
        diagnostic.component = QStringLiteral("GGenerator");
        diagnostic.operation = operation;
        diagnostic.stage = stage;
        diagnostic.userMessage = userMessage;
        diagnostic.technicalDetail = technicalDetail;
        diagnostic.correlationId = context.correlationId;
        diagnostic.context = diagnosticContext;
        return diagnostic;
    }


}

GGenerator::GGenerator()
    : m_defaultProfile(GProfile::createDefaultLaserProfile())
    , m_profile(&m_defaultProfile)
{
}


void GGenerator::setProfile(GProfile* profile)
{
    m_profile = profile != nullptr ? profile : &m_defaultProfile;
}

GProfile* GGenerator::profile() const
{
    return m_profile;
}

void GGenerator::setGenerationMode(GenerationMode generationMode)
{
    m_generationMode = generationMode;
}

GGenerator::GenerationMode GGenerator::generationMode() const
{
    return m_generationMode;
}

void GGenerator::setRotaryTubeCenter(double centerY, double centerZ, bool valid)
{
    m_rotaryTubeCenterY = centerY;
    m_rotaryTubeCenterZ = centerZ;
    m_rotaryTubeCenterValid = valid;
}

void GGenerator::setProcessPlan(const cadcam::planning::ProcessPlan* processPlan)
{
    m_processPlan = processPlan;
}

void GGenerator::setGeometrySnapshot(const GeometrySnapshot* snapshot)
{
    m_geometrySnapshot = snapshot;
}

void GGenerator::setProcessState(const cadcam::process::DocumentProcessState* processState)
{
    m_processState = processState;
}

void GGenerator::setTubeSectionModel
(
    const std::optional<cadcam::machining::TubeSectionModel>& tubeSectionModel
)
{
    m_tubeSectionModel = tubeSectionModel;
}


OperationResult<QString> GGenerator::buildRotaryProgramText(const OperationContext& context) const
{
    OperationResult<QString> result;
    if (m_geometrySnapshot == nullptr || m_geometrySnapshot->entries.empty())
    {
        result.status = OperationStatus::InvalidInput;
        result.addDiagnostic(makeGeneratorDiagnostic
        (
            context,
            DiagnosticCode::InvalidGeometry,
            DiagnosticSeverity::Error,
            QStringLiteral("BuildRotaryProgramText"),
            QStringLiteral("ValidateGeometrySnapshot"),
            QStringLiteral("几何快照中没有可生成 G 代码的图元。")
        ));
        return result;
    }
    if (m_processPlan == nullptr || m_processState == nullptr || m_geometrySnapshot == nullptr)
    {
        result.status = OperationStatus::InvalidInput;
        result.addDiagnostic(makeGeneratorDiagnostic
        (
            context,
            DiagnosticCode::MachineTrajectoryInputInvalid,
            DiagnosticSeverity::Error,
            QStringLiteral("BuildRotaryProgramText"),
            QStringLiteral("ValidateProcessPlan"),
            QStringLiteral("当前没有有效的四轴加工计划或几何快照，无法导出。")
        ));
        return result;
    }
    if (m_processPlan->mode != cadcam::planning::ProcessPlanMode::Rotary4Axis)
    {
        result.status = OperationStatus::Conflict;
        result.addDiagnostic(makeGeneratorDiagnostic
        (
            context, DiagnosticCode::ProcessPlanModeMismatch, DiagnosticSeverity::Error,
            QStringLiteral("BuildRotaryProgramText"), QStringLiteral("ValidateProcessPlan"),
            QStringLiteral("四轴 G 代码只能使用四轴加工计划。")
        ));
        return result;
    }

    std::optional<cadcam::geometry::Vector2d> explicitCenter;
    if (m_rotaryTubeCenterValid)
        explicitCenter = cadcam::geometry::Vector2d{ m_rotaryTubeCenterY, m_rotaryTubeCenterZ };
    MachiningProcessConfig processConfig =
        MachiningProcessConfig::fromProfile(*m_profile);
    processConfig.tubeWallThickness = m_tubeWallThickness;
    processConfig.cutSurfacePositiveCorrection = m_cutSurfacePositiveCorrection;
    processConfig.cutSurfaceNegativeCorrection = m_cutSurfaceNegativeCorrection;
    NcProgramService service;
    auto program = service.buildRotaryProgram
    (
        *m_geometrySnapshot,
        *m_processState,
        *m_processPlan,
        m_tubeSectionModel,
        processConfig,
        context,
        explicitCenter
    );
    result.mergeDiagnostics(program);
    if (!program.succeeded() || !program.value.has_value())
    {
        result.status = program.status;
        return result;
    }

    const auto postProfile = cadcam::infrastructure::nc::makeGCodePostProcessorProfile(*m_profile);
    auto rendered = cadcam::infrastructure::nc::GCodePostProcessor::render
        (*program.value, postProfile, context);
    result.mergeDiagnostics(rendered);
    if (!rendered.succeeded() || !rendered.value.has_value())
    {
        result.status = rendered.status;
        return result;
    }
    result.status = program.status == OperationStatus::PartialSuccess
        ? OperationStatus::PartialSuccess : rendered.status;
    result.value = std::move(rendered.value);
    return result;
}

OperationResult<QString> GGenerator::buildProgramText(const OperationContext& context) const
{
    OperationResult<QString> result;
    if (m_geometrySnapshot == nullptr || m_profile == nullptr)
    {
        result.status = OperationStatus::InvalidInput;
        result.addDiagnostic(makeGeneratorDiagnostic
        (
            context,
            m_geometrySnapshot == nullptr
                ? DiagnosticCode::NcProgramInputInvalid : DiagnosticCode::MissingProfile,
            DiagnosticSeverity::Error,
            QStringLiteral("BuildProgramText"),
            QStringLiteral("ValidateInput"),
            m_geometrySnapshot == nullptr
                ? QStringLiteral("未设置几何快照，无法生成 G 代码。")
                : QStringLiteral("未设置 G 代码配置，无法生成 G 代码。")
        ));
        return result;
    }
    if (m_generationMode == GenerationMode::Mode3D) return buildRotaryProgramText(context);

    if (m_processPlan == nullptr || m_processState == nullptr
        || m_processPlan->mode != cadcam::planning::ProcessPlanMode::Planar3Axis)
    {
        result.status = OperationStatus::Conflict;
        result.addDiagnostic(makeGeneratorDiagnostic
        (
            context, DiagnosticCode::ProcessPlanModeMismatch, DiagnosticSeverity::Error,
            QStringLiteral("BuildProgramText"), QStringLiteral("ValidateProcessPlan"),
            QStringLiteral("三轴 G 代码需要有效的三轴加工计划。")
        ));
        return result;
    }

    const MachiningProcessConfig processConfig =
        MachiningProcessConfig::fromProfile(*m_profile);
    NcProgramService service;
    auto program = service.buildPlanarProgram
        (*m_geometrySnapshot, *m_processState, *m_processPlan,
            processConfig, context);
    result.mergeDiagnostics(program);
    if (!program.succeeded() || !program.value.has_value())
    {
        result.status = program.status;
        return result;
    }
    const auto postProfile = cadcam::infrastructure::nc::makeGCodePostProcessorProfile(*m_profile);
    auto rendered = cadcam::infrastructure::nc::GCodePostProcessor::render
        (*program.value, postProfile, context);
    result.mergeDiagnostics(rendered);
    if (!rendered.succeeded() || !rendered.value.has_value())
    {
        result.status = rendered.status;
        return result;
    }
    result.status = program.status == OperationStatus::PartialSuccess
        ? OperationStatus::PartialSuccess : rendered.status;
    result.value = std::move(rendered.value);
    return result;
}
