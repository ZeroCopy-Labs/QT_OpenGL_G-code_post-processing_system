# 界面体系与交互规范

## 目标

界面负责清晰、统一和易用；三维视口负责工程信息表达；复杂功能通过分层与按需展开管理。
本规范覆盖字号、控件尺寸、留白、工作区导航与面板状态，是所有界面改动的共同起点。

## 视觉规范单一来源

`include/ui/UiStyleSpec.h` 与 `src/ui/UiStyleSpec.cpp` 是字号、控件高度与留白的唯一定义处：

| 元素 | 取值 |
| --- | --- |
| 全局基础字体 | 10.5 pt（`kFontSizeBase`） |
| 次要状态、辅助信息 | 9.5 pt（`kFontSizeSecondary`） |
| 分组标题、强调文字 | 11.5 pt 加粗（`kFontSizeGroupTitle`） |
| 常用操作按钮高度 | 36 px（`kButtonHeight`） |
| 面板内紧凑按钮高度 | 32 px（`kButtonHeightCompact`） |
| 下拉框、数字输入框高度 | 32 px（`kControlHeight`） |
| CAD / CAM 工作区按钮高度 | 72 px（`kWorkspaceButtonHeight`） |
| 面板分组间距 / 内边距 | 10 px（`kGroupSpacing` / `kPanelPadding`） |
| 加工设置面板宽度 | 最小 300 px，默认 344 px |

`uistyle::metricsStyleSheet(theme)` 输出统一控件高度、禁用态、菜单与标签页留白、停靠面板标题和工作区切换按钮的样式；主窗口在设置主题样式表时一次合成：

```cpp
setStyleSheet(themeStyleSheet + uistyle::metricsStyleSheet(theme));
```

字号与间距在这里集中定义，控件本身只设 `minimumHeight` 与伸缩策略，因此放大字体或调整 Windows 显示缩放时文本不会裁剪。

## 工作区导航

工作区切换是主视口右侧的固定窄轨道，由 `WorkspaceSwitcherWidget` 承载：

- 每个工作区一个大按钮，图标与文字同时显示（CAD 为几何轮廓图标，CAM 为刀路图标）。
- 当前工作区使用强调色边框、加粗文字与选中背景，其余按钮保持中性样式。
- 鼠标点击切换；切换器范围内滚轮向上 / 向下切换工作区。
- 键盘 `Ctrl+Tab` 与 `Ctrl+Shift+Tab` 切换工作区。

滚轮只在切换器自身范围内生效：`WorkspaceSwitcherWidget::wheelEvent` 处理并接受事件，主窗口不使用全局事件过滤器，三维视口的缩放等导航操作不受影响。

轨道停靠在 `Qt::RightDockWidgetArea`，不可移动与关闭；加工设置面板通过 `splitDockWidget` 固定在其右侧，形成「视口 | 工作区轨道 | 加工设置」的顺序。

切换工作区只改变显示与操作上下文：共享视口、CAD 文档、加工状态、几何快照与加工计划都不重建。

## 加工设置面板的显示偏好

面板可见性由用户偏好决定，与工作区行为分离：

| 场景 | 行为 |
| --- | --- |
| 首次启动，没有用户偏好 | 进入 CAM 时显示加工设置（默认 `true`） |
| 用户在 CAM 中关闭面板 | 当前会话保持关闭，并记录偏好 |
| 从 CAM 切换到 CAD | 隐藏面板，**不**改变偏好 |
| 再次进入 CAM | 按保存的偏好恢复 |
| 程序关闭并重启 | 恢复上次的选择；宽度与停靠位置由 `saveState` / `restoreState` 恢复 |

- 偏好键：`ui/cam/machiningSettingsVisible`（默认 `true`）。
- `setMachiningSettingsVisible(visible, recordPreference)` 是唯一入口：只有用户在 CAM 工作区的显式操作才写入偏好。
- 启动流程只读取偏好，不使用 `isVisible()` 判断用户意图：窗口尚未显示时任何子控件的 `isVisible()` 都是 `false`。
- CAM 工作区始终保留「加工设置」入口（加工面板按钮、机加工菜单与视图菜单），关闭侧边栏后可以一键重新打开。

## 面板与工具条

- CAD 与 CAM 工具面板共用 `ui/widgets/ToolPanelStyle.h` 的常量与样式表，字体、控件高度、留白与选中反馈来自同一处。
- 工具面板只设最小高度，不再限制最大高度；功能区按钮使用最小尺寸与 `MinimumExpanding` 策略，面板网格使用 `SetMinAndMaxSize`，允许随字体增长。
- 面板页脚提示行、状态文字行、分隔线与内联按钮高度统一取规范值。
- 视口下方的命令输入区与状态信息区使用规范字号，高度按字体度量与规范取较大值。

## 主题

浅色、深色与自定义主题机制保持不变：颜色仍由 `desktop/AppTheme.h` 与主题样式表决定，本规范只定义字号、尺寸与留白。工作区切换图标在主题变化时按当前主题文字色与强调色重绘。

## 相关源码

- `include/ui/UiStyleSpec.h` / `src/ui/UiStyleSpec.cpp`：字号与尺寸规范、统一控件样式。
- `include/ui/widgets/WorkspaceSwitcherWidget.h` / `src/ui/widgets/WorkspaceSwitcherWidget.cpp`：工作区切换轨道。
- `src/desktop/Gcode_postprocessing_system.cpp`：主题应用、工作区切换、面板偏好持久化。
- `include/ui/widgets/ToolPanelStyle.h` / `src/ui/widgets/ToolPanelStyle.cpp`：CAD / CAM 工具面板样式。
- `src/ui/widgets/CadToolPanelWidget.cpp`、`src/ui/widgets/CamToolPanelWidget.cpp`：工具面板尺寸与布局。
- `src/ui/widgets/CadCommandLineWidget.cpp`、`src/ui/widgets/CadStatusPaneWidget.cpp`：命令输入区与状态信息区。
- `src/ui/widgets/MachiningSettingsWidget.cpp`：加工设置面板内容与间距。
