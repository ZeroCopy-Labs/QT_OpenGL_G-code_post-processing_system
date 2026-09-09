#include "ui/dialogs/ToolTransferDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

class TransferPathPreview : public QWidget
{
public:
    explicit TransferPathPreview(QWidget* parent) : QWidget(parent) { setMinimumSize(440, 300); }
    std::vector<cadcam::machine::MachinePose4D> points;
protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), palette().base());
        painter.setPen(palette().text().color());
        painter.drawText(QRect(16, 10, width() - 32, 28), Qt::AlignCenter,
            QStringLiteral("同区位转移剖面 · 横轴为 XY 行程，纵轴为 Z（非等比）"));
        if (points.empty()) return;
        double low = points.front().z, high = low;
        for (const auto& p : points) { low = std::min(low, p.z); high = std::max(high, p.z); }
        const double spanZ = std::max(1.0, high - low);
        const double spanX = std::max(1.0, points.back().x);
        const auto screen = [&](const auto& p)
        {
            return QPointF(48 + p.x / spanX * (width() - 96),
                height() - 62 - (p.z - low) / spanZ * (height() - 130));
        };
        painter.setPen(QPen(palette().mid().color(), 1, Qt::DashLine));
        painter.drawLine(screen(points.front()), screen(points.back()));
        for (std::size_t i = 1; i < points.size(); ++i)
        {
            const auto& a = points[i - 1];
            const auto& b = points[i];
            const bool vertical = std::abs(b.x - a.x) < 1.0e-8;
            const QColor color = std::abs(b.z - a.z) < 1.0e-8 ? QColor("#8b5cf6")
                : b.z > a.z ? QColor(vertical ? "#2563eb" : "#d97706")
                : QColor(vertical ? "#16a34a" : "#0891b2");
            painter.setPen(QPen(color, 3));
            const auto from = screen(a), to = screen(b);
            painter.drawLine(from, to);
            painter.setBrush(color);
            painter.drawEllipse(to, 4, 4);
            const auto middle = (from + to) * 0.5;
            painter.drawText(middle + QPointF(7, -7), QString::number(i));
            const double angle = std::atan2(to.y() - from.y(), to.x() - from.x());
            painter.drawLine(middle, middle - QPointF(9 * std::cos(angle - 0.45), 9 * std::sin(angle - 0.45)));
            painter.drawLine(middle, middle - QPointF(9 * std::cos(angle + 0.45), 9 * std::sin(angle + 0.45)));
        }
        painter.setPen(palette().text().color());
        painter.drawText(screen(points.front()) + QPointF(-20, 24), QStringLiteral("加工结束"));
        painter.drawText(screen(points.back()) + QPointF(-25, 24), QStringLiteral("下个入口"));
        painter.drawText(QRect(12, height() - 28, width() - 24, 24), Qt::AlignCenter,
            QStringLiteral("蓝：安全抬刀   橙：联动抬刀   紫：平移   青：联动落刀   绿：安全落刀"));
    }
};

ToolTransferDialog::ToolTransferDialog(const GProfileToolTransferConfig& value, QWidget* parent)
    : QDialog(parent), m_original(value)
{
    setObjectName(QStringLiteral("ToolTransferDialog"));
    setWindowTitle(QStringLiteral("安全约束下的多轴联动转移"));
    resize(960, 570);
    auto* root = new QVBoxLayout(this);
    auto* description = new QLabel(QStringLiteral(
        "同区位且 A 角不变时：安全抬刀 → 联动抬刀 → 平移 → 联动落刀 → 安全落刀。\n"
        "关闭联动时使用抬刀 → 平移 → 落刀。跨区位仍使用原旋转包络安全策略。"), this);
    description->setWordWrap(true);
    root->addWidget(description);
    auto* body = new QHBoxLayout;
    auto* controls = new QGroupBox(QStringLiteral("运动参数"), this);
    auto* form = new QFormLayout(controls);
    const auto spin = [&](const char* name, double maximum, const QString& suffix)
    {
        auto* input = new QDoubleSpinBox(controls);
        input->setObjectName(QString::fromLatin1(name));
        input->setRange(0.0, maximum);
        input->setDecimals(3);
        input->setSingleStep(0.1);
        input->setSuffix(suffix);
        return input;
    };
    m_enabled = new QCheckBox(QStringLiteral("启用多轴联动转移"), controls);
    m_enabled->setObjectName(QStringLiteral("transferEnabled"));
    m_enabled->setChecked(value.coordinatedTransferEnabled);
    form->addRow(m_enabled);
    m_retract = spin("minimumRetractHeight", 1000000, QStringLiteral(" mm"));
    m_approach = spin("minimumApproachHeight", 1000000, QStringLiteral(" mm"));
    m_height = spin("totalTransferHeight", 1000000, QStringLiteral(" mm"));
    m_departureRatio = spin("departureRatio", 100, QStringLiteral(" %"));
    m_approachRatio = spin("approachRatio", 100, QStringLiteral(" %"));
    m_retract->setValue(value.sameZone.minimumRetractHeight);
    m_approach->setValue(value.sameZone.minimumApproachHeight);
    m_height->setValue(value.sameZone.totalHeight(value.sameZoneTransferClearance));
    m_departureRatio->setValue(value.sameZone.departureRatio * 100);
    m_approachRatio->setValue(value.sameZone.approachRatio * 100);
    m_autoHeight = new QCheckBox(QStringLiteral("总高度自动取两个最小高度的较大值"), controls);
    m_autoHeight->setObjectName(QStringLiteral("automaticTotalHeight"));
    m_autoHeight->setChecked(value.sameZone.automaticTotalHeight);
    form->addRow(QStringLiteral("最小安全抬刀高度"), m_retract);
    form->addRow(QStringLiteral("最小安全落刀高度"), m_approach);
    form->addRow(m_autoHeight);
    form->addRow(QStringLiteral("联动抬刀总高度"), m_height);
    form->addRow(QStringLiteral("联动抬刀水平占比"), m_departureRatio);
    form->addRow(QStringLiteral("联动落刀水平占比"), m_approachRatio);
    m_distance = spin("exampleDistance", 1000000, QStringLiteral(" mm"));
    m_distance->setValue(100);
    m_zDifference = spin("exampleZDifference", 1000000, QStringLiteral(" mm"));
    m_zDifference->setMinimum(-1000000);
    form->addRow(new QLabel(QStringLiteral("下列两项仅改变示意图，不保存到工艺："), controls));
    form->addRow(QStringLiteral("示例 XY 距离"), m_distance);
    form->addRow(QStringLiteral("示例终点 Z 差"), m_zDifference);
    body->addWidget(controls);
    m_preview = new TransferPathPreview(this);
    body->addWidget(m_preview, 1);
    root->addLayout(body, 1);
    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("transferPreviewStatus"));
    m_status->setWordWrap(true);
    root->addWidget(m_status);
    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    root->addWidget(m_buttons);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    for (auto* input : { m_retract, m_approach, m_height, m_departureRatio, m_approachRatio, m_distance, m_zDifference })
        connect(input, &QDoubleSpinBox::valueChanged, this, [this]() { refreshPreview(); });
    connect(m_autoHeight, &QCheckBox::toggled, this, [this]() { refreshPreview(); });
    connect(m_enabled, &QCheckBox::toggled, this, [this]() { refreshPreview(); });
    refreshPreview();
}

GProfileToolTransferConfig ToolTransferDialog::config() const
{
    auto value = m_original;
    value.coordinatedTransferEnabled = m_enabled->isChecked();
    value.sameZone.minimumRetractHeight = m_retract->value();
    value.sameZone.minimumApproachHeight = m_approach->value();
    value.sameZone.automaticTotalHeight = m_autoHeight->isChecked();
    value.sameZoneTransferClearance = m_height->value();
    value.sameZone.departureRatio = m_departureRatio->value() / 100;
    value.sameZone.approachRatio = m_approachRatio->value() / 100;
    return value;
}

OperationResult<cadcam::machine::RotaryTransferPreview> ToolTransferDialog::examplePreview() const
{
    using namespace cadcam;
    const auto value = config();
    machine::RotaryTransferRequest request;
    request.previousProcessUnitIndex = 0;
    request.nextProcessUnitIndex = 1;
    request.previousOwnerZone = request.nextOwnerZone = static_cast<machining::TubeZone16>(0);
    request.nextCutStart = { m_distance->value(), 0, m_zDifference->value(), 0 };
    request.policy.rotationSafetyClearance = value.rotationSafetyClearance;
    request.policy.sameZoneTransferClearance = value.sameZoneTransferClearance;
    request.policy.coordinatedTransferEnabled = value.coordinatedTransferEnabled;
    request.policy.sameZone = value.sameZone;
    return machine::RotaryTransferPlanner::preview(request, {});
}

void ToolTransferDialog::refreshPreview()
{
    const double minimum = std::max(m_retract->value(), m_approach->value());
    {
        const QSignalBlocker blocker(m_height);
        m_height->setMinimum(minimum);
        if (m_autoHeight->isChecked()) m_height->setValue(minimum);
    }
    m_height->setEnabled(!m_autoHeight->isChecked());
    m_departureRatio->setEnabled(m_enabled->isChecked());
    m_approachRatio->setEnabled(m_enabled->isChecked());
    const auto preview = examplePreview();
    const bool valid = preview.succeeded() && preview.value.has_value();
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(valid);
    m_preview->points.clear();
    if (valid)
    {
        m_preview->points.push_back({});
        for (const auto& target : preview.value->targets) m_preview->points.push_back(target);
        double planarDistance = 0.0;
        for (std::size_t i = 1; i < m_preview->points.size(); ++i)
        {
            const auto& a = m_preview->points[i - 1];
            const auto& b = m_preview->points[i];
            if (std::abs(a.z - b.z) < 1.0e-8) planarDistance += std::abs(b.x - a.x);
        }
        m_status->setText(QStringLiteral("实际 %1 段 · 平移可用行程 %2% · 零长度阶段自动合并。\n%3")
            .arg(preview.value->targets.size())
            .arg(m_distance->value() > 0.0 ? 100 * planarDistance / m_distance->value() : 0.0, 0, 'f', 1)
            .arg(m_height->value() <= 0.0
                ? QStringLiteral("注意：总高度为 0，同区位不离面。请根据工件和夹具设置安全高度。")
                : QStringLiteral("总高度以较高端 Z 为基准。示意图与生产轨迹共用算法；未模拟夹具或障碍物碰撞。")));
    }
    else m_status->setText(QStringLiteral("参数无效：两项水平行程占比合计不能超过 100%，总高度不能低于任一最小高度。"));
    m_preview->update();
}
