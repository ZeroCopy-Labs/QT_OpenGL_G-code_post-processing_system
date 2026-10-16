# 数据流

## 1. 文件导入

```text
DXF / DWG 文件
→ 外部格式解析与适配
→ CAD 文档和原始图元
→ 显示几何
→ Viewer 展示
```

导入边界负责把外部文件转换为可编辑文档。文档建立后，Viewer 从 CAD 层取得显示数据；加工侧在需要规划时捕获独立的几何快照。

## 2. 几何转换

```text
CAD 图元
→ 精确源几何
→ 统一加工路径
→ 拓扑和加工分析
```

外部格式对象和 Viewer 显示数据停留在各自边界。拓扑、加工规划和轨迹生成使用统一加工路径，使直线、圆弧、圆、椭圆、多段线和样条进入一致的生产数据流。

## 3. CAD → CAM 几何边界

```text
CadDocument
→ DocumentGeometrySnapshotBuilder::capture
→ GeometrySourceSnapshot
→ GeometrySnapshotCompiler::compile（按图元类型选择采样策略）
→ GeometrySnapshot（CamJob 持有）
→ DocumentProcessPlanningAdapter
→ ProcessPlanningService
→ ProcessPlan
```

几何快照只描述 CAD 几何：图元身份、图层、颜色、可见性、原始解析几何、编译后的路径和实际使用的采样策略。它不携带加工状态、加工计划、机床轨迹或 NC 语义，也不包含 `CadDocument*`、`CadItem*` 或 `DRW_Entity*`。

快照只在需要规划时按需建立：CAD 内容 revision 变化会使快照失效，但不会触发重新编译；加工状态 revision 变化只使加工计划失效，快照继续有效。

几何快照之后 CAM 不再访问 `CadDocument`：

```text
GeometrySnapshot
→ GeometryDuplicateAnalyzer（重复几何分析）
→ ProcessPlanningInput
→ ProcessPlanningService
→ ProcessPlan
→ MachineTrajectoryService
→ MachineTrajectory
```

排序只计算加工计划：发现了重复几何时，保留第一个图元参与加工，其余在计划中按 `DuplicateGeometry` 排除；`CadDocument` 中的重复图元仍然存在，CAD 内容 revision 与撤销历史都不变化。CAD 去重仍是用户主动执行的 CAD 清理操作。

轨迹生成只使用几何快照、加工计划和加工状态：`snapshot.contentRevision`、`plan.contentRevision` 与 `plan.processStateRevision` 必须互相对应，否则返回 `Conflict`，不自动重新规划。

## 4. 加工单元形成

```text
可加工图元
→ 连续关系分析
→ ProcessUnit 集合
→ ProcessUnitSequence
→ ProcessPlan
```

开放或闭合的连续加工组整体形成一个加工单元，不连续的单个图元形成独立加工单元。普通排序、智能排序和人工编排使用同一加工单元集合，不在排序阶段拆分连续单元。

普通排序尽量保留当前加工单元序列、人工方向和人工起点，并将新增或变化的单元补入序列。智能排序忽略当前序列，重新计算全部加工单元顺序，并以结果直接替换当前序列。

## 5. 三轴流程

```text
CAD 几何
+ 当前加工上下文
+ 当前加工单元序列
+ 三轴排序策略
→ 三轴加工计划
→ NC 语义
→ G-code 文本
→ 输出文件
```

两种排序都以加工单元为最小单位并输出统一加工计划。后续 NC 生成仅消费当前有效计划和配置。

## 6. 普通四轴流程

```text
CAD 几何
+ 当前加工上下文
+ 当前加工单元序列
+ 旋转轴中心和运动配置
+ 四轴排序策略
→ 四轴加工计划
→ 普通四轴机床轨迹
→ NC 语义
→ G-code 文本
→ 输出文件
```

普通四轴仅要求有效加工路径、旋转轴中心、XYZ/A 运动参数、当前有效加工顺序和配置，对方管截面尺寸、圆角和截面中心无前置依赖。

## 7. 截面增强四轴流程

```text
普通四轴输入
+ 有效方管截面上下文
→ 表面归属和截面相关加工分析
→ 方管实体内部路径判断
→ 加工断面 / 废面工艺约束
→ 增强四轴加工计划和机床轨迹
→ NC 语义
→ G-code 文本
```

截面增强流程用于方管表面归属、圆角刀头方向、方管实体内部路径、加工断面、废面区间、真实工件碰撞边界和增强安全位置。有效截面上下文的字段见[领域模型](./domain-model.md)，来源见[状态与生命周期](./state-and-lifecycle.md)。

没有有效截面尺寸和圆角时，系统跳过截面增强阶段，普通四轴排序、轨迹和导出流程继续使用其自身输入。

## 8. 内部线流程

```text
统一加工路径
+ 有效方管截面
→ 按最大圆角半径加额外内缩建立 YZ 内缩窗口
→ 完整路径窗口内判断
→ 物理删除窗口内图元（进入统一撤销栈）
```

内部线自动清理依赖有效截面尺寸、圆角和中心。只有整条路径完全位于内缩窗口内部时才删除对应图元；接触或穿越窗口边界、安全带附近的路径继续保留。无有效截面或窗口坍缩时跳过清理并提示用户。

自动清理直接删除 CAD 图元，一次清理作为单个撤销单元进入统一撤销栈，撤销后完整恢复被删除图元；清理使旧加工计划及其下游结果失效。

## 9. 用户输入、自动结果与派生结果

```text
用户业务输入 + 自动分析状态
→ 当前加工上下文

自动识别或自动排序
→ 候选或计划结果

三轴：CAD 几何 + 当前加工上下文
→ 加工计划
→ NC 程序
→ G-code 文本

四轴：CAD 几何 + 当前加工上下文
→ 加工计划
→ 机床轨迹
→ NC 程序
→ G-code 文本
```

用户确认的截面、加工断面和废面优先于对应的自动分析结果；内部线自动清理直接删除窗口内图元。普通排序尽量保留当前加工单元序列、人工方向和人工起点；智能排序直接替换当前加工单元序列。加工计划、轨迹和 NC 均是派生结果；需要调整最终输出时，应修改上游业务输入并重新生成。

## 相关需求

MACH-041、MACH-042、MACH-043、PROCESS-018、PROCESS-019、PROCESS-020、PROCESS-021、PROCESS-022、PROCESS-023。
