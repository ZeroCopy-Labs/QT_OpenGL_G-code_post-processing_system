# 模块边界

## 1. 职责划分

| 区域 | 负责 | 不负责 |
| --- | --- | --- |
| `desktop` / `ui` | 用户命令、窗口、面板、对话框、状态展示和消息 | 加工单元身份、顺序事实、几何和 NC 决策 |
| `cad` | 文档、图元、编辑、图层、选择、Viewer、稳定图元身份和通用 Undo/Redo 历史顺序 | 加工单元序列、遍历覆盖、加工计划、机床轨迹和 NC 语义 |
| `application` | 业务编排、文档数据捕获、用户加工状态、加工单元序列编辑、加工状态命令构造、状态一致性和结果分发 | CAD 历史栈管理、连续关系算法和外部文本方言 |
| `core/geometry` | 精确源几何、统一加工路径和几何不变量 | UI、文件选择和外部格式读写 |
| `core/topology` | 路径连通、严格闭环和拓扑关系 | 加工单元序列所有权、方管工艺和 G-code |
| `core/machining` | 方管截面、内部路径和加工断面等工艺几何 | 加工顺序和文本输出 |
| `core/planning` | 根据连续关系形成加工单元，并生成顺序、方向、起点、排除项和工艺屏障 | 当前加工单元序列持久状态和机床运动文本 |
| `core/machine` | XYZ/A 运动、安全移动、连续连接和过切 | G-code 方言和文件写入 |
| `core/nc` | 与文本格式分离的 NC 语义 | 配置管理和文件读写 |
| `infrastructure` | DXF/DWG、配置、图像、G-code 文本和外部文件边界 | 加工顺序、工艺判断和轨迹决策 |
| `compatibility` | 旧对象与当前业务边界之间的转换 | 新业务规则和核心事实所有权 |

## 2. 依赖方向

主要业务依赖方向为：

```text
UI / CAD ──→ Application ──→ Core
                 ↕
          Infrastructure
                 ↕
              外部格式
```

Application 组织 Infrastructure 的边界适配。文件解析、配置读取和文本输出通过 Infrastructure 进入或离开生产数据流，Core 不依赖 Infrastructure。

Compatibility 仅位于遗留对象转换边界。新业务事实进入 Core 后，不再由 Compatibility 决定其含义。

加工状态编辑由 Application 保存修改前后值并构造执行、撤销回调，随后提交给 CAD 层的通用历史入口。
CAD 编辑和加工状态编辑共用一个 Undo/Redo 栈，因此按用户操作发生顺序统一撤销和重做；CAD 层不定义加工单元排序或遍历专用命令。
删除、批量删除和替换命令本身只维护 CAD 文档：`CadEditer` 通过业务无关的编辑伴随工厂，把本次编辑涉及和新增的图元身份交给 Application。
Application 的 `ProcessStateEditSync` 在同一历史项内完成加工状态清理和加工单元序列恢复，`CadEditer` 不知道 `DocumentProcessState` 的存在。
`CadViewer`、CAD 渲染器和 `GGenerator` 仍直接依赖 `DocumentProcessState` 与 `ProcessPresentationSnapshot`，这部分留待后续渲染层和导出阶段迁移。

## 3. CAM 任务边界

`CamJob`（`application/cam/CamJob.h`）是当前一次 CAM 加工任务的应用层聚合边界，拥有四类事实：

```text
CamJob
├── GeometrySnapshot       CAM 看到的 CAD 几何（CAD → CAM 数据边界）
├── DocumentProcessState   加工输入状态
├── CamJobSettings         加工设置（壁厚覆盖、断面正/负向修正）
└── ProcessPlan            当前有效的加工计划（派生结果）
```

它只提供设置计划、清除计划、判断几何快照与计划是否存在和是否仍然有效等生命周期操作，不负责 CAD 编辑、几何计算、快照构建、排序算法、轨迹计算、G-code 生成、视图绘制和配置持久化，也不拥有 `CadDocument`、`CadEditer`、`CadViewer`、`ProcessPresentationSnapshot`、`RotaryTubeSectionModel` 或 `GProfile`。

几何输入链固定为：

```text
CadDocument
      │ DocumentGeometrySnapshotBuilder::capture
      ▼
GeometrySourceSnapshot
      │ GeometrySnapshotCompiler::compile（按图元类型选择采样策略）
      ▼
GeometrySnapshot          ← CamJob 持有
      │
      ▼
DocumentProcessPlanningAdapter → ProcessPlanningService → ProcessPlan
```

`DocumentProcessPlanningAdapter` 与 `ProcessPlanningService` 不再接收 `CadDocument`，也不再从 CAD 图元重新编译几何；四轴拓扑输入由 `GeometrySnapshotTopologyAdapter` 从同一快照转换。快照构建由应用层编排（桌面层 `ensureCurrentGeometrySnapshot()`）在需要规划时按需触发，不建立全局快照管理器。

CAM 排序不修改 CAD：`sortEntitiesWithProcessPlan2D()` / `sortEntitiesWithProcessPlan3D()` 只消费几何快照，重复几何由 `GeometryDuplicateAnalyzer` 识别并在加工计划中按 `ProcessExclusionReason::DuplicateGeometry` 排除，不调用 `CadEditer` 删除图元，也不推进文档内容 revision。CAD 工作区的显式“去重”和导入后自动去重仍是明确的 CAD 清理操作。

`MachineTrajectoryService::buildRotaryTrajectory()` 同样只接收 `GeometrySnapshot`、加工状态和加工计划，不接收 `CadDocument`，也不自行捕获或编译 CAD 几何；轨迹执行继续按图元类型使用轨迹采样策略（从快照条目内的源几何重新采样）。

仍然直接接收 `CadDocument` 的消费者是 `NcProgramService`（三轴 NC 路径与兼容层元数据捕获）、`GGenerator`（文档与配置持有者）与 compatibility 层的旧适配器，它们属于后续导出与兼容阶段。

`ProcessPresentationSnapshot` 仍由桌面层持有并交给 Viewer，`RotaryTubeSectionModel` 仍由桌面层持有，本阶段未迁移。

## 4. Core 边界

Core 的输入和输出使用独立值对象。Core 的依赖边界不包含：

- UI 或桌面窗口；
- CAD Viewer 和 CadItem；
- Compatibility；
- Qt Widgets；
- DRW 实体和外部文件对象。

CAD 与外部格式数据应在进入 Core 前完成捕获和适配。Core 计算结果由 Application 层校验版本后交给 CAD 或 UI 展示。

## 5. 显示与业务边界

Viewer 使用 CAD 文档和当前加工展示快照完成渲染、拾取、标签和方向箭头显示。图元选择通过稳定图元身份映射到加工单元选择，一个加工单元只显示一个顺序编号。加工单元身份和序列由 Core 与 Application 提供，Viewer 不自行生成或保存这些业务事实。GPU 缓存键和临时选择状态仅在 Viewer 生命周期内使用。

## 6. 外部格式边界

DXF/DWG 输入、配置文件和 G-code 文本属于外部表示。Infrastructure 负责在外部表示与内部业务对象之间转换：

```text
外部格式 ↔ Infrastructure ↔ Application → Core 业务对象
```

格式规则不进入几何、拓扑、规划或机床运动模块。加工决策也不由文件读写代码推断。

## 相关需求

MACH-042、MACH-043、PROCESS-018、PROCESS-020、PROCESS-022、EDIT-029、EDIT-033。
