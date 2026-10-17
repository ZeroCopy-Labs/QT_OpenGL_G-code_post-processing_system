#include "platform/pch.h"

#include "ui/widgets/CamToolPanelWidget.h"

#include "ui/UiStyleSpec.h"
#include "ui/widgets/ToolPanelStyle.h"

#include <algorithm>
#include <cmath>

#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace
{
    constexpr int kFooterHeight = uistyle::kFooterHeight;
    constexpr int kDividerHeight = 96;
    constexpr int kRibbonButtonHeight = 46;
}

CamToolPanelWidget::CamToolPanelWidget(QWidget* parent)
    : QWidget(parent)
{
    buildUi();
    applyTheme();
}

void CamToolPanelWidget::buildUi()
{
    setObjectName(QStringLiteral("camToolPanelRoot"));

    QVBoxLayout* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    m_stageTabs = new QTabWidget(this);
    m_stageTabs->setObjectName(QStringLiteral("camStageTabs"));
    m_stageTabs->setDocumentMode(true);
    m_stageTabs->tabBar()->setExpanding(false);
    m_stageTabs->tabBar()->setUsesScrollButtons(false);
    m_stageTabs->addTab(buildPlanningStage(), QStringLiteral("规划"));
    m_stageTabs->addTab(buildSimulationStage(), QStringLiteral("仿真"));
    m_stageTabs->addTab(buildNcPostStage(), QStringLiteral("NC 与后处理"));
    rootLayout->addWidget(m_stageTabs);
}

QWidget* CamToolPanelWidget::buildPlanningStage()
{
    QWidget* stage = new QWidget(m_stageTabs);
    QHBoxLayout* layout = new QHBoxLayout(stage);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    QToolButton* recognizeSectionButton = buildMachiningButton(stage, QStringLiteral("识别方管截面"));
    QToolButton* recognizeEndCutsButton = buildMachiningButton(stage, QStringLiteral("识别加工断面"));
    QToolButton* removeInternalPathsButton = buildMachiningButton(stage, QStringLiteral("清理内部线条"));
    QToolButton* machiningSettingsButton = buildMachiningButton(stage, QStringLiteral("加工设置"));
    m_sortKeepDirectionButton = buildMachiningButton(stage, QStringLiteral("排序(保留方向)"));
    m_smartSortButton = buildMachiningButton(stage, QStringLiteral("智能排序"));

    QWidget* sectionPanel = new QWidget(stage);
    QGridLayout* sectionLayout = new QGridLayout(sectionPanel);
    sectionLayout->setContentsMargins(1, 6, 1, 2);
    sectionLayout->setHorizontalSpacing(6);
    sectionLayout->setVerticalSpacing(6);
    sectionLayout->addWidget(recognizeSectionButton, 0, 0);
    sectionLayout->addWidget(machiningSettingsButton, 0, 1);
    sectionLayout->addWidget(removeInternalPathsButton, 1, 0);
    sectionLayout->addWidget(recognizeEndCutsButton, 1, 1);
    sectionLayout->setColumnStretch(0, 1);
    sectionLayout->setColumnStretch(1, 1);
    sectionLayout->setAlignment(Qt::AlignLeft | Qt::AlignTop);

    QWidget* sortPanel = new QWidget(stage);
    QGridLayout* sortLayout = new QGridLayout(sortPanel);
    sortLayout->setContentsMargins(1, 6, 1, 2);
    sortLayout->setHorizontalSpacing(6);
    sortLayout->setVerticalSpacing(6);
    sortLayout->addWidget(m_sortKeepDirectionButton, 0, 0);
    sortLayout->addWidget(m_smartSortButton, 1, 0);
    sortLayout->setColumnStretch(0, 1);
    sortLayout->setAlignment(Qt::AlignLeft | Qt::AlignTop);

    layout->addWidget(buildPanelFrame(QStringLiteral("截面与断面"), sectionPanel, 242), 0, Qt::AlignLeft | Qt::AlignTop);
    layout->addWidget(buildDivider(), 0, Qt::AlignLeft | Qt::AlignVCenter);
    layout->addWidget(buildPanelFrame(QStringLiteral("排序"), sortPanel, 132), 0, Qt::AlignLeft | Qt::AlignTop);
    layout->addWidget(buildDivider(), 0, Qt::AlignLeft | Qt::AlignVCenter);
    layout->addWidget(buildPanelFrame(QStringLiteral("加工显示"), buildMachiningDisplayPanel(), 430), 0, Qt::AlignLeft | Qt::AlignTop);
    layout->addStretch(1);

    connect(recognizeSectionButton, &QToolButton::clicked, this, [this]() { emit recognizeRotaryTubeSectionRequested(); });
    connect(recognizeEndCutsButton, &QToolButton::clicked, this, [this]() { emit recognizeRotaryEndCutsRequested(); });
    connect(removeInternalPathsButton, &QToolButton::clicked, this, [this]() { emit removeInternalPathsRequested(); });
    connect(machiningSettingsButton, &QToolButton::clicked, this, [this]() { emit machiningSettingsRequested(); });
    connect(m_sortKeepDirectionButton, &QToolButton::clicked, this, [this]() { emit sortKeepDirectionRequested(); });
    connect(m_smartSortButton, &QToolButton::clicked, this, [this]() { emit smartSortRequested(); });

    return stage;
}

QWidget* CamToolPanelWidget::buildSimulationStage()
{
    QWidget* stage = new QWidget(m_stageTabs);
    QHBoxLayout* layout = new QHBoxLayout(stage);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    layout->addWidget
    (
        buildPanelFrame(QStringLiteral("仿真"), buildSimulationControlPanel(), 470, true),
        0, Qt::AlignLeft | Qt::AlignTop
    );
    layout->addStretch(1);
    return stage;
}

QWidget* CamToolPanelWidget::buildSimulationControlPanel()
{
    QWidget* panel = new QWidget(m_stageTabs);
    QGridLayout* layout = new QGridLayout(panel);
    layout->setContentsMargins(2, 6, 2, 2);
    layout->setHorizontalSpacing(6);
    layout->setVerticalSpacing(6);
    panel->setMaximumWidth(470);

    // 第一行：仿真会话控制。仿真状态与进度始终由 Application 的 SimulationController 保存。
    QWidget* controlRow = new QWidget(panel);
    QHBoxLayout* controlLayout = new QHBoxLayout(controlRow);
    controlLayout->setContentsMargins(0, 0, 0, 0);
    controlLayout->setSpacing(6);
    m_prepareSimulationButton = buildMachiningButton(controlRow, QStringLiteral("准备仿真"));
    m_playSimulationButton = buildMachiningButton(controlRow, QStringLiteral("播放"));
    m_pauseSimulationButton = buildMachiningButton(controlRow, QStringLiteral("暂停"));
    m_stepSimulationButton = buildMachiningButton(controlRow, QStringLiteral("单步"));
    m_resetSimulationButton = buildMachiningButton(controlRow, QStringLiteral("复位"));
    controlLayout->addWidget(m_prepareSimulationButton);
    controlLayout->addWidget(m_playSimulationButton);
    controlLayout->addWidget(m_pauseSimulationButton);
    controlLayout->addWidget(m_stepSimulationButton);
    controlLayout->addWidget(m_resetSimulationButton);
    controlLayout->addStretch(1);
    layout->addWidget(controlRow, 0, 0, 1, 2);

    // 第二行：播放倍率。只是播放速度，不是真实进给率。
    QLabel* speedLabel = new QLabel(QStringLiteral("速度"), panel);
    speedLabel->setProperty("machiningFieldLabel", true);
    m_simulationSpeedComboBox = new QComboBox(panel);
    m_simulationSpeedComboBox->setEditable(false);
    m_simulationSpeedComboBox->setMinimumHeight(toolpanel::kComboHeight);
    m_simulationSpeedComboBox->addItem(QStringLiteral("0.25×"), 0.25);
    m_simulationSpeedComboBox->addItem(QStringLiteral("0.5×"), 0.5);
    m_simulationSpeedComboBox->addItem(QStringLiteral("1×"), 1.0);
    m_simulationSpeedComboBox->addItem(QStringLiteral("2×"), 2.0);
    m_simulationSpeedComboBox->addItem(QStringLiteral("4×"), 4.0);
    m_simulationSpeedComboBox->setCurrentIndex(2);
    layout->addWidget(speedLabel, 1, 0);
    layout->addWidget(m_simulationSpeedComboBox, 1, 1);

    // 第三行：整体进度。滑块只负责用户操作，拖动结束后定位仿真会话。
    m_simulationProgressSlider = new QSlider(Qt::Horizontal, panel);
    m_simulationProgressSlider->setRange(0, 1000);
    m_simulationProgressSlider->setValue(0);
    m_simulationProgressSlider->setEnabled(false);
    layout->addWidget(m_simulationProgressSlider, 2, 0, 1, 2);

    // 第四行：状态文字。
    m_simulationStateLabel = new QLabel(QStringLiteral("状态：未准备"), panel);
    m_simulationMotionLabel = new QLabel(QStringLiteral("运动：-"), panel);
    m_simulationMoveLabel = new QLabel(QStringLiteral("Move：0 / 0"), panel);
    m_simulationAxisLabel = new QLabel(QStringLiteral("A：-"), panel);

    const QList<QLabel*> statusLabels
    {
        m_simulationStateLabel,
        m_simulationMotionLabel,
        m_simulationMoveLabel,
        m_simulationAxisLabel
    };
    for (QLabel* label : statusLabels)
    {
        label->setProperty("machiningValueLabel", true);
    }

    layout->addWidget(m_simulationStateLabel, 3, 0);
    layout->addWidget(m_simulationMotionLabel, 3, 1);
    layout->addWidget(m_simulationMoveLabel, 4, 0);
    layout->addWidget(m_simulationAxisLabel, 4, 1);
    layout->setColumnStretch(1, 1);
    layout->setAlignment(Qt::AlignLeft | Qt::AlignTop);

    connect(m_prepareSimulationButton, &QToolButton::clicked, this,
        [this]() { emit prepareSimulationRequested(); });
    connect(m_playSimulationButton, &QToolButton::clicked, this,
        [this]() { emit playSimulationRequested(); });
    connect(m_pauseSimulationButton, &QToolButton::clicked, this,
        [this]() { emit pauseSimulationRequested(); });
    connect(m_stepSimulationButton, &QToolButton::clicked, this,
        [this]() { emit stepSimulationRequested(); });
    connect(m_resetSimulationButton, &QToolButton::clicked, this,
        [this]() { emit resetSimulationRequested(); });
    connect
    (
        m_simulationSpeedComboBox,
        &QComboBox::currentIndexChanged,
        this,
        [this](int index)
        {
            if (m_updatingUi || index < 0)
            {
                return;
            }

            emit simulationSpeedChanged(m_simulationSpeedComboBox->itemData(index).toDouble());
        }
    );
    connect
    (
        m_simulationProgressSlider,
        &QSlider::sliderReleased,
        this,
        [this]()
        {
            if (m_updatingUi)
            {
                return;
            }

            emit simulationSeekRequested
                (static_cast<double>(m_simulationProgressSlider->value()) / 1000.0);
        }
    );

    setSimulationControlsEnabled(false);
    return panel;
}

void CamToolPanelWidget::setSimulationControlsEnabled(bool enabled)
{
    if (m_prepareSimulationButton == nullptr)
    {
        return;
    }

    m_simulationControlsEnabled = enabled;
    m_playSimulationButton->setEnabled(enabled);
    m_pauseSimulationButton->setEnabled(enabled);
    m_stepSimulationButton->setEnabled(enabled);
    m_resetSimulationButton->setEnabled(enabled);
    m_simulationSpeedComboBox->setEnabled(enabled);
    m_simulationProgressSlider->setEnabled(enabled);
}

void CamToolPanelWidget::setSimulationPlaying(bool playing)
{
    if (m_simulationProgressSlider == nullptr)
    {
        return;
    }

    // 播放期间进度由仿真会话驱动，用户拖动只在暂停时生效。
    m_simulationProgressSlider->setEnabled(m_simulationControlsEnabled && !playing);
}

void CamToolPanelWidget::setSimulationStateText(const QString& text)
{
    if (m_simulationStateLabel != nullptr)
    {
        m_simulationStateLabel->setText(QStringLiteral("状态：%1").arg(text));
    }
}

void CamToolPanelWidget::setSimulationMotionText(const QString& text)
{
    if (m_simulationMotionLabel != nullptr)
    {
        m_simulationMotionLabel->setText(QStringLiteral("运动：%1").arg(text));
    }
}

void CamToolPanelWidget::setSimulationMoveText(const QString& text)
{
    if (m_simulationMoveLabel != nullptr)
    {
        m_simulationMoveLabel->setText(text);
    }
}

void CamToolPanelWidget::setSimulationAxisAlignmentText(const QString& text)
{
    if (m_simulationAxisLabel != nullptr)
    {
        m_simulationAxisLabel->setText(text);
    }
}

void CamToolPanelWidget::setSimulationProgress(double progress)
{
    if (m_simulationProgressSlider == nullptr)
    {
        return;
    }

    m_updatingUi = true;
    m_simulationProgressSlider->setValue
    (
        static_cast<int>(std::clamp(progress, 0.0, 1.0) * 1000.0)
    );
    m_updatingUi = false;
}

void CamToolPanelWidget::setSimulationSpeed(double speed)
{
    if (m_simulationSpeedComboBox == nullptr)
    {
        return;
    }

    m_updatingUi = true;
    for (int index = 0; index < m_simulationSpeedComboBox->count(); ++index)
    {
        if (std::abs(m_simulationSpeedComboBox->itemData(index).toDouble() - speed) < 1.0e-6)
        {
            m_simulationSpeedComboBox->setCurrentIndex(index);
            break;
        }
    }
    m_updatingUi = false;
}

QWidget* CamToolPanelWidget::buildNcPostStage()
{
    QWidget* stage = new QWidget(m_stageTabs);
    QHBoxLayout* layout = new QHBoxLayout(stage);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_exportGCodeButton = buildMachiningButton(stage, QStringLiteral("G代码导出"));
    m_profileSettingsButton = buildMachiningButton(stage, QStringLiteral("G代码配置"));

    QWidget* exportPanel = new QWidget(stage);
    QGridLayout* exportLayout = new QGridLayout(exportPanel);
    exportLayout->setContentsMargins(1, 6, 1, 2);
    exportLayout->setHorizontalSpacing(6);
    exportLayout->setVerticalSpacing(6);
    exportLayout->addWidget(m_exportGCodeButton, 0, 0);
    exportLayout->setColumnStretch(0, 1);
    exportLayout->setAlignment(Qt::AlignLeft | Qt::AlignTop);

    QWidget* configPanel = new QWidget(stage);
    QGridLayout* configLayout = new QGridLayout(configPanel);
    configLayout->setContentsMargins(2, 6, 2, 2);
    configLayout->setHorizontalSpacing(6);
    configLayout->setVerticalSpacing(4);
    configPanel->setMaximumWidth(310);

    m_profileManagerButton = new QToolButton(configPanel);
    m_profileManagerButton->setText(QStringLiteral("当前配置"));
    m_profileManagerButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_profileManagerButton->setProperty("machiningButton", true);
    m_profileManagerButton->setToolTip(QStringLiteral("管理配置文件与检索路径"));
    m_profileManagerButton->setMinimumHeight(toolpanel::kComboHeight);
    m_profileManagerButton->setMinimumWidth(84);
    m_profileManagerButton->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    m_profileComboBox = new QComboBox(configPanel);
    m_profileComboBox->setEditable(false);
    m_profileComboBox->setMinimumHeight(toolpanel::kComboHeight);
    m_profileComboBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    configLayout->addWidget(m_profileManagerButton, 0, 0);
    configLayout->addWidget(m_profileComboBox, 0, 1, 1, 2);

    QLabel* modeLabel = new QLabel(QStringLiteral("G代码模式"), configPanel);
    modeLabel->setProperty("machiningFieldLabel", true);
    modeLabel->setMinimumWidth(60);
    m_gcodeModeComboBox = new QComboBox(configPanel);
    m_gcodeModeComboBox->setEditable(false);
    m_gcodeModeComboBox->setMinimumHeight(toolpanel::kComboHeight);
    m_gcodeModeComboBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_gcodeModeComboBox->addItem(QStringLiteral("自动"), static_cast<int>(GCodeModeSelection::Auto));
    m_gcodeModeComboBox->addItem(QStringLiteral("3轴"), static_cast<int>(GCodeModeSelection::ThreeAxis));
    m_gcodeModeComboBox->addItem(QStringLiteral("4轴(绕A)"), static_cast<int>(GCodeModeSelection::FourAxisAroundA));
    configLayout->addWidget(modeLabel, 1, 0);
    configLayout->addWidget(m_gcodeModeComboBox, 1, 1, 1, 2);
    configLayout->addWidget(m_profileSettingsButton, 2, 0);
    configLayout->setColumnStretch(1, 1);

    layout->addWidget(buildPanelFrame(QStringLiteral("导出"), exportPanel, 132), 0, Qt::AlignLeft | Qt::AlignTop);
    layout->addWidget(buildDivider(), 0, Qt::AlignLeft | Qt::AlignVCenter);
    layout->addWidget(buildPanelFrame(QStringLiteral("G代码配置"), configPanel, 250, true), 0, Qt::AlignLeft | Qt::AlignTop);
    layout->addStretch(1);

    connect(m_exportGCodeButton, &QToolButton::clicked, this, [this]() { emit exportGCodeRequested(); });
    connect(m_profileManagerButton, &QToolButton::clicked, this, [this]() { emit profileManagerRequested(); });
    connect(m_profileSettingsButton, &QToolButton::clicked, this, [this]() { emit profileSettingsRequested(); });
    connect
    (
        m_profileComboBox,
        &QComboBox::currentIndexChanged,
        this,
        [this](int index)
        {
            if (m_updatingUi || index < 0)
            {
                return;
            }

            emit profileSelectionChanged(m_profileComboBox->itemData(index).toString());
        }
    );
    connect
    (
        m_gcodeModeComboBox,
        &QComboBox::currentIndexChanged,
        this,
        [this](int index)
        {
            if (m_updatingUi || index < 0)
            {
                return;
            }

            emit gcodeModeSelectionChanged(static_cast<GCodeModeSelection>(index));
        }
    );

    return stage;
}

QWidget* CamToolPanelWidget::buildMachiningDisplayPanel()
{
    QWidget* panel = new QWidget(m_stageTabs);
    QGridLayout* layout = new QGridLayout(panel);
    layout->setContentsMargins(2, 1, 2, 1);
    layout->setHorizontalSpacing(18);
    layout->setVerticalSpacing(1);

    m_processVisualsCheckBox = new QCheckBox(QStringLiteral("显示机加工相关"), panel);
    m_processVisualsCheckBox->setProperty("machiningOption", true);
    m_processVisualsCheckBox->setToolTip(QStringLiteral("总开关，仅影响画布显示，不影响排序和 G 代码输出"));
    m_processVisualsCheckBox->setChecked(true);
    m_processDirectionCheckBox = new QCheckBox(QStringLiteral("加工方向箭头"), panel);
    m_processOrderCheckBox = new QCheckBox(QStringLiteral("加工顺序编号"), panel);
    m_rotaryEndCutsCheckBox = new QCheckBox(QStringLiteral("加工断面标记"), panel);
    m_excludedEntitiesDimmedCheckBox = new QCheckBox(QStringLiteral("废弃区域弱化"), panel);

    const QList<QCheckBox*> defaultChecked
    {
        m_processDirectionCheckBox,
        m_processOrderCheckBox,
        m_rotaryEndCutsCheckBox,
        m_excludedEntitiesDimmedCheckBox
    };
    for (QCheckBox* checkBox : defaultChecked)
    {
        checkBox->setChecked(true);
    }

    layout->addWidget(m_processVisualsCheckBox, 0, 0);
    layout->addWidget(m_processDirectionCheckBox, 0, 1);
    layout->addWidget(m_processOrderCheckBox, 1, 0);
    layout->addWidget(m_rotaryEndCutsCheckBox, 1, 1);
    layout->addWidget(m_excludedEntitiesDimmedCheckBox, 2, 0);

    QToolButton* resetButton = new QToolButton(panel);
    resetButton->setText(QStringLiteral("恢复默认显示"));
    resetButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    resetButton->setProperty("machiningButton", true);
    layout->addWidget(resetButton, 2, 1, Qt::AlignLeft);

    const auto updateMachiningOptionState = [this](bool enabled)
    {
        m_processDirectionCheckBox->setEnabled(enabled);
        m_processOrderCheckBox->setEnabled(enabled);
        m_rotaryEndCutsCheckBox->setEnabled(enabled);
        m_excludedEntitiesDimmedCheckBox->setEnabled(enabled);
    };

    connect(m_processVisualsCheckBox, &QCheckBox::toggled, this, [this, updateMachiningOptionState](bool checked)
    {
        updateMachiningOptionState(checked);
        if (!m_updatingUi)
        {
            emit processVisualsVisibleChanged(checked);
        }
    });
    connect(m_processDirectionCheckBox, &QCheckBox::toggled, this, &CamToolPanelWidget::processDirectionVisibleChanged);
    connect(m_processOrderCheckBox, &QCheckBox::toggled, this, &CamToolPanelWidget::processOrderVisibleChanged);
    connect(m_rotaryEndCutsCheckBox, &QCheckBox::toggled, this, &CamToolPanelWidget::rotaryEndCutsVisibleChanged);
    connect(m_excludedEntitiesDimmedCheckBox, &QCheckBox::toggled, this, &CamToolPanelWidget::excludedEntitiesDimmedChanged);
    connect(resetButton, &QToolButton::clicked, this, [this]()
    {
        m_processVisualsCheckBox->setChecked(true);
        m_processDirectionCheckBox->setChecked(true);
        m_processOrderCheckBox->setChecked(true);
        m_rotaryEndCutsCheckBox->setChecked(true);
        m_excludedEntitiesDimmedCheckBox->setChecked(true);
    });

    return panel;
}

QToolButton* CamToolPanelWidget::buildMachiningButton(QWidget* parent, const QString& text)
{
    QToolButton* button = new QToolButton(parent);
    button->setText(text);
    button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    button->setProperty("machiningButton", true);
    button->setMinimumHeight(uistyle::kButtonHeightCompact);
    button->setMinimumWidth(96);
    button->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
    return button;
}

QWidget* CamToolPanelWidget::buildPanelFrame(const QString& title, QWidget* contentWidget, int preferredWidth, bool flexibleWidth)
{
    QWidget* panel = new QWidget(this);
    panel->setProperty("ribbonPanel", true);
    panel->setSizePolicy(flexibleWidth ? QSizePolicy::Preferred : QSizePolicy::Fixed, QSizePolicy::Fixed);
    panel->setMinimumHeight(toolpanel::kPanelHeight);

    QVBoxLayout* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(6, 4, 6, 0);
    layout->setSpacing(0);
    layout->addWidget(contentWidget, 1);

    QWidget* footerWidget = new QWidget(panel);
    footerWidget->setMinimumHeight(kFooterHeight);
    QHBoxLayout* footerLayout = new QHBoxLayout(footerWidget);
    footerLayout->setContentsMargins(0, 0, 0, 0);
    footerLayout->setSpacing(0);

    QLabel* titleLabel = new QLabel(title, footerWidget);
    titleLabel->setAlignment(Qt::AlignHCenter | Qt::AlignBottom);
    titleLabel->setProperty("panelTitle", true);
    footerLayout->addStretch(1);
    footerLayout->addWidget(titleLabel, 0, Qt::AlignHCenter | Qt::AlignBottom);
    footerLayout->addStretch(1);

    layout->addWidget(footerWidget, 0, Qt::AlignBottom);

    const int resolvedWidth = preferredWidth > 0
        ? preferredWidth
        : std::max(contentWidget->sizeHint().width(), titleLabel->sizeHint().width())
            + layout->contentsMargins().left() + layout->contentsMargins().right();

    if (flexibleWidth)
    {
        panel->setMinimumWidth(std::max(120, resolvedWidth - 24));
        panel->setMaximumWidth(QWIDGETSIZE_MAX);
    }
    else
    {
        panel->setMinimumWidth(resolvedWidth);
        panel->setMaximumWidth(resolvedWidth);
    }

    return panel;
}

QWidget* CamToolPanelWidget::buildDivider()
{
    QFrame* divider = new QFrame();
    divider->setFixedWidth(1);
    divider->setMinimumHeight(kDividerHeight);
    divider->setFrameShape(QFrame::NoFrame);
    m_dividers.push_back(divider);
    return divider;
}

void CamToolPanelWidget::setTheme(const AppThemeColors& theme)
{
    m_theme = theme;
    applyTheme();
}

void CamToolPanelWidget::applyTheme()
{
    setStyleSheet(toolpanel::toolPanelStyleSheet(m_theme));

    for (QFrame* divider : m_dividers)
    {
        if (divider != nullptr)
        {
            divider->setStyleSheet
                (QStringLiteral("background-color: %1;").arg(toolpanel::cssRgb(m_theme.borderColor)));
        }
    }
}

void CamToolPanelWidget::setGCodeModeSelection(GCodeModeSelection selection)
{
    if (m_gcodeModeComboBox == nullptr)
    {
        return;
    }

    m_updatingUi = true;
    m_gcodeModeComboBox->setCurrentIndex(static_cast<int>(selection));
    m_updatingUi = false;
}

void CamToolPanelWidget::setAvailableProfiles(const QList<QPair<QString, QString>>& profiles)
{
    if (m_profileComboBox == nullptr)
    {
        return;
    }

    const QString currentProfileId = m_profileComboBox->currentData().toString();
    m_updatingUi = true;
    m_profileComboBox->clear();

    for (const QPair<QString, QString>& profile : profiles)
    {
        m_profileComboBox->addItem(profile.second, profile.first);
    }

    const int currentIndex = m_profileComboBox->findData(currentProfileId);

    if (currentIndex >= 0)
    {
        m_profileComboBox->setCurrentIndex(currentIndex);
    }

    m_updatingUi = false;
}

void CamToolPanelWidget::setCurrentProfileSelection(const QString& profileId)
{
    if (m_profileComboBox == nullptr)
    {
        return;
    }

    const int index = m_profileComboBox->findData(profileId);

    if (index < 0)
    {
        return;
    }

    m_updatingUi = true;
    m_profileComboBox->setCurrentIndex(index);
    m_updatingUi = false;
}

void CamToolPanelWidget::setProcessVisualsVisible(bool enabled)
{
    if (m_processVisualsCheckBox == nullptr)
    {
        return;
    }

    m_updatingUi = true;
    m_processVisualsCheckBox->setChecked(enabled);
    m_updatingUi = false;
}

bool CamToolPanelWidget::processVisualsVisible() const
{
    return m_processVisualsCheckBox == nullptr || m_processVisualsCheckBox->isChecked();
}

void CamToolPanelWidget::setProcessDirectionVisible(bool enabled)
{
    if (m_processDirectionCheckBox != nullptr) m_processDirectionCheckBox->setChecked(enabled);
}

void CamToolPanelWidget::setProcessOrderVisible(bool enabled)
{
    if (m_processOrderCheckBox != nullptr) m_processOrderCheckBox->setChecked(enabled);
}

void CamToolPanelWidget::setRotaryEndCutsVisible(bool enabled)
{
    if (m_rotaryEndCutsCheckBox != nullptr) m_rotaryEndCutsCheckBox->setChecked(enabled);
}

void CamToolPanelWidget::setExcludedEntitiesDimmed(bool enabled)
{
    if (m_excludedEntitiesDimmedCheckBox != nullptr) m_excludedEntitiesDimmedCheckBox->setChecked(enabled);
}
