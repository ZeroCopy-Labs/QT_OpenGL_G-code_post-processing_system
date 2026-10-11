#include "platform/pch.h"

#include "ui/widgets/MachiningSettingsWidget.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>

// 输入框仅在获得焦点（被左键点击过）后响应滚轮调整数值；
// 未聚焦时忽略滚轮，事件继续冒泡给外层滚动区域，用于滚动面板。
class FocusWheelDoubleSpinBox : public QDoubleSpinBox
{
public:
    using QDoubleSpinBox::QDoubleSpinBox;

protected:
    void wheelEvent(QWheelEvent* event) override
    {
        if (!hasFocus())
        {
            event->ignore();
            return;
        }

        QDoubleSpinBox::wheelEvent(event);
    }
};

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
        QDoubleSpinBox* input = new FocusWheelDoubleSpinBox(parent);
        input->setDecimals(3);
        input->setRange(0.0, 1000000.0);
        input->setSingleStep(1.0);
        input->setSuffix(QStringLiteral(" mm"));
        input->setSpecialValueText(QStringLiteral("--"));
        return input;
    }

    QDoubleSpinBox* createSignedSectionInput(QWidget* parent)
    {
        QDoubleSpinBox* input = new FocusWheelDoubleSpinBox(parent);
        input->setDecimals(3);
        input->setRange(-1000000.0, 1000000.0);
        input->setSingleStep(1.0);
        input->setSuffix(QStringLiteral(" mm"));
        return input;
    }
}

// 方管 YZ 截面示意图：外轮廓、圆角、中心与 X 轴标记，随截面参数实时重绘。
class TubeSectionDiagramWidget : public QWidget
{
public:
    explicit TubeSectionDiagramWidget(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setMinimumHeight(170);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    }

    void setSection
    (
        bool recognized,
        double yLength,
        double zWidth,
        double cornerRadius,
        double centerY,
        double centerZ,
        bool centerUserSpecified
    )
    {
        m_recognized = recognized;
        m_yLength = std::max(0.0, yLength);
        m_zWidth = std::max(0.0, zWidth);
        m_cornerRadius = std::max(0.0, cornerRadius);
        m_centerY = centerY;
        m_centerZ = centerZ;
        m_centerUserSpecified = centerUserSpecified;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        QFont labelFont = font();
        labelFont.setPointSizeF(std::max(7.5, labelFont.pointSizeF() - 1.0));
        painter.setFont(labelFont);

        const QColor outlineColor = palette().color(QPalette::WindowText);
        const QColor fillColor = palette().color(QPalette::Base);
        const QColor accentColor = palette().color(QPalette::Highlight);
        const QColor dimColor = palette().color(QPalette::Disabled, QPalette::WindowText);

        const QRectF drawingArea = QRectF(rect()).adjusted(6.0, 8.0, -6.0, -8.0);
        const bool hasShape = m_recognized && m_yLength > 0.0 && m_zWidth > 0.0;

        if (!hasShape)
        {
            painter.setPen(dimColor);
            painter.drawText(drawingArea, Qt::AlignCenter,
                QStringLiteral("尚未识别方管截面"));
            return;
        }

        // 世界坐标（Y 向右、Z 向下）→ 控件坐标，中心 (centerY, centerZ) 置于绘图区中心。
        const double halfY = m_yLength * 0.5;
        const double halfZ = m_zWidth * 0.5;
        const double radius = std::min(m_cornerRadius, std::min(halfY, halfZ));
        const double scale = std::min
        (
            drawingArea.width() / (2.0 * halfY + 8.0),
            drawingArea.height() / (2.0 * halfZ + 8.0)
        );
        const QPointF center =
            drawingArea.center() - QPointF(24.0, 0.0);
        auto toWidget = [center, scale](double y, double z)
        {
            return QPointF(center.x() + y * scale, center.y() + z * scale);
        };

        const QPointF topLeft = toWidget(-halfY, -halfZ);
        const QPointF bottomRight = toWidget(halfY, halfZ);
        const QRectF outlineRect(topLeft, bottomRight);
        QPainterPath outlinePath;
        outlinePath.addRoundedRect(outlineRect,
            radius * scale, radius * scale);
        painter.fillPath(outlinePath, fillColor);
        painter.setPen(QPen(outlineColor, 1.4));
        painter.drawPath(outlinePath);

        // 中心十字与 X 轴标记（X 轴垂直于截面）。
        painter.setPen(QPen(accentColor, 1.4));
        const double crossHalf = 6.0;
        painter.drawLine(QPointF(center.x() - crossHalf, center.y()),
            QPointF(center.x() + crossHalf, center.y()));
        painter.drawLine(QPointF(center.x(), center.y() - crossHalf),
            QPointF(center.x(), center.y() + crossHalf));
        painter.drawEllipse(center, 3.2, 3.2);
        painter.drawText(QPointF(center.x() + 7.0, center.y() - 7.0),
            QStringLiteral("X 轴"));

        // 尺寸与中心标注。
        const QString dimensions = QStringLiteral("Y 长 %1\nZ 宽 %2\n圆角 %3")
            .arg(m_yLength, 0, 'f', 3)
            .arg(m_zWidth, 0, 'f', 3)
            .arg(m_cornerRadius, 0, 'f', 3);
        const QString centerText = m_centerUserSpecified
            ? QStringLiteral("中心 Y=%1  Z=%2（手动）")
                .arg(m_centerY, 0, 'f', 3)
                .arg(m_centerZ, 0, 'f', 3)
            : QStringLiteral("中心 Y=%1  Z=%2")
                .arg(m_centerY, 0, 'f', 3)
                .arg(m_centerZ, 0, 'f', 3);
        const QRectF dimensionsRect = QRectF(drawingArea.left(), drawingArea.top(),
            drawingArea.width(), drawingArea.height());
        painter.setPen(outlineColor);
        painter.drawText(dimensionsRect.adjusted(0.0, 0.0, 0.0, -18.0),
            Qt::AlignRight | Qt::AlignTop, dimensions);
        painter.drawText(dimensionsRect.adjusted(0.0, -18.0, 0.0, 0.0),
            Qt::AlignLeft | Qt::AlignBottom, centerText);
    }

private:
    bool m_recognized = false;
    double m_yLength = 0.0;
    double m_zWidth = 0.0;
    double m_cornerRadius = 0.0;
    double m_centerY = 0.0;
    double m_centerZ = 0.0;
    bool m_centerUserSpecified = false;
};

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
    QVBoxLayout* statusGroupLayout = new QVBoxLayout(statusGroup);
    statusGroupLayout->setContentsMargins(6, 6, 6, 6);
    statusGroupLayout->setSpacing(8);

    QGroupBox* parametersGroup = new QGroupBox(QStringLiteral("截面参数"), statusGroup);
    QVBoxLayout* parametersLayout = new QVBoxLayout(parametersGroup);
    parametersLayout->setSpacing(6);
    m_sectionDiagram = new TubeSectionDiagramWidget(parametersGroup);
    parametersLayout->addWidget(m_sectionDiagram);

    QFormLayout* parametersForm = new QFormLayout;
    parametersForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_yLengthInput = createSectionInput(parametersGroup);
    m_zWidthInput = createSectionInput(parametersGroup);
    m_cornerRadiusInput = createSectionInput(parametersGroup);
    m_cornerRadiusInput->setSingleStep(0.1);
    m_centerYInput = createSignedSectionInput(parametersGroup);
    m_centerYInput->setToolTip(QStringLiteral(
        "方管截面中心 Y 坐标。手动设置后优先于自动识别中心；清除后恢复自动识别中心或默认 (0,0)。"));
    m_centerZInput = createSignedSectionInput(parametersGroup);
    m_centerZInput->setToolTip(QStringLiteral(
        "方管截面中心 Z 坐标。手动设置后优先于自动识别中心；清除后恢复自动识别中心或默认 (0,0)。"));
    m_wallThicknessInput = createSectionInput(parametersGroup);
    m_wallThicknessInput->setObjectName(QStringLiteral("tubeWallThickness"));
    m_wallThicknessInput->setRange(0.0, 100000.0);
    m_wallThicknessInput->setSingleStep(0.1);
    m_wallThicknessInput->setSpecialValueText(QStringLiteral("未设置"));
    m_wallThicknessInput->setToolTip(QStringLiteral(
        "默认取圆角半径，手动修改后保留；输入 0 可恢复默认值。内截面尺寸为外尺寸减去两倍壁厚，内圆角半径不小于 0；厚度须小于 Y 长、Z 宽的一半。用于两条内壁参考走线及导出时的加工断面修正。"));
    m_applyManualSectionButton = new QPushButton(QStringLiteral("应用手动截面"), parametersGroup);
    m_applyManualSectionButton->setToolTip
        (QStringLiteral("手动设置优先用于后续内部线识别、加工断面、排序和导出。"));
    m_applySectionCenterButton = new QPushButton(QStringLiteral("应用截面中心"), parametersGroup);
    m_applySectionCenterButton->setToolTip(QStringLiteral(
        "把上方 Y、Z 输入作为方管截面中心，优先于自动识别中心。"));
    m_clearSectionCenterButton = new QPushButton(QStringLiteral("清除截面中心"), parametersGroup);
    m_clearSectionCenterButton->setToolTip(QStringLiteral(
        "恢复自动识别中心；没有自动识别结果时使用默认 (0,0)。"));
    QHBoxLayout* sectionButtonLayout = new QHBoxLayout;
    sectionButtonLayout->setContentsMargins(0, 0, 0, 0);
    sectionButtonLayout->addWidget(m_applyManualSectionButton);
    QHBoxLayout* centerButtonLayout = new QHBoxLayout;
    centerButtonLayout->setContentsMargins(0, 0, 0, 0);
    centerButtonLayout->addWidget(m_applySectionCenterButton);
    centerButtonLayout->addWidget(m_clearSectionCenterButton);
    parametersForm->addRow(QStringLiteral("Y 长："), m_yLengthInput);
    parametersForm->addRow(QStringLiteral("Z 宽："), m_zWidthInput);
    parametersForm->addRow(QStringLiteral("圆角半径："), m_cornerRadiusInput);
    parametersForm->addRow(QStringLiteral("截面中心 Y："), m_centerYInput);
    parametersForm->addRow(QStringLiteral("截面中心 Z："), m_centerZInput);
    parametersForm->addRow(QStringLiteral("方管厚度："), m_wallThicknessInput);
    parametersForm->addRow(QString(), sectionButtonLayout);
    parametersForm->addRow(QString(), centerButtonLayout);
    parametersLayout->addLayout(parametersForm);
    statusGroupLayout->addWidget(parametersGroup);

    QGroupBox* stateGroup = new QGroupBox(QStringLiteral("识别状态"), statusGroup);
    QFormLayout* stateLayout = new QFormLayout(stateGroup);
    stateLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_sectionStatusValue = createStatusValue(stateGroup);
    m_roundedCornerCountValue = createStatusValue(stateGroup);
    m_centerLineValue = createStatusValue(stateGroup);
    m_endCutPreviewStatusValue = createStatusValue(stateGroup);
    m_endCutPreviewStatusValue->setObjectName(QStringLiteral("endCutPreviewStatus"));
    m_endCutPreviewStatusValue->setWordWrap(true);
    m_rotaryEndCutCountValue = createStatusValue(stateGroup);
    m_endCutSideValue = createStatusValue(stateGroup);
    m_endCutSideValue->setWordWrap(true);
    m_endCutSideValue->setToolTip(QStringLiteral(
        "加工断面的端面归属：自动结果按最小 X 顺序左右交替；右键画布上的断面加工单元可指定为左端面、右端面或普通断面。"));
    m_laserWallImpactVisibleCheckBox = new QCheckBox(QStringLiteral("激光内壁作用线（绿色）"), stateGroup);
    m_laserWallImpactVisibleCheckBox->setObjectName(QStringLiteral("laserWallImpactVisible"));
    m_laserWallImpactVisibleCheckBox->setChecked(true);
    m_laserWallImpactVisibleCheckBox->setToolTip(QStringLiteral(
        "先按四轴姿态和 Z 修正求候选点，再将 YZ 位置投到内截面轮廓；内圆角归零时补齐直角转折。仅是投影预览，不是激光射线与材料的完整求交仿真。"));
    m_expectedSurfaceVisibleCheckBox = new QCheckBox(QStringLiteral("期望断面内侧走线（紫色）"), stateGroup);
    m_expectedSurfaceVisibleCheckBox->setObjectName(QStringLiteral("expectedSurfaceVisible"));
    m_expectedSurfaceVisibleCheckBox->setChecked(true);
    m_expectedSurfaceVisibleCheckBox->setToolTip(QStringLiteral(
        "与绿色作用线投影到相同内轮廓，由圆角推断的局部断面方向求 X，直角处也补入转折点；曲线边保留自身形状。边两端方向不一致时报错。两条走线还受加工标记和加工断面标记总开关控制。"));
    m_cutSurfacePositiveCorrectionCheckBox = new QCheckBox(QStringLiteral("左端面修正（+X）"), stateGroup);
    m_cutSurfacePositiveCorrectionCheckBox->setObjectName(QStringLiteral("cutSurfacePositiveCorrection"));
    m_cutSurfacePositiveCorrectionCheckBox->setToolTip(QStringLiteral("仅对左端面加工断面生效：期望断面内侧走线 X 大于激光内壁作用线 X 时，沿 +X 补足差值。默认关闭。"));
    m_cutSurfaceNegativeCorrectionCheckBox = new QCheckBox(QStringLiteral("右端面修正（−X）"), stateGroup);
    m_cutSurfaceNegativeCorrectionCheckBox->setObjectName(QStringLiteral("cutSurfaceNegativeCorrection"));
    m_cutSurfaceNegativeCorrectionCheckBox->setToolTip(QStringLiteral("仅对右端面加工断面生效：期望断面内侧走线 X 小于激光内壁作用线 X 时，沿 −X 补足差值。可与左端面修正同时开启；普通“断面”不做任何修正。"));
    connect(m_cutSurfacePositiveCorrectionCheckBox, &QCheckBox::toggled, this, [this](bool checked)
    {
        if (!m_updatingUi) emit cutSurfacePositiveCorrectionChanged(checked);
    });
    connect(m_cutSurfaceNegativeCorrectionCheckBox, &QCheckBox::toggled, this, [this](bool checked)
    {
        if (!m_updatingUi) emit cutSurfaceNegativeCorrectionChanged(checked);
    });
    stateLayout->addRow(QStringLiteral("截面状态："), m_sectionStatusValue);
    stateLayout->addRow(QStringLiteral("圆角数量："), m_roundedCornerCountValue);
    stateLayout->addRow(QStringLiteral("中心线位置："), m_centerLineValue);
    stateLayout->addRow(QStringLiteral("走线状态："), m_endCutPreviewStatusValue);
    stateLayout->addRow(QString(), m_cutSurfacePositiveCorrectionCheckBox);
    stateLayout->addRow(QString(), m_cutSurfaceNegativeCorrectionCheckBox);
    stateLayout->addRow(QString(), m_laserWallImpactVisibleCheckBox);
    stateLayout->addRow(QString(), m_expectedSurfaceVisibleCheckBox);
    stateLayout->addRow(QStringLiteral("加工断面："), m_rotaryEndCutCountValue);
    stateLayout->addRow(QStringLiteral("断面端面："), m_endCutSideValue);
    statusGroupLayout->addWidget(stateGroup);
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

void MachiningSettingsWidget::setRotaryEndCutSideSummary(const QString& summary)
{
    m_endCutSideValue->setText(summary.trimmed().isEmpty()
        ? QStringLiteral("--") : summary);
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
    m_sectionManuallyConfigured = manuallyConfigured;
    m_sectionBlinkVisible = true;
    m_sectionStatusValue->setText(recognized
        ? (manuallyConfigured ? QStringLiteral("已手动设置") : QStringLiteral("已识别"))
        : QStringLiteral("未识别"));
    m_sectionDiagram->setSection(recognized, yLength, zWidth, cornerRadius,
        centerY, centerZ, centerUserSpecified);
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

void MachiningSettingsWidget::setCutSurfaceCorrectionOptions
    (bool positiveEnabled, bool negativeEnabled)
{
    m_updatingUi = true;
    m_cutSurfacePositiveCorrectionCheckBox->setChecked(positiveEnabled);
    m_cutSurfaceNegativeCorrectionCheckBox->setChecked(negativeEnabled);
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
        m_sectionStatusValue->setStyleSheet(m_sectionManuallyConfigured
            ? QStringLiteral("color: #1565c0; font-weight: 600;")
            : QStringLiteral("color: #2e7d32; font-weight: 600;"));
        return;
    }

    m_sectionStatusValue->setStyleSheet(m_sectionBlinkVisible
        ? QStringLiteral("color: #d32f2f; font-weight: 600;")
        : QStringLiteral("color: rgba(211, 47, 47, 90); font-weight: 600;"));
}
