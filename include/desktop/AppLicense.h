#pragma once

#include <QString>

// 授权只区分“已授权”和“未授权”：没有有效授权文件时程序无法启动，
// 取得有效授权后即可使用全部功能，因此授权文件只包含机器码、授权编号和签名。
enum class AppFeature
{
    BitmapImport,
    SmartSort,
    FourAxisExport,
    ProfileSettings,
    CustomAppearance
};

class AppLicense
{
public:
    static AppLicense load();
    static QString currentMachineId();

    bool isValid() const;
    bool allows(AppFeature feature) const;
    QString licenseId() const;
    QString statusText() const;

private:
    static QString signaturePayload(const QString& machineId, const QString& licenseId);
    static QString signatureForPayload(const QString& payload);

private:
    bool m_valid = false;
    QString m_licenseId;
    QString m_message = QStringLiteral("未授权：未找到授权文件。");
};
