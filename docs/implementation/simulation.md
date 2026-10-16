# CAM 仿真详细设计

## 职责

仿真把已经计算完成的机床运动结果按段回放，并在共享视口中显示当前机床位置、当前运动与已完成轨迹。
仿真只消费 CAM 结果，不重新规划、不修改 CAD、不修改加工状态，也不解析 G-code。

```text
ProcessPlan
    ↓
Machine Motion Result（四轴 MachineTrajectory / 三轴既有结构化运动）
    ↓
SimulationTrajectory
    ↓
SimulationController（播放会话）
    ↓
SimulationSnapshot
    ↓
SimulationRenderLayer
    ↓
共享视口
```

## 生产入口

- 桌面层 CAM 工作区“仿真”页提供“准备仿真 / 播放 / 暂停 / 单步 / 复位”与播放倍率、进度显示。
- “准备仿真”先校验 `CamJob` 当前计划与几何快照是否有效，再生成运动结果并加载仿真会话。
- 播放由桌面层定时器按真实经过时间推进 `SimulationController::advance()`，每帧把 `SimulationSnapshot` 交给渲染层后请求重绘。
- 计划失效（`invalidateCurrentProcessPlan()`）时同时清除当前仿真会话，避免继续播放过期轨迹。

## 输入与输出

输入：

- 四轴：`GeometrySnapshot` + `DocumentProcessState` + `Rotary4Axis ProcessPlan` + 截面与加工配置 → `MachineTrajectoryService::buildRotaryTrajectory()`。
- 三轴：`GeometrySnapshot` + `DocumentProcessState` + `Planar3Axis ProcessPlan` + 加工配置 → `NcProgramService::buildPlanarProgram()` 得到的结构化 `NcProgram`。

输出：`SimulationTrajectory`（仿真输入）、`SimulationSnapshot`（每帧展示状态）和视口内的轨迹与刀具标记图元。

仿真不读取 `CadDocument`、`CadItem`，不需要图元几何；需要标识时只使用 `EntityId`。

## 主要数据类型

- `SimulationMoveKind`：`Rapid` / `Cutting` / `CuttingConnection` / `Overcut`，直接沿用 CAM 已确定的运动分类。
- `SimulationPose`：机床 X/Y/Z 与可选 A 角（度）；A 角取 CAM 已算出的目标值。
- `SimulationMove`：起点、终点、运动分类、`EntityId`、加工组与加工单元索引。
- `SimulationTrajectory`：`contentRevision`、`processStateRevision`、来源模式、起始位姿与运动序列。
- `SimulationPlaybackState`：`Empty` / `Ready` / `Playing` / `Paused` / `Finished`，只表示播放会话状态。
- `SimulationSnapshot`：播放状态、当前运动序号与段内进度、整体进度、当前位姿、当前运动分类、当前图元与加工单元。
- `SimulationRenderLayer`：只消费 `SimulationTrajectory` 与 `SimulationSnapshot`，绘制已完成轨迹、当前运动段、刀具标记与状态文字。

## 三轴 / 四轴当前输入差异

四轴使用正式链路：`MachineTrajectory` 是完整的机床运动结果，直接按 `approachMoves` / `cuttingMoves` / `overcutMoves` 转换为仿真运动。

三轴当前使用桥接输入：现有三轴结构化运动只保存在 `NcProgram::NcEntityBlock::motions` 中，因此仿真通过内存适配读取这些运动：

```text
ProcessPlan
→ 既有三轴 NC 结构化生成
→ NcProgram
→ SimulationTrajectoryAdapter::fromNcProgram()
→ SimulationTrajectory
```

这条路径不生成 G-code 文本、不做后处理、不写文件、也不解析 G-code。使用 `NcProgram` 作为桥接是为了复用现有三轴运动表示，不是长期架构。

## 播放语义

- 归一化播放：一段运动固定占用同一播放时长（`SimulationController::kSecondsPerMove`），不模拟真实机床时间、进给率、加速度或物理时长。
- 段内进度 0→1，按起点与终点插值得到当前位姿；X/Y/Z 与 A 角都做数值插值，A 角不重新计算角度规则、不处理绕圈或旋转规划。
- `Play` 从当前进度继续播放；`Pause` 停止推进但保留进度；`Step` 前进一整段运动并保持暂停；`Reset` 回到起点并变为就绪。
- 播放到终点后状态为 `Finished`，再次播放会从头开始。
- 播放倍率（0.25×/0.5×/1×/2×/4×）只是播放速度，不写入 `MachiningProcessConfig`、`GProfile` 或 `CamJobSettings`，也不进入持久化配置。
- 进度条只负责用户操作，真正的播放状态由 `SimulationController` 保存。

## 显示内容

- 已完成轨迹：每段运动一条线段，按 `Rapid` / `Cutting` / `CuttingConnection` / `Overcut` 分别着色，空移降低不透明度。
- 当前运动：起点到当前位置使用高亮色，当前位置到目标点的剩余部分以低不透明度显示。
- 刀具标记：当前位置的十字与圆点，尺寸按屏幕像素换算。
- 状态文字：播放状态、当前运动分类、`Move N / M`、A 角与机床坐标，同时显示在视口左上角与 CAM 面板。

## 状态所有权与边界

- `SimulationController` 只拥有本次仿真的 `SimulationTrajectory` 与播放进度，不持有 CAD、`CamJob`、`ProcessPlan`。
- `SimulationRenderLayer` 只持有一份仿真展示数据副本与当前 `SimulationSnapshot`，不引用控制器。
- `CamJob` 仍只拥有 `GeometrySnapshot`、`DocumentProcessState`、`CamJobSettings` 与 `ProcessPlan`；`MachineTrajectory` 与 `NcProgram` 仍是派生运行结果，只在本次仿真会话内使用。
- 仿真不调用 `DocumentProcessState` 的写入接口、不调用 `CadEditer`、不修改 `CamJob`，因此不产生任何 Undo/Redo 历史项。
- 仿真复用 Phase 7 的共享视口：`CadViewer` 内并列 `CadRenderLayer`、`SelectionRenderLayer`、`CamRenderLayer` 与 `SimulationRenderLayer`，不新建第二个 Viewer 或第二个 OpenGL Widget。

## 失效条件

- 没有有效加工计划或计划已失效：拒绝准备仿真并提示重新执行 Planning。
- 几何快照与当前文档 revision 不一致：拒绝准备仿真。
- 四轴轨迹、元数据、计划与加工状态 revision 不一致：既有服务返回 `Conflict`，仿真不自动修复。
- 计划失效（CAD 变化、加工状态变化、排序或配置变化）时清除当前仿真会话。
- 机床运动配置变化（回转轴、安全抬刀、Z 修正、过切、初始机床点、转移参数）与方管壁厚、断面修正变化也清除当前仿真会话；此时几何快照、加工计划与 CAM 展示保持有效。
- 仅 G 代码规则或 G 代码模式偏好变化时不清除仿真会话：它们不参与轨迹与 NC 计算。
- 播放倍率与进度只影响展示，不影响任何加工结果。

## 关键业务规则

- 仿真是结果的消费者：不重新判断何处应为空移、切削、连续连接或过切。
- 运动分类、加工顺序、方向、起点、A 轴角度与过切全部沿用 CAM 已算出的结果。
- 三轴圆弧等运动在仿真中按起点到终点的直线段展示，不重新推导 NC 几何。
- 仿真不修改 CAD 文档，也不推进文档内容 revision 或加工状态 revision。

## 当前明确不实现的能力

- 碰撞检测、材料去除、刀具实体模型、真实机床模型。
- 物理动力学、速度/加速度/进给模拟、热力学与加工力。
- G-code 解释器与 G-code → 仿真回放。
- 第二个 Viewer、独立仿真窗口、仿真状态持久化。

## 相关源码

- `src/application/simulation/SimulationController.cpp`：播放会话状态、推进、单步、复位与展示快照。
- `src/application/simulation/SimulationTrajectoryAdapter.cpp`：既有运动结果到仿真输入的整理。
- `src/cad/view/rendering/SimulationRenderLayer.cpp`：仿真轨迹、刀具标记与状态文字绘制。
- `src/desktop/Gcode_postprocessing_system_SimulationActions.cpp`：准备仿真、播放控制、状态同步与计划失效清理。
- `src/ui/widgets/CamToolPanelWidget.cpp`：CAM 工作区“仿真”页的最小控制与状态显示。
- `src/application/machine/MachineTrajectoryService.cpp`：四轴机床轨迹生产（未修改）。
- `src/application/nc/NcProgramService.cpp`：三轴结构化运动生产（未修改）。

## 对应需求与概要设计

概要设计：[领域模型](../architecture/domain-model.md)、[数据流](../architecture/data-flow.md)、[模块边界](../architecture/module-boundaries.md)、[状态与生命周期](../architecture/state-and-lifecycle.md)。
