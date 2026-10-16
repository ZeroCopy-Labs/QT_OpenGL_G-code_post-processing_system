#include "platform/pch.h"

#include "application/export/GCodeFileWriter.h"

#include <QDir>
#include <QFile>

namespace
{
    Diagnostic makeWriterDiagnostic
    (
        const OperationContext& context,
        DiagnosticCode code,
        DiagnosticSeverity severity,
        const QString& stage,
        const QString& userMessage,
        const QString& technicalDetail = QString(),
        const QVariantMap& diagnosticContext = QVariantMap()
    )
    {
        Diagnostic diagnostic;
        diagnostic.code = code;
        diagnostic.severity = severity;
        diagnostic.component = QStringLiteral("GCodeFileWriter");
        diagnostic.operation = QStringLiteral("WriteProgramText");
        diagnostic.stage = stage;
        diagnostic.userMessage = userMessage;
        diagnostic.technicalDetail = technicalDetail;
        diagnostic.correlationId = context.correlationId;
        diagnostic.context = diagnosticContext;
        return diagnostic;
    }
}

OperationReport GCodeFileWriter::write
(
    const QString& filePath,
    const QString& program,
    const OperationContext& context
)
{
    OperationReport report;
    if (filePath.trimmed().isEmpty())
    {
        report.status = OperationStatus::InvalidInput;
        report.addDiagnostic(makeWriterDiagnostic
        (
            context, DiagnosticCode::InvalidArgument, DiagnosticSeverity::Error,
            QStringLiteral("ValidateInput"),
            QStringLiteral("G 代码输出路径为空。"), QStringLiteral("filePath is empty")
        ));
        return report;
    }
    if (program.isEmpty())
    {
        report.status = OperationStatus::InvalidInput;
        report.addDiagnostic(makeWriterDiagnostic
        (
            context, DiagnosticCode::OutputVerificationFailure, DiagnosticSeverity::Error,
            QStringLiteral("ValidateInput"),
            QStringLiteral("不能写入空的 G 代码程序。"), QStringLiteral("program is empty"),
            { { QStringLiteral("filePath"), filePath } }
        ));
        return report;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        const QString nativePath = QDir::toNativeSeparators(filePath);
        report.status = OperationStatus::Failed;
        report.addDiagnostic(makeWriterDiagnostic
        (
            context, DiagnosticCode::FileOpenFailure, DiagnosticSeverity::Error,
            QStringLiteral("OpenOutputFile"),
            QStringLiteral("无法打开 G 代码输出文件：%1\n原因：%2\n常见原因：安全软件（如 360 文档保护）拦截未签名程序写入桌面/文档目录、文件为只读或被其他程序占用、目标目录不存在。\n可改用其他目录（例如 D 盘下的工作目录），或把本程序加入安全软件信任列表后重试。")
                .arg(nativePath, file.errorString()),
            file.errorString(),
            { { QStringLiteral("filePath"), filePath } }
        ));
        return report;
    }

    const QByteArray encodedProgram = program.toUtf8();
    const qint64 writtenLength = file.write(encodedProgram);
    file.close();
    if (writtenLength != encodedProgram.size())
    {
        report.status = OperationStatus::Failed;
        report.addDiagnostic(makeWriterDiagnostic
        (
            context, DiagnosticCode::FileWriteFailure, DiagnosticSeverity::Error,
            QStringLiteral("WriteOutputFile"),
            QStringLiteral("G 代码文件写入不完整：%1\n已写入 %2/%3 字节，请检查磁盘空间与目标目录权限后重试。")
                .arg(QDir::toNativeSeparators(filePath))
                .arg(writtenLength)
                .arg(encodedProgram.size()),
            QStringLiteral("QFile::write returned an unexpected byte count"),
            {
                { QStringLiteral("filePath"), filePath },
                { QStringLiteral("expectedCount"), encodedProgram.size() },
                { QStringLiteral("actualCount"), writtenLength }
            }
        ));
        return report;
    }

    report.status = OperationStatus::Success;
    report.value = std::monostate{};
    return report;
}
