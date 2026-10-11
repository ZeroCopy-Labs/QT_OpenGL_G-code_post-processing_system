#include "desktop/Gcode_postprocessing_system.h"
#include "infrastructure/config/AppSettings.h"
#include "ui/widgets/MachiningSettingsWidget.h"

#include <QApplication>
#include <QDoubleSpinBox>
#include <QSettings>
#include <QTemporaryDir>

#include <cmath>
#include <iostream>
#include <stdexcept>
#include "CutSurfaceCorrectionFlowTests.h"
#include "ToolTransferTests.h"

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    // 测试真实主窗口的信号连接，同时隔离用户配置。
    QTemporaryDir settingsDirectory;
    if (!settingsDirectory.isValid())
    {
        // 静默返回 1 会让失败难以定位：明确说明原因（通常是系统临时目录不可用）。
        std::cerr << "FAIL: 无法创建临时目录用于隔离用户设置；请检查系统 TEMP/TMP 是否可用。\n";
        return 1;
    }
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, settingsDirectory.path());
    // 防呆：应用设置必须落在临时目录内，否则测试会读写用户真实设置。
    const QString isolatedSettingsFile = makeAppSettings().fileName();
    if (!isolatedSettingsFile.startsWith(settingsDirectory.path()))
    {
        std::cerr << "FAIL: 应用设置未隔离到临时目录：" << isolatedSettingsFile.toStdString() << '\n';
        return 1;
    }
    try
    {
        // 启动准入依赖该判定：测试目录没有 license.dat，必须判为未授权且给出状态说明。
        const AppLicense unlicensed = AppLicense::load();
        if (unlicensed.isValid() || unlicensed.statusText().trimmed().isEmpty())
        {
            throw std::runtime_error("license gate: 无 license.dat 时应判为未授权并给出状态说明");
        }
        cutSurfaceCorrectionFlowTests();
        boundaryMinimumXOrderTests();
        toolTransferTests();
        Gcode_postprocessing_system window;
        auto* settings = window.findChild<MachiningSettingsWidget*>();
        auto* thickness = window.findChild<QDoubleSpinBox*>(QStringLiteral("tubeWallThickness"));
        if (!settings || !thickness) throw std::runtime_error("thickness control missing");
        const auto expect = [&](double value)
        {
            if (std::abs(thickness->value() - value) > 1.0e-6)
                throw std::runtime_error("thickness state did not reach the UI");
        };
        const auto setRadius = [&](double radius)
        { settings->manualRotaryTubeSectionRequested(80.0, 60.0, radius); };
        setRadius(4.0);
        expect(4.0);
        // 未改数值，仅结束编辑，仍应保持默认跟随。
        thickness->editingFinished();
        setRadius(6.0);
        expect(6.0);
        thickness->setValue(2.5);
        thickness->editingFinished();
        setRadius(8.0);
        expect(2.5);
        thickness->setValue(0.0);
        thickness->editingFinished();
        expect(8.0);
        setRadius(3.0);
        expect(3.0);
        setRadius(0.0);
        expect(0.0);
        thickness->setValue(1.5);
        thickness->editingFinished();
        expect(1.5);
        std::cout << "PASS: real main-window wiring, radius defaults, focus-only edits, manual override, reset and sharp corners.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
