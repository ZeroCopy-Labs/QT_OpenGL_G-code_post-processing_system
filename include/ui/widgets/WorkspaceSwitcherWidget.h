#pragma once

#include "desktop/AppTheme.h"

#include <QWidget>

class QToolButton;

// 主视口右侧的工作区切换轨道：每个工作区一个大按钮，图标与文字同时显示。
//
// 滚轮只在切换器自身范围内切换工作区，不使用全局事件过滤器，
// 因此不会影响三维视口的缩放等导航操作。
class WorkspaceSwitcherWidget : public QWidget
{
    Q_OBJECT

public:
    enum class Workspace
    {
        Cad,
        Cam
    };

    explicit WorkspaceSwitcherWidget(QWidget* parent = nullptr);

    Workspace currentWorkspace() const;
    void setCurrentWorkspace(Workspace workspace);
    void setTheme(const AppThemeColors& theme);

signals:
    void workspaceActivated(Workspace workspace);
    // 滚轮切换请求：delta 为 +1 表示下一个工作区，-1 表示上一个。
    void workspaceStepRequested(int delta);

protected:
    void wheelEvent(QWheelEvent* event) override;

private:
    QToolButton* buildWorkspaceButton
    (
        Workspace workspace,
        const QString& title,
        const QString& description
    );
    void rebuildIcons();
    void refreshActiveState();

    AppThemeColors m_theme;
    QToolButton* m_cadButton = nullptr;
    QToolButton* m_camButton = nullptr;
    Workspace m_current = Workspace::Cad;
    bool m_syncing = false;
};
