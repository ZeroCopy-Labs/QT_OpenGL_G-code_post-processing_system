#include "platform/pch.h"

#include "ui/widgets/WorkspaceSwitcherWidget.h"

#include "ui/UiStyleSpec.h"

#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

namespace
{
    constexpr int kWorkspaceIconSize = 40;
    constexpr int kWorkspaceIconMargin = 0;

    // CAD 工作区图标：折线轮廓加节点，表示几何创建与编辑。
    QIcon buildCadIcon(const QColor& stroke, const QColor& accent)
    {
        QPixmap pixmap(kWorkspaceIconSize, kWorkspaceIconSize);
        pixmap.fill(Qt::transparent);

        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const qreal inset = kWorkspaceIconMargin + 3.0;
        const QRectF frame
        (
            inset,
            inset,
            kWorkspaceIconSize - 2.0 * inset,
            kWorkspaceIconSize - 2.0 * inset
        );

        QPen pen(stroke);
        pen.setWidthF(2.0);
        pen.setJoinStyle(Qt::RoundJoin);
        pen.setCapStyle(Qt::RoundCap);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(frame);

        QPainterPath path;
        path.moveTo(frame.left() + 2.0, frame.bottom() - 4.0);
        path.lineTo(frame.left() + frame.width() * 0.38, frame.top() + frame.height() * 0.34);
        path.lineTo(frame.right() - 4.0, frame.top() + frame.height() * 0.52);
        painter.drawPath(path);

        painter.setPen(Qt::NoPen);
        painter.setBrush(accent);
        painter.drawEllipse
        (
            QPointF(frame.left() + 2.0, frame.bottom() - 4.0), 3.0, 3.0
        );
        painter.drawEllipse
        (
            QPointF(frame.right() - 4.0, frame.top() + frame.height() * 0.52), 3.0, 3.0
        );
        return QIcon(pixmap);
    }

    // CAM 工作区图标：往复刀路加切削方向箭头，表示加工规划与刀路。
    QIcon buildCamIcon(const QColor& stroke, const QColor& accent)
    {
        QPixmap pixmap(kWorkspaceIconSize, kWorkspaceIconSize);
        pixmap.fill(Qt::transparent);

        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const qreal inset = kWorkspaceIconMargin + 3.0;
        const QRectF frame
        (
            inset,
            inset,
            kWorkspaceIconSize - 2.0 * inset,
            kWorkspaceIconSize - 2.0 * inset
        );

        QPen pen(stroke);
        pen.setWidthF(2.0);
        pen.setJoinStyle(Qt::RoundJoin);
        pen.setCapStyle(Qt::RoundCap);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(frame);

        QPainterPath path;
        path.moveTo(frame.left() + 2.0, frame.top() + frame.height() * 0.28);
        path.lineTo(frame.right() - 2.0, frame.top() + frame.height() * 0.28);
        path.lineTo(frame.right() - 2.0, frame.top() + frame.height() * 0.68);
        path.lineTo(frame.left() + 2.0, frame.top() + frame.height() * 0.68);
        painter.drawPath(path);

        // 切削方向：沿刀路的高亮段。
        QPen accentPen(accent);
        accentPen.setWidthF(3.0);
        accentPen.setCapStyle(Qt::RoundCap);
        painter.setPen(accentPen);
        painter.drawLine
        (
            QPointF(frame.left() + 3.0, frame.top() + frame.height() * 0.28),
            QPointF(frame.right() - 6.0, frame.top() + frame.height() * 0.28)
        );
        return QIcon(pixmap);
    }
}

WorkspaceSwitcherWidget::WorkspaceSwitcherWidget(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("workspaceSwitcherRoot"));
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    setMinimumWidth(uistyle::kWorkspaceTrackWidth);
    setMaximumWidth(uistyle::kWorkspaceTrackWidth + 24);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 4, 8, 8);
    layout->setSpacing(uistyle::kGroupSpacing);

    QLabel* title = new QLabel(QStringLiteral("工作区"), this);
    title->setObjectName(QStringLiteral("workspaceSwitcherTitle"));
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);

    m_cadButton = buildWorkspaceButton
    (
        Workspace::Cad,
        QStringLiteral("CAD"),
        QStringLiteral("CAD 工作区：几何创建、编辑与整理（Ctrl+Tab 切换）")
    );
    layout->addWidget(m_cadButton);

    m_camButton = buildWorkspaceButton
    (
        Workspace::Cam,
        QStringLiteral("CAM"),
        QStringLiteral("CAM 工作区：加工规划、仿真、NC 与后处理（Ctrl+Tab 切换）")
    );
    layout->addWidget(m_camButton);

    layout->addStretch(1);
    rebuildIcons();
    refreshActiveState();
}

QToolButton* WorkspaceSwitcherWidget::buildWorkspaceButton
(
    Workspace workspace,
    const QString& title,
    const QString& description
)
{
    QToolButton* button = new QToolButton(this);
    button->setObjectName
    (
        workspace == Workspace::Cad
            ? QStringLiteral("workspaceCadButton")
            : QStringLiteral("workspaceCamButton")
    );
    button->setProperty("workspaceButton", true);
    button->setText(title);
    button->setToolTip(description);
    button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    button->setIconSize(QSize(kWorkspaceIconSize, kWorkspaceIconSize));
    button->setCheckable(true);
    button->setAutoRaise(false);
    button->setFocusPolicy(Qt::StrongFocus);
    button->setMinimumHeight(uistyle::kWorkspaceButtonHeight);
    button->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::MinimumExpanding);
    connect(button, &QToolButton::clicked, this, [this, workspace]()
    {
        if (m_syncing || m_current == workspace) return;
        setCurrentWorkspace(workspace);
        emit workspaceActivated(workspace);
    });
    return button;
}

WorkspaceSwitcherWidget::Workspace WorkspaceSwitcherWidget::currentWorkspace() const
{
    return m_current;
}

void WorkspaceSwitcherWidget::setCurrentWorkspace(Workspace workspace)
{
    if (m_current == workspace)
    {
        refreshActiveState();
        return;
    }

    m_current = workspace;
    refreshActiveState();
}

void WorkspaceSwitcherWidget::setTheme(const AppThemeColors& theme)
{
    m_theme = theme;
    rebuildIcons();
}

void WorkspaceSwitcherWidget::rebuildIcons()
{
    const QColor stroke = m_theme.textPrimaryColor.isValid()
        ? m_theme.textPrimaryColor : QColor(34, 40, 47);
    const QColor accent = m_theme.accentColor.isValid()
        ? m_theme.accentColor : QColor(58, 108, 184);
    if (m_cadButton != nullptr) m_cadButton->setIcon(buildCadIcon(stroke, accent));
    if (m_camButton != nullptr) m_camButton->setIcon(buildCamIcon(stroke, accent));
}

void WorkspaceSwitcherWidget::refreshActiveState()
{
    const auto applyState = [this](QToolButton* button, bool active)
    {
        if (button == nullptr) return;
        button->setChecked(active);
        button->setProperty("workspaceActive", active);
        // 动态属性变化后需要重新应用样式表。
        button->style()->unpolish(button);
        button->style()->polish(button);
        button->update();
    };

    m_syncing = true;
    applyState(m_cadButton, m_current == Workspace::Cad);
    applyState(m_camButton, m_current == Workspace::Cam);
    m_syncing = false;
}

void WorkspaceSwitcherWidget::wheelEvent(QWheelEvent* event)
{
    // 只有滚轮位于切换器范围内才切换工作区；事件在此被接受，不进入视口。
    const int delta = event->angleDelta().y();
    if (delta == 0)
    {
        event->ignore();
        return;
    }

    event->accept();
    emit workspaceStepRequested(delta > 0 ? -1 : 1);
}
