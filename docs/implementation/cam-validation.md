# CAM 数据一致性验证详细设计

## 职责

`CamValidation` 检查已经存在的 CAM 值对象之间是否自洽、完整、可继续使用，并在验证失败时只返回诊断，由调用方决定下一步。

```text
CamLifecycle   → 什么变化以后旧结果必须失效
CamReadiness   → 当前是否具备进入某阶段的基本条件
CamValidation  → 当前已有数据彼此是否一致、完整、可信
```

验证是只读操作：

- 不修改 `CamJob`、`ProcessPlan`、`GeometrySnapshot`、`DocumentProcessState` 与 CAD 文档；
- 不调用 `CadEditer`，不触发排序、规划、轨迹生成或 NC 生成；
- 不自动修复 revision，不自动删除图元，不自动重建结果；
- 不产生 Undo/Redo；
- 不保存验证结果，`CamJob` 与 `CamReadiness` 都不新增 `validationPassed` 之类的字段。

验证结果是“一次计算的结论”，事实源始终是各个结果对象自身的来源 revision 与结构。

## 生产入口

| 入口 | 调用位置 | 验证内容 | 失败行为 |
| --- | --- | --- | --- |
| 计划发布 | 桌面层加工计划发布边界（普通排序、智能排序、加工单元重排、加工单元整组反向） | `validateProcessPlan` | 计划不写入 `CamJob`，命令栏输出诊断并以消息框提示 |
| 四轴轨迹生成 | `MachineTrajectoryService::buildRotaryTrajectory()` 返回前 | `validateMachineTrajectory` | 轨迹被丢弃，返回验证的诊断与状态 |
| 三轴 NC 生成 | `NcProgramService::buildPlanarProgram()` 返回前 | `validateNcProgram` | NC 被丢弃，返回验证的诊断与状态 |
| 四轴 NC 生成 | `NcProgramService::buildRotaryProgram()` 返回前 | `validateNcProgram`（同时对照机床轨迹） | NC 被丢弃，返回验证的诊断与状态 |
| 仿真准备 | 桌面层 `prepareSimulation()` | `validateProcessPlan` | 拒绝准备仿真并提示重新 Planning |
| 导出前置 | 桌面层 `prepareDocumentForGCodeExport()` | 通过 `CamReadiness` 判定后由 NC 服务验证 | 导出中止 |

验证失败不会触发重新规划、重新生成轨迹或重新生成 NC。

## 输入与输出

输入：已经生成好的值对象（`CamJob`、`GeometrySnapshot`、`DocumentProcessState`、`ProcessPlan`、可选的 `MachineTrajectory`、`NcProgram`）与当前文档内容 revision、`OperationContext`。

输出：`OperationReport` 与 `Diagnostic`。

状态语义沿用项目已有约定：

| 情况 | 状态 |
| --- | --- |
| revision 不一致、引用的图元已不在快照中 | `Conflict` |
| 缺少必要输入（没有快照、没有计划、四轴 NC 缺少轨迹） | `InvalidInput` |
| 内部结构不变量被破坏（单元结构、前置约束、覆盖关系、非有限数值、启停代码非法） | `InternalError` |

诊断字段：`component`（`CamValidation`）、`operation`、`stage`、`userMessage`、`technicalDetail`、`correlationId`、`entityId`（适用时）与 `context`。

## CamJob Validation

`validateCamJob(job, documentContentRevision, context)` 复用 `CamJob` 已有判定，不复制 revision 规则：

- 几何快照必须存在，并且 `CamJob::isGeometrySnapshotCurrent()` 成立；
- 加工计划必须存在，并且 `CamJob::isProcessPlanCurrent()` 成立。

计划与快照之间的跨对象一致性交给 `validateProcessPlan`。

## ProcessPlan Validation

`validateProcessPlan(snapshot, processState, plan, context)` 覆盖：

1. **Revision**：`plan.contentRevision == snapshot.contentRevision`；`plan.processStateRevision == processState.revision()`。
2. **快照身份**：几何快照中的图元编号必须非 0 且不重复。
3. **计划结构**：复用 Core 的共享纯验证函数 `cadcam::planning::validateProcessUnitStructure()` 与 `cadcam::planning::satisfiesPrecedenceConstraints()`（后者由规划服务内部的同名校验抽取而来，规划过程与验证共用同一实现）。
4. **图元引用**：加工分配、加工单元成员、加工组、计划片段与排除项引用的 `EntityId` 必须都能在几何快照中找到；缺失时报 `ProcessPlanEntityMissing`。
5. **排除与分配不重叠**：同一图元不能既参与加工又被排除。
6. **加工断面端面归属**：编号必须唯一且非负。

加工顺序连续性、加工单元索引范围、单元键规范性与片段参数合法性由共享的 `validateProcessUnitStructure()` 覆盖，验证模块不重复实现。

## MachineTrajectory Validation

`validateMachineTrajectory(snapshot, processState, plan, trajectory, context)` 先执行 `validateProcessPlan`，然后检查：

- `trajectory.contentRevision == plan.contentRevision`；
- `trajectory.processStateRevision == plan.processStateRevision`；
- 轨迹实体的 `EntityId` 必须来自计划分配，不允许重复或计划外图元；
- 加工单元归属必须与分配一致，并且轨迹内的加工单元顺序单调不减；
- 加工组编号若存在必须对应计划中的加工组；
- `sourceIndex` 与 `sourceKind` 必须与几何快照一致（轨迹只能来自同一份几何事实）；
- 三类运动的坐标与源路径坐标必须为有限值。

验证不重新计算轨迹，也不判断何处应为空移、切削、连续连接或过切。

## NcProgram Validation

`validateNcProgram(snapshot, processState, plan, trajectory, program, context)` 先执行 `validateProcessPlan`，然后检查：

- `program.contentRevision`、`program.processStateRevision` 与计划一致；
- `program.mode` 与计划模式一致；
- 四轴程序必须提供对应的机床轨迹，并校验轨迹 revision 与计划一致；
- 实体块的 `metadata.entityId` 必须来自计划分配，不允许重复或计划外图元；
- 实体块的加工单元归属必须与分配一致；
- 加工顺序与加工组来源随模式不同：三轴对照计划分配，四轴对照生成该 NC 的机床轨迹实体；
- `metadata.sourceIndex` 与 `metadata.sourceKind` 必须与几何快照一致；
- 运动自身的 `entityId` 必须属于所属实体块（或为 0）；
- 轴字（X/Y/Z/A/I/J/K/R）必须为有限值；
- 切割控制只允许“首块启用、末块停用”的合法组合：三轴不出现启停代码，四轴每个加工单元恰好一次启用与一次停用。

三轴链路不使用 `MachineTrajectory`，因此不要求提供它。

## 与已有局部验证的关系

| 已有验证 | 归属 | 本阶段处理 |
| --- | --- | --- |
| `validateProcessUnitStructure()` | Core 计划结构 | 直接复用，不重新实现 |
| `satisfiesPrecedenceConstraints()` | 规划服务内部 | 抽取为 Core 共享纯函数，规划与验证共用 |
| `ProcessPlanningService` 的 revision / 输入校验 | 规划阶段 | 保留；发布边界再验证一次跨对象一致性 |
| `MachineTrajectoryService` 的输入与覆盖校验 | 轨迹生成 | 保留；返回前追加结果一致性验证 |
| `NcProgramService` / `NcProgramBuilder` 的输入与内部不变量校验 | NC 生成 | 保留；返回前追加结果一致性验证 |
| `ProcessPresentationSnapshot::build()` 的计划结构校验 | Viewer 展示 | 保留（消费侧自检） |

验证模块不复制这些实现，只补齐跨对象关系与“结果 vs 输入”的一致性。

## 与 CamJob 的边界

`CamJob` 仍然只拥有 `GeometrySnapshot`、`DocumentProcessState`、`CamJobSettings` 与 `ProcessPlan`：

- `MachineTrajectory`、`NcProgram`、`SimulationTrajectory` 仍是按需生成的临时结果，验证只接收它们；
- 不新增 `ValidationResult` 之类的长期成员；
- 验证结果不进入持久化，也不作为后续判断的缓存。

## 关键业务规则

- 无效计划不进入 `CamJob`：发布边界先验证再写入。
- 验证只报告问题，不修复问题；调用方决定拒绝、重算还是提示用户。
- 验证不改变任何 revision。
- 验证不读取 CAD 图元几何，只使用几何快照中的身份、来源序号与类型。

## 相关源码

- `include/application/cam/CamValidation.h` / `src/application/cam/CamValidation.cpp`：验证入口与实现。
- `include/core/planning/ProcessPlan.h`：共享的结构与前置约束验证函数。
- `src/desktop/Gcode_postprocessing_system_ProcessUnitActions.cpp`：计划发布边界验证与写入。
- `src/desktop/Gcode_postprocessing_system_SortActions.cpp`：排序结果的发布边界。
- `src/desktop/Gcode_postprocessing_system_SimulationActions.cpp`：仿真准备前的计划验证。
- `src/application/machine/MachineTrajectoryService.cpp`：四轴轨迹结果验证。
- `src/application/nc/NcProgramService.cpp`：三轴与四轴 NC 结果验证。

## 对应需求与概要设计

概要设计：[领域模型](../architecture/domain-model.md)、[数据流](../architecture/data-flow.md)、[模块边界](../architecture/module-boundaries.md)、[状态与生命周期](../architecture/state-and-lifecycle.md)。
