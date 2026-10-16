// SimulationRenderLayer 实现文件
// 仿真可视化：按 CAM 已确定的运动分类绘制已完成轨迹、当前运动段、刀具标记与状态文字。
#include "platform/pch.h"

#include "cad/view/rendering/SimulationRenderLayer.h"

#include <QFont>
#include <QFontMetrics>
#include <QPen>
#include <QStringList>

#include <algorithm>
#include <cmath>

namespace
{
    using cadcam::simulation::SimulationMoveKind;
    using cadcam::simulation::SimulationPlaybackState;
    using cadcam::simulation::SimulationPose;

    QVector3D toVector(const SimulationPose& pose)
    {
        return QVector3D
        (
            static_cast<float>(pose.x),
            static_cast<float>(pose.y),
            static_cast<float>(pose.z)
        );
    }

    // 已完成轨迹按运动分类着色：空移、切削、连续连接与过切互相可区分。
    QVector3D completedColor(SimulationMoveKind kind)
    {
        switch (kind)
        {
        case SimulationMoveKind::Rapid: return QVector3D(0.45f, 0.55f, 0.68f);
        case SimulationMoveKind::Cutting: return QVector3D(0.18f, 0.88f, 0.45f);
        case SimulationMoveKind::CuttingConnection: return QVector3D(0.15f, 0.78f, 0.82f);
        case SimulationMoveKind::Overcut: return QVector3D(1.00f, 0.52f, 0.18f);
        }
        return QVector3D(0.45f, 0.55f, 0.68f);
    }

    QVector3D currentColor(SimulationMoveKind kind)
    {
        switch (kind)
        {
        case SimulationMoveKind::Rapid: return QVector3D(0.62f, 0.80f, 1.00f);
        case SimulationMoveKind::Cutting: return QVector3D(0.35f, 1.00f, 0.55f);
        case SimulationMoveKind::CuttingConnection: return QVector3D(0.30f, 0.95f, 1.00f);
        case SimulationMoveKind::Overcut: return QVector3D(1.00f, 0.72f, 0.30f);
        }
        return QVector3D(0.62f, 0.80f, 1.00f);
    }

    QString playbackStateText(SimulationPlaybackState state)
    {
        switch (state)
        {
        case SimulationPlaybackState::Empty: return QStringLiteral("未准备");
        case SimulationPlaybackState::Ready: return QStringLiteral("就绪");
        case SimulationPlaybackState::Playing: return QStringLiteral("播放中");
        case SimulationPlaybackState::Paused: return QStringLiteral("已暂停");
        case SimulationPlaybackState::Finished: return QStringLiteral("已结束");
        }
        return QString();
    }
}

void SimulationRenderLayer::setTrajectory
(
    const cadcam::simulation::SimulationTrajectory& trajectory
)
{
    m_trajectory = trajectory;
}

void SimulationRenderLayer::clearTrajectory()
{
    m_trajectory.reset();
    m_snapshot = cadcam::simulation::SimulationSnapshot{};
}

void SimulationRenderLayer::setSnapshot
(
    const cadcam::simulation::SimulationSnapshot& snapshot
)
{
    m_snapshot = snapshot;
}

void SimulationRenderLayer::setVisible(bool visible)
{
    m_visible = visible;
}

std::vector<TransientPrimitive> SimulationRenderLayer::buildPrimitives
(
    const ViewportRenderContext& context
) const
{
    Q_UNUSED(context);

    std::vector<TransientPrimitive> primitives;

    if (!m_visible || !m_trajectory.has_value() || m_trajectory->moves.empty())
    {
        return primitives;
    }

    const std::vector<cadcam::simulation::SimulationMove>& moves = m_trajectory->moves;
    const std::size_t currentIndex = std::min(m_snapshot.moveIndex, moves.size() - 1U);

    // 已完成轨迹：按运动分类着色，每段一条线段。
    for (std::size_t index = 0; index < currentIndex; ++index)
    {
        TransientPrimitive segment;
        segment.primitiveType = GL_LINES;
        segment.color = completedColor(moves[index].kind);
        segment.opacity = moves[index].kind == cadcam::simulation::SimulationMoveKind::Rapid
            ? 0.45f : 0.9f;
        segment.vertices = { toVector(moves[index].startPose), toVector(moves[index].targetPose) };
        primitives.push_back(std::move(segment));
    }

    // 当前运动段：起点到当前位置，使用高亮色。
    {
        const cadcam::simulation::SimulationMove& move = moves[currentIndex];
        TransientPrimitive segment;
        segment.primitiveType = GL_LINES;
        segment.color = currentColor(move.kind);
        segment.opacity = 1.0f;
        segment.vertices = { toVector(move.startPose), toVector(m_snapshot.currentPose) };
        primitives.push_back(std::move(segment));

        // 当前运动的剩余部分用细虚线感（低透明度）表示，便于观察整段走向。
        TransientPrimitive remaining;
        remaining.primitiveType = GL_LINES;
        remaining.color = currentColor(move.kind);
        remaining.opacity = 0.25f;
        remaining.vertices = { toVector(m_snapshot.currentPose), toVector(move.targetPose) };
        primitives.push_back(std::move(remaining));
    }

    // 刀具标记：当前位置的十字与中心点。
    {
        const QVector3D position = toVector(m_snapshot.currentPose);
        const float pixelScale = std::max(context.pixelToWorldScale, 1.0e-4f);
        const float size = pixelScale * 9.0f;

        TransientPrimitive cross;
        cross.primitiveType = GL_LINES;
        cross.color = QVector3D(1.0f, 0.92f, 0.35f);
        cross.opacity = 1.0f;
        cross.vertices =
        {
            position - QVector3D(size, 0.0f, 0.0f), position + QVector3D(size, 0.0f, 0.0f),
            position - QVector3D(0.0f, size, 0.0f), position + QVector3D(0.0f, size, 0.0f),
            position - QVector3D(0.0f, 0.0f, size), position + QVector3D(0.0f, 0.0f, size)
        };
        primitives.push_back(std::move(cross));

        TransientPrimitive marker;
        marker.primitiveType = GL_POINTS;
        marker.color = QVector3D(1.0f, 0.95f, 0.55f);
        marker.opacity = 1.0f;
        marker.pointSize = 11.0f;
        marker.roundPoint = true;
        marker.vertices = { position };
        primitives.push_back(std::move(marker));
    }

    return primitives;
}

void SimulationRenderLayer::paintStatusOverlay
(
    QPainter& painter,
    const ViewportRenderContext& context
) const
{
    if (!m_visible || !m_trajectory.has_value() || !m_snapshot.hasTrajectory)
    {
        return;
    }

    QStringList lines;
    lines << QStringLiteral("仿真：%1").arg(playbackStateText(m_snapshot.state));
    lines << QStringLiteral("运动：%1")
        .arg(QString::fromLatin1(cadcam::simulation::simulationMoveKindName(m_snapshot.currentKind)));
    lines << QStringLiteral("Move：%1 / %2")
        .arg(m_snapshot.moveIndex + 1U)
        .arg(m_snapshot.moveCount);

    if (m_snapshot.currentPose.aDegrees.has_value())
    {
        lines << QStringLiteral("A：%1°").arg(*m_snapshot.currentPose.aDegrees, 0, 'f', 1);
    }

    lines << QStringLiteral("X：%1  Y：%2  Z：%3")
        .arg(m_snapshot.currentPose.x, 0, 'f', 2)
        .arg(m_snapshot.currentPose.y, 0, 'f', 2)
        .arg(m_snapshot.currentPose.z, 0, 'f', 2);

    QFont font = context.overlayFont;
    font.setPointSize(9);
    painter.setFont(font);
    const QFontMetrics metrics(font);

    int textWidth = 0;
    for (const QString& line : lines)
    {
        textWidth = std::max(textWidth, metrics.horizontalAdvance(line));
    }

    const int padding = 7;
    const int lineHeight = metrics.height() + 1;
    const QRect panel
    (
        10,
        10,
        textWidth + padding * 2,
        lineHeight * lines.size() + padding * 2
    );

    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setPen(QPen(QColor(120, 170, 220, 190), 1.0));
    painter.setBrush(QColor(16, 22, 30, 200));
    painter.drawRoundedRect(panel, 6.0, 6.0);

    painter.setPen(QColor(226, 236, 248));
    int lineTop = panel.top() + padding;

    for (const QString& line : lines)
    {
        painter.drawText
        (
            QRect(panel.left() + padding, lineTop, textWidth, lineHeight),
            Qt::AlignLeft | Qt::AlignVCenter,
            line
        );
        lineTop += lineHeight;
    }
}
