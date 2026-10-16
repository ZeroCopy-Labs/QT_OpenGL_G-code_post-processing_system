#pragma once
#include "core/machine/SameZoneTransferParameters.h"

#include <QColor>
#include <QJsonObject>
#include <QMap>
#include <QString>

struct GProfileCodeBlock
{
    QString header;
    QString footer;
    QString comment;

    QJsonObject toJson() const;
    static GProfileCodeBlock fromJson(const QJsonObject& object);
};

struct GProfileToolTransferConfig
{
    double rotationSafetyClearance = 5.0;
    double sameZoneTransferClearance = 0.0;
    bool coordinatedTransferEnabled = true;
    cadcam::machine::SameZoneTransferParameters sameZone;

    QJsonObject toJson() const;
    static GProfileToolTransferConfig fromJson(const QJsonObject& object);
};

enum class GProfilePerimeterSweepDirection
{
    Clockwise,
    CounterClockwise
};

enum class GProfileLongitudinalSweepDirection
{
    PositiveX,
    NegativeX
};

struct GProfileRotaryAxisConfig
{
    double centerY = 0.0;
    double centerZ = 0.0;
    double aAxisOffsetDegrees = 0.0;
    double machiningPlaneZOffset = 0.0; // 相对实时加工平面的 Z 修正
    double overcutDistance = 2.0; // 闭合路径沿原方向继续加工的距离
    bool lazyRotationProcessing = false;
    bool invertAAxisDirection = false;
    bool keepContinuousAngle = true;
    bool useSafeZBeforeRapid = true;
    bool useInitialMachinePoint = false;
    GProfilePerimeterSweepDirection perimeterSweepDirection =
        GProfilePerimeterSweepDirection::Clockwise;
    GProfileLongitudinalSweepDirection longitudinalSweepDirection =
        GProfileLongitudinalSweepDirection::PositiveX;
    double initialMachineX = 0.0;
    double initialMachineY = 0.0;
    double initialMachineZ = 0.0;

    QJsonObject toJson() const;
    static GProfileRotaryAxisConfig fromJson(const QJsonObject& object);
};

class GProfile
{
public:
    GProfile() = default;

    static GProfile createDefaultLaserProfile();
    static GProfile createDefaultRotaryProfile();
    static GProfile loadFromFile(const QString& filePath, QString* errorMessage = nullptr);

    bool saveToFile(const QString& filePath, QString* errorMessage = nullptr) const;

    void clear();

    void setProfileName(const QString& profileName);
    const QString& profileName() const;

    void setFileCode(const GProfileCodeBlock& codeBlock);
    const GProfileCodeBlock& fileCode() const;
    void setProcessUnitCode(const GProfileCodeBlock& codeBlock);
    const GProfileCodeBlock& processUnitCode() const;

    void setEntityTypeCode(const QString& entityType, const GProfileCodeBlock& codeBlock);
    bool containsEntityTypeCode(const QString& entityType) const;
    GProfileCodeBlock entityTypeCode(const QString& entityType) const;
    void removeEntityTypeCode(const QString& entityType);
    const QMap<QString, GProfileCodeBlock>& entityTypeCodes() const;

    void setLayerCode(const QString& layerName, const GProfileCodeBlock& codeBlock);
    bool containsLayerCode(const QString& layerName) const;
    GProfileCodeBlock layerCode(const QString& layerName) const;
    void removeLayerCode(const QString& layerName);
    const QMap<QString, GProfileCodeBlock>& layerCodes() const;

    void setEntityColorCode(const QString& colorKey, const GProfileCodeBlock& codeBlock);
    void setEntityColorCode(const QColor& color, const GProfileCodeBlock& codeBlock);
    bool containsEntityColorCode(const QString& colorKey) const;
    bool containsEntityColorCode(const QColor& color) const;
    GProfileCodeBlock entityColorCode(const QString& colorKey) const;
    GProfileCodeBlock entityColorCode(const QColor& color) const;
    void removeEntityColorCode(const QString& colorKey);
    void removeEntityColorCode(const QColor& color);
    const QMap<QString, GProfileCodeBlock>& entityColorCodes() const;

    void setRotaryAxisConfig(const GProfileRotaryAxisConfig& config);
    const GProfileRotaryAxisConfig& rotaryAxisConfig() const;
    void setToolTransferConfig(const GProfileToolTransferConfig& config);
    const GProfileToolTransferConfig& toolTransferConfig() const;

    // 四轴加工规划直接读取的回转轴与刀具转移参数是否相同。
    //
    // 这些字段会改变四轴加工计划的顺序、方向、起点和加工单元边界，因此变化时需要重建计划；
    // 其余机床运动与后处理配置只影响轨迹、NC、仿真和 G 代码文本，不影响已有多轴计划。
    bool hasSameRotaryPlanningInputs(const GProfile& other) const;

    // 轨迹、NC 与仿真读取的机床运动参数是否相同：回转轴配置与刀具转移配置全部字段。
    // 这些字段变化时轨迹、NC 与仿真需要重建，但几何快照、加工计划与 CAM 展示保持有效。
    bool hasSameMachineMotionInputs(const GProfile& other) const;

    static QString normalizeEntityTypeKey(const QString& entityType);
    static QString normalizeLayerKey(const QString& layerName);
    static QString normalizeColorKey(const QString& colorKey);
    static QString colorKeyFromColor(const QColor& color);
    static QString colorKeyFromAci(int colorIndex);

private:
    QString m_profileName;
    GProfileCodeBlock m_fileCode;
    GProfileCodeBlock m_processUnitCode
    {
        QStringLiteral("M03"),
        QStringLiteral("M05"),
        QStringLiteral("加工单元启停代码")
    };
    QMap<QString, GProfileCodeBlock> m_entityTypeCodes;
    QMap<QString, GProfileCodeBlock> m_layerCodes;
    QMap<QString, GProfileCodeBlock> m_entityColorCodes;
    GProfileRotaryAxisConfig m_rotaryAxisConfig;
    GProfileToolTransferConfig m_toolTransferConfig;
};
