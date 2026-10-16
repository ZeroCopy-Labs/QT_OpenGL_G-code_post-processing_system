#include "platform/pch.h"

#include "ui/widgets/CamToolPanelWidget.h"

#include "ui/widgets/ToolPanelStyle.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace
{
    constexpr int kFooterHeight = 18;
    constexpr int kDividerHeight = 78;
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

    QWidget* placeholderPanel = new QWidget(stage);
    QVBoxLayout* placeholderLayout = new QVBoxLayout(placeholderPanel);
    placeholderLayout->setContentsMargins(8, 8, 8, 8);
    placeholderLayout->setSpacing(4);

    // 本阶段只建立入口：轨迹仿真引擎属于后续阶段，此处不提供任何仿真实现。
    QLabel* placeholderLabel = new QLabel
    (
        QStringLiteral("轨迹仿真尚未实现。\n当前可在 CAM 工作区检查加工顺序、方向和刀路预览，"),
        placeholderPanel
    );
    placeholderLabel->setWordWrap(true);
    QLabel* placeholderHint = new QLabel
    (
        QStringLiteral("实际仿真与空跑由后续阶段基于 MachineTrajectory 建立。"),
        placeholderPanel
    );
    placeholderHint->setWordWrap(true);
    QToolButton* placeholderButton = buildMachiningButton(placeholderPanel, QStringLiteral("仿真（后续阶段）"));
    placeholderButton->setEnabled(false);

    placeholderLayout->addWidget(placeholderLabel);
    placeholderLayout->addWidget(placeholderHint);
    placeholderLayout->addWidget(placeholderButton, 0, Qt::AlignLeft);
    placeholderLayout->addStretch(1);

    layout->addWidget(buildPanelFrame(QStringLiteral("仿真"), placeholderPanel, 460, true), 0, Qt::AlignLeft | Qt::AlignTop);
    layout->addStretch(1);
    return stage;
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
    m_profileManagerButton->setFixedHeight(toolpanel::kComboHeight);
    m_profileManagerButton->setMinimumWidth(76);
    m_profileManagerButton->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    m_profileComboBox = new QComboBox(configPanel);
    m_profileComboBox->setEditable(false);
    m_profileComboBox->setFixedHeight(toolpanel::kComboHeight);
    m_profileComboBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    configLayout->addWidget(m_profileManagerButton, 0, 0);
    configLayout->addWidget(m_profileComboBox, 0, 1, 1, 2);

    QLabel* modeLabel = new QLabel(QStringLiteral("G代码模式"), configPanel);
    modeLabel->setProperty("machiningFieldLabel", true);
    modeLabel->setMinimumWidth(54);
    m_gcodeModeComboBox = new QComboBox(configPanel);
    m_gcodeModeComboBox->setEditable(false);
    m_gcodeModeComboBox->setFixedHeight(toolpanel::kComboHeight);
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
    button->setMinimumHeight(30);
    button->setMinimumWidth(92);
    button->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
    return button;
}

QWidget* CamToolPanelWidget::buildPanelFrame(const QString& title, QWidget* contentWidget, int preferredWidth, bool flexibleWidth)
{
    QWidget* panel = new QWidget(this);
    panel->setProperty("ribbonPanel", true);
    panel->setSizePolicy(flexibleWidth ? QSizePolicy::Preferred : QSizePolicy::Fixed, QSizePolicy::Fixed);
    panel->setMinimumHeight(toolpanel::kPanelHeight);
    panel->setMaximumHeight(toolpanel::kPanelHeight);

    QVBoxLayout* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(6, 4, 6, 0);
    layout->setSpacing(0);
    layout->addWidget(contentWidget, 1);

    QWidget* footerWidget = new QWidget(panel);
    footerWidget->setFixedHeight(kFooterHeight);
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
        panel->setMinimumWidth(std::max(112, resolvedWidth - 24));
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
    divider->setFixedHeight(kDividerHeight);
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
