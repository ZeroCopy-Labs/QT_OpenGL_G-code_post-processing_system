#pragma once

#include <QSettings>
#include <QString>

// 应用设置的统一入口。
// 必须使用“显式 format 重载”并传入 defaultFormat()：
// QSettings(org, app) 与 QSettings(scope, org, app) 两个重载都会强制 NativeFormat，
// 测试无法通过 setDefaultFormat(IniFormat) + setPath 隔离，会直接读写用户真实注册表。
// 生产环境 defaultFormat() 仍是 NativeFormat（注册表），行为不变。
inline QSettings makeAppSettings()
{
    return QSettings
    (
        QSettings::defaultFormat(),
        QSettings::UserScope,
        QStringLiteral("GCodePostProcessingSystem"),
        QStringLiteral("GCodePostProcessingSystem")
    );
}
