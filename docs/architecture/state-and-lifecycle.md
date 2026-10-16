# 状态与生命周期

## 1. 稳定身份与 Viewer 临时身份

系统区分两类身份：

| 身份 | 生命周期 | 用途 |
| --- | --- | --- |
| 稳定图元身份 | 贯穿文档、加工状态和派生结果 | 关联 CAD 图元、加工计划、机床轨迹和 NC |
| Viewer 临时身份 | 当前对象和显示缓存生命周期 | 渲染缓存、拾取、选择和 GPU 资源定位 |

加工状态、计划、轨迹和 NC 仅使用稳定图元身份。Viewer 临时身份仅在显示边界内使用，不跨对象生命周期保存，也不进入加工数据链。

## 2. 文档内容状态

以下操作改变 CAD 文档内容：

- 导入或清空文档；
- 新增、删除和复制图元；
- 移动、旋转、缩放、镜像等几何编辑；
- 图层和颜色修改；
- Undo/Redo。

文档内容变化后，旧几何捕获结果和基于旧文档生成的加工计划失效。批量操作作为一次业务动作推进一次内容状态，避免中间状态被误认为完整结果。

## 3. 加工上下文

用户加工状态、加工设置、CAD 几何快照和当前加工计划由应用层 `CamJob` 统一持有：`CamJob::geometrySnapshot()` 保存 CAM 看到的 CAD 几何，`CamJob::processState()` 保存逐图元加工输入与唯一加工单元序列，`CamJob::settings()` 保存壁厚覆盖和断面修正开关，`CamJob::processPlan()` 保存当前有效的加工计划。桌面层只持有 `CamJob` 实例本身，不再分别拥有这些状态。

几何快照与文档内容 revision 对应：`CamJob::isGeometrySnapshotCurrent(documentContentRevision)` 判断有效性，`CamJob::invalidateGeometrySnapshot()` 在 CAD 变化后使其失效；需要规划时由应用层按需重建，CAD 编辑过程中不重新编译几何。加工状态变化不影响快照有效期。

重复几何的识别结果属于加工计划：`ProcessExclusionReason::DuplicateGeometry` 记录被排除的重复图元，CAD 文档中的图元保持不变，也不进入 CAD 撤销历史。

四轴机床轨迹仍是导出调用中的临时派生值：`MachineTrajectoryService` 只接收几何快照、加工状态和加工计划，要求三者 revision 对应，失败时返回 `Conflict` 而不是自动重新规划。轨迹当前不进入 `CamJob` 长期持有。

导出是消费结果的阶段：`NcProgram`、G-code 文本和输出文件都在一次导出中产生，导出前只校验 `CamJob` 的加工计划与几何快照是否有效，不执行排序、不重建快照、不修改加工状态，也不修改 CAD 文档。

加工上下文分为用户业务输入和自动分析状态。

用户业务输入包括：

- 加工启用；
- 加工方向；
- 加工起点；
- 当前加工单元序列；
- 方管截面；
- 加工断面；
- 废面；
- 排序方式。

自动分析状态包括系统识别得到的截面、加工断面及其他加工分析结果。用户确认的截面、加工断面和废面优先于对应的自动分析状态；内部线自动清理则直接删除窗口内图元。当前加工单元序列和方向由排序入口解析：普通排序尽量保留当前序列、人工方向和人工起点，智能排序忽略当前序列并重新计算。

当前有效加工上下文与 CAD 几何共同决定加工计划。选择状态和 Viewer 显示开关仅改变交互或展示，不改变加工业务输入或自动分析状态。

用户业务输入不会因为重新规划、重新仿真或重新导出而被清除：CAD 变化使几何派生结果失效，加工状态变化使规划派生结果失效，机床运动配置变化只使轨迹与仿真失效，后处理配置变化只影响输出文本。范围定义与判定见第 5 节。

## 4. 加工单元序列状态

Core 根据统一加工路径及连续关系形成 `ProcessUnit` 集合。每个单元使用其全部稳定图元身份的排序规范集合作为 `ProcessUnitKey`。Application 保存并编辑唯一的 `ProcessUnitSequence`，序列位置产生连续的 `1..N` 加工编号。

加工单元插入、删除或移动后，Application 立即重排连续编号并推进加工状态 revision。智能排序结果直接替换当前序列，不保留隐藏的旧人工顺序。Viewer 中的选择范围和编号显示是该序列的交互投影，不拥有序列事实。

文档几何或连续关系变化后，旧加工单元身份和序列需要按新的 `ProcessUnitKey` 重新解析。能够匹配的单元可保留相对顺序，新增或变化的单元由普通排序补入；智能排序重新生成完整序列。

加工单元块内移尾和整组反向由 Application 保存完整修改前后状态，再以通用回调命令进入 `CadEditer` 的统一历史栈。
CAD 几何编辑与这两类加工状态编辑按实际发生顺序共同 Undo/Redo。回调应用成功后才迁移历史项；执行、撤销或重做失败时，命令保留在原栈，不产生跨栈迁移。
删除、批量删除和替换命令只在 CAD 命令内部维护 `CadDocument`；加工状态由 Application 的 `ProcessStateEditSync` 在同一历史项内同步，因此逐图元状态和加工单元序列的保存与恢复位置改到 Application 层，生命周期语义不变。

## 5. 派生结果与失效关系

三轴和四轴使用不同的派生链：

```text
三轴：ProcessPlan → NcProgram → G-code 文本
四轴：ProcessPlan → MachineTrajectory → NcProgram → G-code 文本
```

完整的 CAM 生命周期：

```text
CadDocument（contentRevision）
  → GeometrySnapshot（contentRevision）
  → ProcessPlan（contentRevision + processStateRevision）
  → MachineTrajectory（四轴）或 NcProgram（三轴）
  → SimulationTrajectory / 仿真会话
  → NcProgram → G-code 文本
```

### 5.1 事实源与就绪投影

真实结果对象加 revision 是唯一事实源：`CamJob` 的几何快照与加工计划各自携带来源 revision，`MachineTrajectory` 与 `NcProgram` 携带生成时的 content / processState revision。

系统不保存“CAM 当前处于哪个阶段”的独立状态变量，也不维护 `hasPlan`、`hasTrajectory`、`simulationReady` 这类并行事实。界面与前置条件使用 `application/cam/CamLifecycle.h` 的 `CamReadiness`，它完全由当前数据计算：快照是否存在且与文档 revision 一致、计划是否存在且与文档 revision 和加工状态 revision 一致、是否已有仿真会话。导出与仿真准备都直接读取这份投影，不再各自重复判断。

### 5.2 派生结果依赖表

| 派生结果 | 直接上游 | 保存的来源信息 | 当前持有者 |
| --- | --- | --- | --- |
| `GeometrySnapshot` | `CadDocument` 图元与属性 | `contentRevision` | `CamJob` |
| `ProcessPlan` | `GeometrySnapshot`、`DocumentProcessState` | `contentRevision`、`processStateRevision` | `CamJob` |
| `MachineTrajectory` | 计划、快照、加工状态、方管截面、加工配置 | `contentRevision`、`processStateRevision` | 导出与仿真调用中的临时结果 |
| `NcProgram` | 三轴：计划 + 快照；四轴：轨迹 + 计划 | `contentRevision`、`processStateRevision` | 导出与仿真调用中的临时结果 |
| `SimulationTrajectory` / 仿真会话 | 轨迹或 NC 结构化运动 | 由输入结果决定 | `SimulationController`（桌面层持有） |
| `ProcessPresentationSnapshot` | 加工计划 | 计划内容 | `CamRenderLayer` |
| G-code 文本 | NC 程序与后处理配置 | 无（一次性文本） | 导出调用中的临时结果 |

### 5.3 失效矩阵

失效入口是桌面层的 `invalidateCamDerivedResults(scope)`，范围由真实数据依赖决定（`application/cam/CamLifecycle.h`）：

| 变化 | 范围 | 失效结果 | 保留内容 |
| --- | --- | --- | --- |
| 文档几何、图层或颜色变化（含 Undo / Redo） | `GeometryDerived` | 几何快照、加工计划、CAM 展示、仿真会话 | 加工状态、加工设置、方管截面与用户确认的加工输入 |
| 加工启用、方向、起点、加工单元序列变化 | `PlanningDerived` | 加工计划、CAM 展示、仿真会话 | 几何快照、CAD 文档与加工状态本身 |
| 内部线、加工断面、废面变化 | `PlanningDerived` | 加工计划、CAM 展示、仿真会话 | 几何快照、CAD 图元与业务输入 |
| 方管截面识别或中心变化 | `PlanningDerived` | 加工计划、CAM 展示、仿真会话 | 几何快照与 CAD 文档 |
| 排序方式变化（排序即重新生成计划） | `PlanningDerived` | 旧计划、CAM 展示、仿真会话 | 几何快照与 CAD 文档 |
| 四轴规划输入变化（回转轴中心、A 轴规则、安全距离、Z 修正、过切、初始机床点、扫掠方向、转移参数），且当前存在四轴计划 | `PlanningDerived` | 加工计划、CAM 展示、仿真会话 | 几何快照与 CAD 文档 |
| 机床运动配置变化但四轴规划输入未变（例如安全抬刀开关） | `MotionDerived` | 仿真会话（轨迹与 NC 本就不缓存） | 几何快照、加工计划、CAM 展示 |
| 方管壁厚覆盖、断面正/负向修正变化 | `MotionDerived` | 仿真会话 | 几何快照、加工计划、CAM 展示 |
| 纯 G 代码格式规则变化、G 代码模式偏好变化 | `OutputDerived` | 没有需要失效的内存派生结果（文本重新生成） | 几何快照、加工计划、CAM 展示、仿真会话 |

规划输入变化的判定是 `GProfile::hasSameRotaryPlanningInputs()`，机床运动输入变化的判定是 `GProfile::hasSameMachineMotionInputs()`；两者都与生产代码实际读取的字段一致，不按参数名称猜测。

失效仅清除或拒绝使用派生结果，不反向删除用户业务输入或自动分析状态。重新生成时，系统使用当前文档、当前加工上下文和当前配置建立新结果。

加工计划的失效入口仍是 `CamJob::invalidateProcessPlan()`（由 `PlanningDerived` 复用）；`CamJob::isProcessPlanCurrent(documentContentRevision)` 判断当前计划是否仍匹配文档内容 revision 和加工状态 revision。计划整体替换，不做原位修改。撤销和重做同样按“状态发生实际变化即推进 revision”的规则处理，revision 不倒退。

### 5.4 为什么不把派生结果放进 CamJob

`MachineTrajectory`、`NcProgram`、`SimulationTrajectory` 与仿真会话都按需重新生成，当前不长期缓存：导出与仿真在需要时按有效计划重新计算。因此 `CamJob` 仍然只拥有几何快照、加工状态、加工设置和加工计划；生命周期通过 revision 与失效范围表达，而不是通过增加缓存成员表达。

## 6. 方管截面状态

方管截面的字段定义见[领域模型](./domain-model.md)。本节仅定义其来源、缺省值和失效关系。

截面输入来源优先级为：

1. 用户当前明确指定的截面参数；
2. 当前有效的自动识别结果；
3. 中心缺失时使用 `(0, 0)`。

自动识别结果不得静默覆盖用户已经确认的手动截面。用户修改截面后，所有依赖旧截面的增强分析和派生结果失效。

已识别的截面不因文档几何变化而失效：排序和轨迹继续使用当前截面参数，导出解算实际失败时再报错提示用户检查或重新设置截面。

没有有效截面尺寸和圆角时，普通四轴排序、轨迹和 G-code 导出仍可使用旋转轴中心及运动配置继续运行。表面归属、圆角刀头方向、方管实体内部路径、加工断面、废面和增强安全位置等截面增强功能不可运行。

## 7. 已知实现差异

截面中心缺失时使用 `(0, 0)`，与需求和概要设计一致：轨迹服务在无有效截面且无显式中心时直接使用 `(0, 0)`，不再使用路径 YZ 包围盒中心。

当前生产实现由 Application 保存唯一 `ProcessUnitSequence`，三轴和四轴计划均生成 `ProcessUnitKey`。普通排序保留现有单元序列，智能排序重建单元序列；`CamRenderLayer` 按加工单元显示编号和起点方向箭头，并支持连续范围内的块内移尾，加工单元序列的修改始终由桌面层完成。

## 8. 加工展示状态

加工顺序、方向、加工单元和计划排除项从当前有效加工计划生成显示快照。目标显示以加工单元为粒度，每个单元只显示一个编号。内部线等即时用户加工状态即使使旧计划失效，也应继续通过当前业务状态显示。

展示快照随有效计划更新或清除：计划成功后桌面层构建快照并交给 `CamRenderLayer` 持有，计划失效时清除，避免旧顺序、箭头和排除状态继续显示。展示快照仅反映业务状态，不承担计划或加工输入的持久化职责，也不进入 `CamJob`。

仿真会话同样是临时派生状态：`SimulationController` 只在桌面层“准备仿真”时加载一次运动结果，播放进度、播放倍率与当前位姿都只存在于本次会话内；计划失效时清除会话，避免继续播放过期轨迹。`MachineTrajectory` 与 `NcProgram` 仍不进入 `CamJob` 的长期成员。

## 相关需求

MACH-041、MACH-042、MACH-043、PROCESS-018、PROCESS-019、PROCESS-021、PROCESS-022、PROCESS-023、EDIT-029、EDIT-030、EDIT-031、EDIT-033。