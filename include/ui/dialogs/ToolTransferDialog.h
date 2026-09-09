#pragma once

#include "infrastructure/config/GProfile.h"
#include "core/machine/RotaryTransferPlanner.h"
#include <QDialog>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QDialogButtonBox;
class TransferPathPreview;

class ToolTransferDialog : public QDialog
{
public:
    explicit ToolTransferDialog(const GProfileToolTransferConfig& config, QWidget* parent = nullptr);
    GProfileToolTransferConfig config() const;
    OperationResult<cadcam::machine::RotaryTransferPreview> examplePreview() const;

private:
    void refreshPreview();
    GProfileToolTransferConfig m_original;
    QCheckBox* m_enabled = nullptr;
    QCheckBox* m_autoHeight = nullptr;
    QDoubleSpinBox* m_retract = nullptr;
    QDoubleSpinBox* m_approach = nullptr;
    QDoubleSpinBox* m_height = nullptr;
    QDoubleSpinBox* m_departureRatio = nullptr;
    QDoubleSpinBox* m_approachRatio = nullptr;
    QDoubleSpinBox* m_distance = nullptr;
    QDoubleSpinBox* m_zDifference = nullptr;
    QLabel* m_status = nullptr;
    QDialogButtonBox* m_buttons = nullptr;
    TransferPathPreview* m_preview = nullptr;
};
