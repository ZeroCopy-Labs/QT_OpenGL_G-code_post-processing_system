#include "platform/pch.h"

#include "ui/widgets/MachiningSettingsWidget.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

namespace
{
    QLabel* createStatusValue(QWidget* parent)
    {
        QLabel* label = new QLabel(QStringLiteral("--"), parent);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        return label;
    }

    QDoubleSpinBox* createSectionInput(QWidget* parent)
    {
        QDoubleSpinBox* input = new QDoubleSpinBox(parent);
        input->setDecimals(3);
        input->setRange(0.0, 1000000.0);
        input->setSingleStep(1.0);
        input->setSuffix(QStringLiteral(" mm"));
        input->setSpecialValueText(QStringLiteral("--"));
        return input;
    }

    QDoubleSpinBox* createSignedSectionInput(QWidget* parent)
    {
        QDoubleSpinBox* input = new QDoubleSpinBox(parent);
        input->setDecimals(3);
        input->setRange(-1000000.0, 1000000.0);
        input->setSingleStep(1.0);
        input->setSuffix(QStringLiteral(" mm"));
        return input;
    }
}

MachiningSettingsWidget::MachiningSettingsWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumWidth(260);

    QVBoxLayout* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);

    QScrollArea* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    rootLayout->addWidget(scrollArea);

    QWidget* content = new QWidget(scrollArea);
    QVBoxLayout* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(10, 10, 10, 10);
    contentLayout->setSpacing(10);

    QGroupBox* automaticGroup = new QGroupBox(QStringLiteral("自动处理"), content);
    QVBoxLayout* automaticLayout = new QVBoxLayout(automaticGroup);
    m_autoDeduplicateCheckBox = new QCheckBox(QStringLiteral("导入后自动去重"), automaticGroup);
    m_autoDeduplicateCheckBox->setToolTip(QStringLiteral("导入 DXF/DWG 后删除完全重叠的同类型图元。"));
    m_autoRecognizeSectionCheckBox = new QCheckBox(QStringLiteral("导入后自动识别方管截面"), automaticGroup);
    m_autoRecognizeSectionCheckBox->setToolTip(QStringLiteral("从文档中的多个候选截面中选择最可靠的方管垂直截面。"));
    m_autoRecognizeEndCutsCheckBox = new QCheckBox(QStringLiteral("自动识别加工断面"), automaticGroup);
    m_autoRecognizeEndCutsCheckBox->setToolTip(QStringLiteral("方管截面识别成功后，自动搜索并标记全部有效加工断面。"));
    m_autoRemoveInternalPathsCheckBox = new QCheckBox(QStringLiteral("自动清理内部线条"), automaticGroup);
    m_autoRemoveInternalPathsCheckBox->setToolTip(QStringLiteral(
        "方管截面识别成功后，自动删除与内缩窗口相交的内部线图元，删除可撤销。"));
    m_autoRecognizeEndCutsCheckBox->setContentsMargins(18, 0, 0, 0);
    m_autoRemoveInternalPathsCheckBox->setContentsMargins(18, 0, 0, 0);
    automaticLayout->addWidget(m_autoDeduplicateCheckBox);
    automaticLayout->addWidget(m_autoRecognizeSectionCheckBox);
    automaticLayout->addWidget(m_autoRecognizeEndCutsCheckBox);
    automaticLayout->addWidget(m_autoRemoveInternalPathsCheckBox);
    contentLayout->addWidget(automaticGroup);

    QGroupBox* exportGroup = new QGroupBox(QStringLiteral("导出设置"), content);
    QVBoxLayout* exportLayout = new QVBoxLayout(exportGroup);
    m_useDefaultExportDirectoryCheckBox = new QCheckBox(QStringLiteral("使用默认导出目录"), exportGroup);
    m_useDxfFileNameCheckBox = new QCheckBox(QStringLiteral("使用 DXF 文件名"), exportGroup);
    exportLayout->addWidget(m_useDefaultExportDirectoryCheckBox);
    exportLayout->addWidget(m_useDxfFileNameCheckBox);
    contentLayout->addWidget(exportGroup);

    QGroupBox* statusGroup = new QGroupBox(QStringLiteral("方管识别状态"), content);
    QFormLayout* statusLayout = new QFormLayout(statusGroup);
    statusLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_sectionStatusValue = createStatusValue(statusGroup);
    m_yLengthInput = createSectionInput(statusGroup);
    m_zWidthInput = createSectionInput(statusGroup);
    m_cornerRadiusInput = createSectionInput(statusGroup);
    m_cornerRadiusInput->setSingleStep(0.1);
    m_centerYInput = createSignedSectionInput(statusGroup);
    m_centerYInput->setToolTip(QStringLiteral(
        "方管截面中心 Y 坐标。手动设置后优先于自动识别中心；清除后恢复自动识别中心或默认 (0,0)。"));
    m_centerZInput = createSignedSectionInput(statusGroup);
    m_centerZInput->setToolTip(QStringLiteral(
        "方管截面中心 Z 坐标。手动设置后优先于自动识别中心；清除后恢复自动识别中心或默认 (0,0)。"));
    m_wallThicknessInput = createSectionInput(statusGroup);
    m_wallThicknessInput->setObjectName(QStringLiteral("tubeWallThickness"));
    m_wallThicknessInput->setRange(0.0, 100000.0);
    m_wallThicknessInput->setSingleStep(0.1);
    m_wallThicknessInput->setSpecialValueText(QStringLiteral("未设置"));
    m_wallThicknessInput->setToolTip(QStringLiteral(
        "默认取圆角半径，手动修改后保留；输入 0 可恢复默认值。内截面尺寸为外尺寸减去两倍壁厚，内圆角半径不小于 0；厚度须小于 Y 长、Z 宽的一半。用于两条内壁参考走线及导出时的加工断面修正。"));
    m_laserWallImpactVisibleCheckBox = new QCheckBox(QStringLiteral("激光内壁作用线（绿色）"), statusGroup);
    m_laserWallImpactVisibleCheckBox->setObjectName(QStringLiteral("laserWallImpactVisible"));
    m_laserWallImpactVisibleCheckBox->setChecked(true);
    m_laserWallImpactVisibleCheckBox->setToolTip(QStringLiteral(
        "先按四轴姿态和 Z 修正求候选点，再将 YZ 位置投到内截面轮廓；内圆角归零时补齐直角转折。仅是投影预览，不是激光射线与材料的完整求交仿真。"));
    m_expectedSurfaceVisibleCheckBox = new QCheckBox(QStringLiteral("期望断面内侧走线（紫色）"), statusGroup);
    m_expectedSurfaceVisibleCheckBox->setObjectName(QStringLiteral("expectedSurfaceVisible"));
    m_expectedSurfaceVisibleCheckBox->setChecked(true);
    m_expectedSurfaceVisibleCheckBox->setToolTip(QStringLiteral(
        "与绿色作用线投影到相同内轮廓，由圆角推断的局部断面方向求 X，直角处也补入转折点；曲线边保留自身形状。边两端方向不一致时报错。两条走线还受加工标记和加工断面标记总开关控制。"));
    m_endCutPreviewStatusValue = createStatusValue(statusGroup);
    m_endCutPreviewStatusValue->setObjectName(QStringLiteral("endCutPreviewStatus"));
    m_endCutPreviewStatusValue->setWordWrap(true);
    m_applyManualSectionButton = new QPushButton(QStringLiteral("应用手动截面"), statusGroup);
    m_applyManualSectionButton->setToolTip
        (QStringLiteral("手动设置优先用于后续内部线识别、加工断面、排序和导出。"));
    m_applySectionCenterButton = new QPushButton(QStringLiteral("应用截面中心"), statusGroup);
    m_applySectionCenterButton->setToolTip(QStringLiteral(
        "把上方 Y、Z 输入作为方管截面中心，优先于自动识别中心。"));
    m_clearSectionCenterButton = new QPushButton(QStringLiteral("清除截面中心"), statusGroup);
    m_clearSectionCenterButton->setToolTip(QStringLiteral(
        "恢复自动识别中心；没有自动识别结果时使用默认 (0,0)。"));
    QHBoxLayout* centerButtonLayout = new QHBoxLayout;
    centerButtonLayout->setContentsMargins(0, 0, 0, 0);
    centerButtonLayout->addWidget(m_applySectionCenterButton);
    centerButtonLayout->addWidget(m_clearSectionCenterButton);
    m_roundedCornerCountValue = createStatusValue(statusGroup);
    m_centerLineValue = createStatusValue(statusGroup);
    m_rotaryEndCutCountValue = createStatusValue(statusGroup);
    statusLayout->addRow(QStringLiteral("截面状态："), m_sectionStatusValue);
    statusLayout->addRow(QStringLiteral("Y 长："), m_yLengthInput);
    statusLayout->addRow(QStringLiteral("Z 宽："), m_zWidthInput);
    statusLayout->addRow(QStringLiteral("圆角半径："), m_cornerRadiusInput);
    statusLayout->addRow(QStringLiteral("截面中心 Y："), m_centerYInput);
    statusLayout->addRow(QStringLiteral("截面中心 Z："), m_centerZInput);
    statusLayout->addRow(QString(), centerButtonLayout);
    statusLayout->addRow(QStringLiteral("方管厚度："), m_wallThicknessInput);
    auto* positiveCorrection = new QCheckBox(QStringLiteral("加工断面正向修正（+X）"), statusGroup);
    positiveCorrection->setObjectName(QStringLiteral("cutSurfacePositiveCorrection"));
    positiveCorrection->setToolTip(QStringLiteral("生成四轴 G 代码时，仅当期望点 X 大于内壁作用点 X，沿 +X 补足差值。默认关闭。"));
    auto* negativeCorrection = new QCheckBox(QStringLiteral("加工断面反向修正（−X）"), statusGroup);
    negativeCorrection->setObjectName(QStringLiteral("cutSurfaceNegativeCorrection"));
    negativeCorrection->setToolTip(QStringLiteral("生成四轴 G 代码时，仅当期望点 X 小于内壁作用点 X，沿 −X 补足差值。可与正向同时开启；显示开关不影响此工艺。"));
    statusLayout->addRow(QString(), positiveCorrection);
    statusLayout->addRow(QString(), negativeCorrection);
    connect(positiveCorrection, &QCheckBox::toggled,
        this, &MachiningSettingsWidget::cutSurfacePositiveCorrectionChanged);
    connect(negativeCorrection, &QCheckBox::toggled,
        this, &MachiningSettingsWidget::cutSurfaceNegativeCorrectionChanged);
    statusLayout->addRow(QString(), m_laserWallImpactVisibleCheckBox);
    statusLayout->addRow(QString(), m_expectedSurfaceVisibleCheckBox);
    statusLayout->addRow(QStringLiteral("走线状态："), m_endCutPreviewStatusValue);
    statusLayout->addRow(QString(), m_applyManualSectionButton);
    statusLayout->addRow(QStringLiteral("圆角数量："), m_roundedCornerCountValue);
    statusLayout->addRow(QStringLiteral("中心线位置："), m_centerLineValue);
    statusLayout->addRow(QStringLiteral("加工断面："), m_rotaryEndCutCountValue);
    contentLayout->addWidget(statusGroup);
    contentLayout->addStretch(1);
    scrollArea->setWidget(content);

    m_sectionBlinkTimer = new QTimer(this);
    m_sectionBlinkTimer->setInterval(650);
    connect(m_sectionBlinkTimer, &QTimer::timeout, this, [this]()
    {
        if (m_sectionRecognized)
        {
            m_sectionBlinkTimer->stop();
            return;
        }

        m_sectionBlinkVisible = !m_sectionBlinkVisible;
        refreshSectionStatusStyle();
    });
    m_sectionBlinkTimer->start();

    connect(m_autoDeduplicateCheckBox, &QCheckBox::toggled, this, [this](bool enabled)
    {
        if (!m_updatingUi) emit autoDeduplicateOnImportChanged(enabled);
    });
    connect(m_autoRecognizeSectionCheckBox, &QCheckBox::toggled, this, [this](bool enabled)
    {
        updateAutomaticOptionDependencies();

        if (!m_updatingUi) emit autoRecognizeRotaryTubeSectionOnImportChanged(enabled);
    });
    connect(m_autoRecognizeEndCutsCheckBox, &QCheckBox::toggled, this, [this](bool enabled)
    {
        if (enabled && !m_autoRecognizeSectionCheckBox->isChecked())
        {
            m_autoRecognizeSectionCheckBox->setChecked(true);
        }

        if (!m_updatingUi) emit autoRecognizeRotaryEndCutsOnImportChanged(enabled);
    });
    connect(m_autoRemoveInternalPathsCheckBox, &QCheckBox::toggled, this, [this](bool enabled)
    {
        if (enabled && !m_autoRecognizeSectionCheckBox->isChecked())
        {
            m_autoRecognizeSectionCheckBox->setChecked(true);
        }

        if (!m_updatingUi) emit autoRemoveInternalPathsOnImportChanged(enabled);
    });
    connect(m_useDefaultExportDirectoryCheckBox, &QCheckBox::toggled, this, [this](bool enabled)
    {
        if (!m_updatingUi) emit useDefaultExportDirectoryChanged(enabled);
    });
    connect(m_useDxfFileNameCheckBox, &QCheckBox::toggled, this, [this](bool enabled)
    {
        if (!m_updatingUi) emit useDxfFileNameChanged(enabled);
    });
    connect(m_applyManualSectionButton, &QPushButton::clicked, this, [this]()
    {
        emit manualRotaryTubeSectionRequested
        (
            m_yLengthInput->value(),
            m_zWidthInput->value(),
            m_cornerRadiusInput->value()
        );
    });
    connect(m_applySectionCenterButton, &QPushButton::clicked, this, [this]()
    {
        emit sectionCenterRequested
        (
            m_centerYInput->value(),
            m_centerZInput->value()
        );
    });
    connect(m_clearSectionCenterButton, &QPushButton::clicked, this,
        &MachiningSettingsWidget::sectionCenterClearRequested);
    connect(m_wallThicknessInput, &QDoubleSpinBox::editingFinished, this, [this]()
    {
        if (!m_updatingUi)
        {
            emit rotaryTubeWallThicknessChanged(m_wallThicknessInput->value());
        }
    });
    connect(m_laserWallImpactVisibleCheckBox, &QCheckBox::toggled,
        this, &MachiningSettingsWidget::rotaryLaserWallImpactVisibleChanged);
    connect(m_expectedSurfaceVisibleCheckBox, &QCheckBox::toggled,
        this, &MachiningSettingsWidget::rotaryExpectedSurfaceVisibleChanged);
}

void MachiningSettingsWidget::setRotaryEndCutPreviewStatus(const QString& status)
{
    m_endCutPreviewStatusValue->setText(status);
}

void MachiningSettingsWidget::setAutomaticOptions
(
    bool deduplicate,
    bool recognizeSection,
    bool recognizeEndCuts,
    bool removeInternalPaths
)
{
    m_updatingUi = true;
    recognizeSection = recognizeSection || recognizeEndCuts || removeInternalPaths;
    m_autoDeduplicateCheckBox->setChecked(deduplicate);
    m_autoRecognizeSectionCheckBox->setChecked(recognizeSection);
    m_autoRecognizeEndCutsCheckBox->setChecked(recognizeEndCuts);
    m_autoRemoveInternalPathsCheckBox->setChecked(removeInternalPaths);
    updateAutomaticOptionDependencies();
    m_updatingUi = false;
}

void MachiningSettingsWidget::setExportOptions(bool useDefaultExportDirectory, bool useDxfFileName)
{
    m_updatingUi = true;
    m_useDefaultExportDirectoryCheckBox->setChecked(useDefaultExportDirectory);
    m_useDxfFileNameCheckBox->setChecked(useDxfFileName);
    m_updatingUi = false;
}

void MachiningSettingsWidget::setRotaryTubeSectionProperties
(
    bool recognized,
    double yLength,
    double zWidth,
    double cornerRadius,
    int roundedCornerCount,
    bool manuallyConfigured,
    double centerY,
    double centerZ,
    bool centerUserSpecified
)
{
    m_updatingUi = true;
    m_sectionRecognized = recognized;
    m_sectionBlinkVisible = true;
    m_sectionStatusValue->setText(recognized
        ? (manuallyConfigured ? QStringLiteral("已手动设置") : QStringLiteral("已识别"))
        : QStringLiteral("未识别"));
    m_centerYInput->setValue(centerY);
    m_centerZInput->setValue(centerZ);
    m_centerLineValue->setText(centerUserSpecified
        ? QStringLiteral("Y=%1  Z=%2（手动）")
            .arg(centerY, 0, 'f', 3)
            .arg(centerZ, 0, 'f', 3)
        : QStringLiteral("Y=%1  Z=%2")
            .arg(centerY, 0, 'f', 3)
            .arg(centerZ, 0, 'f', 3));

    if (recognized)
    {
        m_sectionBlinkTimer->stop();
        m_yLengthInput->setValue(yLength);
        m_zWidthInput->setValue(zWidth);
        m_cornerRadiusInput->setValue(cornerRadius);
        m_roundedCornerCountValue->setText(QString::number(std::clamp(roundedCornerCount, 0, 4)));
    }
    else
    {
        m_yLengthInput->setValue(0.0);
        m_zWidthInput->setValue(0.0);
        m_cornerRadiusInput->setValue(0.0);
        m_roundedCornerCountValue->setText(QStringLiteral("--"));

        if (!centerUserSpecified)
        {
            m_centerLineValue->setText(QStringLiteral("--"));
        }

        if (!m_sectionBlinkTimer->isActive())
        {
            m_sectionBlinkTimer->start();
        }
    }

    m_updatingUi = false;
    refreshSectionStatusStyle();
}

void MachiningSettingsWidget::setRotaryEndCutCount(int count)
{
    m_rotaryEndCutCountValue->setText(count > 0
        ? QStringLiteral("已识别 %1 个").arg(count)
        : QStringLiteral("未识别"));
}

void MachiningSettingsWidget::setRotaryTubeWallThickness(double wallThickness)
{
    m_updatingUi = true;
    m_wallThicknessInput->setValue(std::max(0.0, wallThickness));
    m_updatingUi = false;
}

void MachiningSettingsWidget::updateAutomaticOptionDependencies()
{
    const bool sectionEnabled = m_autoRecognizeSectionCheckBox->isChecked();

    if (!sectionEnabled)
    {
        m_autoRecognizeEndCutsCheckBox->setChecked(false);
        m_autoRemoveInternalPathsCheckBox->setChecked(false);
    }

    m_autoRecognizeEndCutsCheckBox->setEnabled(sectionEnabled);
    m_autoRemoveInternalPathsCheckBox->setEnabled(sectionEnabled);
}

void MachiningSettingsWidget::refreshSectionStatusStyle()
{
    if (m_sectionRecognized)
    {
        m_sectionStatusValue->setStyleSheet(QString());
        return;
    }

    m_sectionStatusValue->setStyleSheet(m_sectionBlinkVisible
        ? QStringLiteral("color: #d32f2f; font-weight: 600;")
        : QStringLiteral("color: rgba(211, 47, 47, 90); font-weight: 600;"));
}
