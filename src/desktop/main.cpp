#include "platform/pch.h"

#include "desktop/AppLicense.h"
#include "desktop/Gcode_postprocessing_system.h"

#include <QtWidgets/QApplication>
#include <QAbstractButton>
#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QMessageBox>
#include <QString>

namespace
{
    QString machineCodeFileName()
    {
        return QStringLiteral("机器码.txt");
    }

    int saveMachineCodeFile()
    {
        QFile file(QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(machineCodeFileName()));

        if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        {
            return 1;
        }

        file.write(AppLicense::currentMachineId().toUtf8());
        file.write("\n");
        return 0;
    }

    // 启动准入：没有有效 license.dat 时直接给出授权提示并拒绝运行，不再等到导出等功能入口才提示。
    // @return true 表示已授权并继续启动；false 表示已提示用户并应结束进程。
    bool ensureLicenseAvailable()
    {
        const AppLicense license = AppLicense::load();
        if (license.isValid())
        {
            return true;
        }

        const QString machineId = AppLicense::currentMachineId();
        const QString applicationDirectory =
            QDir::toNativeSeparators(QCoreApplication::applicationDirPath());
        const QString executableName =
            QFileInfo(QCoreApplication::applicationFilePath()).fileName();

        const QString message = QStringLiteral(
            "未检测到可用的 license.dat，程序无法启动。\n\n"
            "授权状态：%1\n"
            "程序目录：%2\n"
            "机器码：%3\n\n"
            "获取方式：把上面的机器码发给软件提供方，取得 license.dat 后放到程序目录，再重新启动。\n"
            "也可以在命令行执行 “%4 --license-request”，程序目录会生成 %5，内容即机器码。")
            .arg(license.statusText(), applicationDirectory, machineId,
                executableName, machineCodeFileName());

        QMessageBox authorizationDialog
        (
            QMessageBox::Critical,
            QStringLiteral("需要授权"),
            message,
            QMessageBox::Close
        );
        authorizationDialog.setWindowIcon(QApplication::windowIcon());
        QAbstractButton* copyMachineCodeButton = authorizationDialog.addButton
            (QStringLiteral("复制机器码"), QMessageBox::ActionRole);
        authorizationDialog.exec();

        if (authorizationDialog.clickedButton() == copyMachineCodeButton)
        {
            QApplication::clipboard()->setText(machineId);
        }

        return false;
    }
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    const AppBranding branding = AppBranding::load();
    QCoreApplication::setApplicationName(branding.applicationName());
    QCoreApplication::setOrganizationName(QStringLiteral("G-code Post Processing"));
    QGuiApplication::setApplicationDisplayName(branding.applicationName());

    const QIcon applicationIcon(QStringLiteral(":/branding/G.svg"));
    if (applicationIcon.isNull())
    {
        qWarning("Failed to load application icon from :/branding/G.svg");
    }
    else
    {
        QApplication::setWindowIcon(applicationIcon);
    }

    // 机器码申请入口必须在不授权时也可用，因此先于准入检查处理。
    if (QApplication::arguments().contains(QStringLiteral("--license-request")))
    {
        return saveMachineCodeFile();
    }

    if (!ensureLicenseAvailable())
    {
        return 2;
    }

    Gcode_postprocessing_system window;
    window.show();
    return app.exec();
}
