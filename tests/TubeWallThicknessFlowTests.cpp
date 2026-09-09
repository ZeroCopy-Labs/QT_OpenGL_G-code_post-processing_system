#include "desktop/Gcode_postprocessing_system.h"
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
    if (!settingsDirectory.isValid()) return 1;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, settingsDirectory.path());
    try
    {
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
