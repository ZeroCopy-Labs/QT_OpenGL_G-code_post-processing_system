#pragma once

#include "core/diagnostics/OperationContext.h"
#include "core/diagnostics/OperationResult.h"

#include <QString>

// 把已经生成好的 G-code 文本写入文件。
// 只负责打开文件、写入并校验写入长度；不负责 NC 生成、后处理、规划、CAD、对话框或配置读写。
class GCodeFileWriter
{
public:
    static OperationReport write
    (
        const QString& filePath,
        const QString& program,
        const OperationContext& context
    );
};
