# NC 与 G-code 详细设计

## 职责

NC 生产链将已确定的三轴计划或四轴轨迹转换为与文本方言分离的 `NcProgram`。
G-code 后处理器依据当前配置将 NC 语义格式化为完整文本。
文件写入只在程序构建和文本渲染成功后开始。

## 生产入口

- 桌面层 `exportGCode()` 负责模式检查、输出路径选择、覆盖确认和结果消息。
- `prepareDocumentForGCodeExport()` 只校验当前 CAM 任务：没有加工计划时提示先完成 CAM Planning，计划或几何快照失效时提示重新 Planning；不自动排序、不建立快照、不修改加工状态。
- `GGenerator::buildProgramText()` 根据三轴或四轴模式调用 `NcProgramService`，并完成几何快照与配置的参数组织。
- `GCodePostProcessor::render()` 将完整 `NcProgram` 转换为 CRLF 文本。
- `GCodeFileWriter::write()` 将已完成的文本一次写入目标文件。

## 输入与输出

三轴输入是几何快照、加工状态、`Planar3Axis ProcessPlan` 和后处理配置。
四轴输入额外包含由计划和运动配置生成的 `MachineTrajectory`、可选截面及实体元数据。
两种模式的 NC 输入都来自几何快照：`GeometrySnapshotNcInputBuilder` 由快照条目重建实体元数据与三轴实体输入，不访问 CAD 图元。
NC 阶段输出 `NcProgram`，保存模式、revision、实体块、运动和诊断注释。
文本阶段输出完整 `QString`；文件阶段输出 `.nc`、`.gcode` 或文本文件。

## 主要数据类型

- `NcProgram`：模式、revision、前置注释和有序实体块。
- `NcEntityBlock`：实体元数据、四轴加工单元索引、切削启停边界及该实体对应的 NC 运动。
- `NcMotion`：快速、直线或圆弧运动及轴字。
- `NcCuttingControl`：加工单元在首条切削前启用、在最后切削和过切后停用的明确 NC 语义；语义与模式无关，四轴程序必用，三轴程序默认不使用（None）。
- `NcEntityMetadata`：稳定 `EntityId`、顺序、连续组、类型、图层和颜色键。
- `GProfile`：文件代码、加工单元启停代码、图元类型、图层、颜色和四轴配置。
- `GCodePostProcessorProfile`：后处理器实际读取的文本规则和值格式。
- `GGenerator`：组织 NC 服务与后处理器的调用顺序和参数，只返回文本的应用层编排对象。
- `GCodeFileWriter`：把已完成的文本写入目标文件的写入器。

## 当前生产数据流

三轴：

```text
Planar3Axis ProcessPlan
+ GeometrySnapshot
+ DocumentProcessState
→ GeometrySnapshotNcInputBuilder
→ PlanarNcProgramBuilder
→ NcProgram
→ GCodePostProcessor
→ G-code 文本
→ GCodeFileWriter
→ 文件
```

四轴：

```text
Rotary4Axis ProcessPlan
+ GeometrySnapshot
+ DocumentProcessState
+ 四轴运动配置
+ 可选 TubeSectionModel
→ MachineTrajectoryService
→ MachineTrajectory
+ GeometrySnapshotNcInputBuilder（实体元数据）
→ NcProgramBuilder
→ NcProgram
→ GCodePostProcessor
→ G-code 文本
→ GCodeFileWriter
→ 文件
```

## 状态所有权

当前计划、加工状态和几何快照由 `CamJob` 提供，截面和活动配置由主窗口提供；三轴与四轴 NC 路径都使用几何快照。
`NcProgram`、`MachineTrajectory` 和最终文本均为单次导出过程中的临时值对象。
NC 语义继承计划确定的顺序、方向、起点和连续组，不回写加工上下文。
输出路径由桌面层和设置存储管理，核心 NC 与后处理器不持有文件对话框。

## 失效条件

- 计划缺失、模式错误或 revision 与几何快照及加工状态不一致时停止导出。
- 三轴要求几何快照、计划与加工状态 revision 一致。
- 四轴要求轨迹、元数据、计划、几何快照和加工状态 revision 一致。
- 排序配置变化影响计划及全部下游结果。
- 旋转轴、安全移动、初始位置、Z 修正、过切和 A 轴配置变化影响四轴轨迹、NC 和文本。
- 文件头尾、类型、图层、颜色规则和文本精度仅影响后处理文本。
- 当前实现没有独立配置 revision；活动配置变化由桌面层保守地清除当前计划。

## 关键业务规则

- NC 构建器按 `ProcessPlan` 顺序消费分配，不重新决定加工顺序、方向或起点。
- 三轴可输出直线和受支持平面上的精确圆弧；其他曲线路径按统一离散路径输出。
- 四轴 NC 仅接受已确定的 XYZ/A 快速和线性轨迹。
- 四轴 `NcProgramBuilder` 以 `EntityTrajectory::processUnitIndex` 固化加工单元边界。每个加工单元恰好在首个实体块标记一次 `Enable`，并在最后一个实体块的全部切削、连接和过切之后标记一次 `Disable`。
- 同一加工单元内的完整图元和路径片段保持切削开启；第二个及后续实体块不得包含快速运动。不同加工单元之间必须先关闭切削，再执行快速转移，最后开启下一单元。
- `GCodePostProcessor` 只把 `NcCuttingControl` 翻译为配置中的加工单元开始和结束代码。三轴与四轴共用同一切割控制排序器：程序不含 Enable/Disable 块时排序器保持未激活，三轴默认输出与历史行为一致；程序启用单元级控制后自动生效。快速运动要求切削状态为关闭，切削、连续切削连接和过切要求切削状态为开启。
- 图层、颜色和图元类型规则仍可输出非启停类工艺代码，但不得包含 `M03/M3/M05/M5`；这些规则的变化不会形成额外加工单元边界。
- 缺少 `processUnitCode` 的旧配置加载时默认迁移为 `M03/M05`，并从图元类型、图层和颜色规则中移除旧启停命令；混合命令行保留非启停内容并输出迁移警告。
- 四轴固定平面实体在轨迹阶段已经按整条路径确定唯一表面和恒定原始 A 角；NC 构建器只消费验证通过的姿态，不按单点坐标重新判断表面。
- 连续加工组在进入 NC 前同时通过源空间连接和机床空间端点连续性检查，表面误判造成的 XYZ/A 跳变会阻止文件生成。
- 普通四轴导出不要求方管截面；缺失截面时轨迹服务使用普通四轴中心回退规则。
- 实体快速移动先输出，随后才应用该实体的工艺代码块。
- 工艺头应用顺序为图层、颜色、图元类型。
- 工艺尾应用顺序为图元类型、颜色、图层。
- 文件头位于全部实体之前，文件尾位于全部实体之后。
- 后处理器不再通过最终文本猜测或消除相邻 `M05/M03`；启停数量和位置由 NC 加工单元边界决定。
- 后处理器移除空代码行，并统一输出 CRLF，文件末尾保留一个 CRLF。
- G-code 文本在打开目标文件前完整构建；业务校验或渲染失败不会开始覆盖文件。
- 文件写入使用截断模式并检查实际字节数；底层写入失败会返回明确错误。

## 相关源码

- `src/desktop/Gcode_postprocessing_system_GCodeActions.cpp`：组织导出前计划检查、路径选择和用户消息。
- `src/application/export/GGenerator.cpp`：组织三轴或四轴 NC 构建与文本渲染。
- `src/application/export/GCodeFileWriter.cpp`：写入并校验已完成的 G-code 文本。
- `src/application/nc/GeometrySnapshotNcInputBuilder.cpp`：由几何快照与加工计划生成 NC 输入和实体元数据。
- `src/application/nc/NcProgramService.cpp`：校验 revision 并组织两种模式的 NC 生产链。
- `src/core/nc/PlanarNcProgramBuilder.cpp`：从三轴计划和几何生成 NC 实体运动。
- `src/core/nc/NcProgramBuilder.cpp`：将四轴机床轨迹映射为 NC 语义。
- `src/infrastructure/nc/GCodePostProcessor.cpp`：应用配置规则并格式化最终 G-code 文本。
- `src/infrastructure/config/GProfile.cpp`：读取、保存和提供文件、类型、图层、颜色及四轴配置。

## 当前实现差异

| 事项 | 当前生产实现 | 需求或概要设计要求 | 影响 |
|---|---|---|---|
| 配置失效层级 | 任意活动配置切换通常清除整个计划 | 排序、运动和纯文本配置应分别使对应层及下游失效 | 纯文本调整会触发不必要的重新排序 |
| 配置一致性 | 计划、轨迹和 NC 没有记录配置 revision | 导出应保证配置与各派生结果一致 | 当前依赖单线程同步构建和上层清计划，缺少显式版本校验 |
| 自动补计划 | 导出缺少有效计划时调用当前“智能排序”入口 | 智能排序应忽略当前加工单元序列和人工方向，并直接替换当前序列 | 已实现：自动补计划以 `RebuildSequence` 采用自动方向和起点并替换唯一序列；懒旋转时人工方向与起点仅作为入口硬约束校验，用户状态不删除 |
| 文件原子性 | 文本先完整构建，但目标文件以截断方式直接写入 | 失败时应阻止部分文件写出 | 业务失败不会改文件；打开后发生底层短写时仍可能留下不完整文件 |

## 对应需求与概要设计

需求：[GCODE-001～GCODE-025](../requirements/gcode-and-export.md)、[PROCESS-004～PROCESS-010、PROCESS-015～PROCESS-017](../requirements/machining-process.md)。

概要设计：[数据流](../architecture/data-flow.md)、[模块边界](../architecture/module-boundaries.md)、[状态与生命周期](../architecture/state-and-lifecycle.md)。
