# NX10 几何信息悬浮窗 设计规格（v1）

> 状态：待用户评审（brainstorming 阶段 6/7）
> 日期：2026-11-19
> 目标版本：Siemens NX 10.0（主）/ NX 2512（同一协议复用，不阻塞 v1）
> 决策来源：2026-11-19 会话问答，见 §2

---

## 1. 背景与已证事实

### 1.1 问题

在 NX10 里做模具设计时，看一个体/面/边的几何信息要走 `信息 → 对象` 或测量命令，打断操作流。
目标是一个**悬浮信息窗**：鼠标选中或悬浮到对象上，就地显示 tag、颜色、长度、面积、类型、包围盒、距离。

### 1.2 已实测的事实（本机 NX 10.0 / 2512 安装目录）

| # | 事实 | 证据 |
|---|---|---|
| F1 | NX10 **没有**公开的 hover/prehighlight 回调 | 全量扫 `UGOPEN\*.h`、`UGOPEN\NXOpen\*.hxx`、`UGII\managed\NXOpen.xml`(31MB)，`PreHighlight/hover/UnderCursor/rollover callback` 0 命中 |
| F2 | 只有 rollover 的**偏好开关**，无事件 | `Display::SelPref`：`HighlightSelectionOnRolloverToggle`、`TooltipOnRolloverToggle`、`RolloverDelay` |
| F3 | 存在的选择面是「点击驱动」 | `UI::SelectionManager()`→`Selection`；`SetTaggedObjectSelectionCallbacks`（选择动作回调）；`AskSelectionTaggedObjectList`；`UF_UI_ask_global_sel_object_list` |
| F4 | NX10 **没有** `UF_MODL_ask_face_props` 的「面积」语义 | 该函数返回某 (u,v) 处的点/一阶二阶导/法向/主曲率，`uf_modl.h:2550` |
| F5 | NX10 **没有** `UF_MODL_ask_edge_length` / `ask_curve_length` | `uf_modl.h` 只有 `ask_edge_type/body/faces/verts/feats/smoothness/tolerance` |
| F6 | 面积/周长/长度/体积/距离的**正确接口是 NXOpen Measure** | `MeasureManager::NewFaceProperties`→`MeasureFaces::Area()/Perimeter()`；`NewLength(units, objects)`→`SingleMeasure::Value()`；`NewMassProperties`；`NewDistance` |
| F7 | NX10 可拿到的 O(1) 显示/几何信息 | `UF_OBJ_ask_type_and_subtype`、`UF_OBJ_ask_display_properties`、`UF_MODL_ask_bounding_box` |
| F8 | 自建拾取所需的数学/网格接口齐全 | `UF_VIEW_ask_view_matrix`（视图矩阵）、`UF_FACET_facet_solid`/`ask_vertices_of_facet`/`ask_solid_face_of_facet` |
| F9 | NX 是单线程 UI 模型 | 跨线程只能 `UF_UI_lock_ug_access`，而它本身锁 UI（"Inhibit access to DA1 and appropriate menu bar items"），**不能并行加速** |
| F10 | Block UI Styler 不能做浮窗 | 只能停 Dialog Area 2，无法浮在图形区跟随鼠标 |

> F1 与用户记忆（"C 的 NXOpen 里用过悬停回调"）冲突。本规格把捕获层做成**可插拔接口**（§6.1），
> 用户提供的接口一旦给出签名即可作为第三个实现接入，不阻塞其余四层。

### 1.3 未证实、必须在 M0 消解

| # | 待验证 | 影响 | 默认动作 |
|---|---|---|---|
| U1 | 用户所指的「悬停回调」的确切类名与签名 | 决定 v1 是选择驱动还是真悬停 | 按 F3 先做选择驱动 |
| U2 | ~~NX10 是否有 NX Open C++ 运行许可、本机是否有 VS2010 + NXOpen C++ 头~~ **已消解**：本机无 VS2010，用户用 **v142** 编译通过；运行期 ABI 风险由 M0 的最小导出探针先验 | — | 已并入 D8 |
| U3 | 未签名 .NET 是否触发 `dotnet_author` | 不影响本方案（浮窗进程不加载 NXOpen） | — |

---

## 2. 决策记录

| # | 决策 | 结论 | 否决的选项 |
|---|---|---|---|
| D1 | 捕获层形态 | 抽象为 `ICaptureSource`；v1 实现 `SelectionSource`，`HoverSource` 预留 | 一开始就写死某一种捕获 |
| D2 | 宿主 | **进程内 C++ 薄插件**（捕获+查询）+ **进程外 WPF 浮窗**（渲染）+ 共享内存 IPC | 纯进程内 C#（许可+崩溃域）、纯 Python（做不了高频查询与 UI） |
| D3 | 距离语义 | 默认「到上一次选中的对象」，可配置切「到 WCS 原点」 | 只做一种 |
| D4 | 查询分级 | L0 骨架 → L1 几何 → L2 重查询，异步补、旧结果丢弃 | 一次性查全再显示 |
| D5 | 是否改 NX 显示 | 只读，绝不改高亮/颜色，避免与 NX 自身 prehighlight 打架 | 自己高亮命中对象 |
| D6 | 许可边界 | 浮窗进程不加载任何 NXOpen 程序集，独立进程独立许可 | 进程内 WPF |
| D7 | 版本策略 | 协议（共享内存结构体 + 字段表）版本化；NX10 与 2512 复用同一浮窗 | 每版本各写一套 |
| D8 | 编译工具链 | **MSVC v142（VS2019）**，C++14；用户已在 NX10 上跑通编译。若运行期发生 STL/ABI 崩溃，退回「只用 UFUN C API」 | VS2010 v100（本机无 `VC\bin\cl.exe`）、C#/.NET |
| D9 | 浮窗技术 | **Win32 分层窗口 + GDI+**（`UpdateLayeredWindow` + 32bpp DIB），进程外 | Direct2D（分层窗上兼容性差）、WPF（多一套运行时） |

---

## 3. 目标与非目标

### 3.1 目标（v1）

1. 选中体/面/边，**≤100ms 内**浮窗出现骨架信息（类型、tag、颜色）。
2. 面积、长度、包围盒**≤300ms** 补齐；体积、距离异步补齐，不阻塞已显示字段。
3. 运行期 NX 图形区**旋转/缩放不掉帧**（60fps 下不出现 >50ms 卡顿）。
4. 浮窗崩溃不影响 NX；NX 崩溃浮窗自动退出。
5. 单件（≤5 万面）模具零件上稳定可用，连续操作 1 小时无泄漏。

### 3.2 非目标（v1 不做）

- 装配树/组件级信息（只做工作部件的体/面/边/曲线）。
- 历史/特征依赖链。
- 距离的「点到点」「曲面间最小间隙」等全部模式（v1 只做对象到对象最小距离）。
- 多对象聚合统计（面积和、体总数）。
- NX12 以下以外版本的适配。

---

## 4. 总体架构

```
┌────────────────────────── NX 主线程 ──────────────────────────┐
│  ICaptureSource（可插拔，§6.1）                                │
│    ├ SelectionSource   官方：选择回调 / 全局选择列表轮询（v1）    │
│    ├ HoverSourceA      用户提供的 C 接口（待签名，U1）           │
│    └ HoverSourceB      自建拾取：鼠标钩子+视图矩阵+UF_FACET BVH  │
│         │ 产出 ObjRef{tag, kind, view, cursor, gen}            │
│         ▼                                                      │
│  QueryEngine：L0/L1 同步查、L2 投递异步 job                     │
│         │ 写 InfoSnapshot（定长 POD）                          │
│         ▼                                                      │
│  SnapshotWriter：seqlock 双槽 + SetEvent(命名事件)              │
└───────────────────────────────┬────────────────────────────────┘
                                │ 共享内存（CreateFileMapping）
┌───────────────────────────────▼────────────────────────────────┐
│  浮窗进程（C# WPF，独立 STA，不加载 NXOpen）                     │
│    SnapshotReader（seqlock 读，无锁）                           │
│    InfoViewModel（字段级 dirty）                                │
│    OverlayWindow（无边框/Topmost/owner=NX主窗/可点穿/跟随光标）   │
└─────────────────────────────────────────────────────────────────┘
```

**边界原则**：NX 侧不知道浮窗存在，浮窗侧不知道 NX 存在；两侧只共享一个带版本号的 POD 结构和一个命名事件。

---

## 5. 数据契约

### 5.1 `ObjRef`（NX 侧内部）

```cpp
struct ObjRef {
  uint64_t gen;        // 每次捕获 +1，用于丢弃过期结果
  uint32_t tag;        // NX tag_t
  uint8_t  kind;       // 1=body 2=face 3=edge 4=curve 5=other
  uint8_t  _pad[3];
  char     view[16];   // 视图标识，用于后续拾取/高亮校验
  double   cursor[3];  // 光标绝对坐标（选择回调可拿）
};
```

### 5.2 `InfoSnapshot`（跨进程 POD）

结构体本身约 264B；共享内存每槽固定 512B，余量留给后续字段，`version` 不匹配时拒绝读。

```cpp
#define NXGEO_MAGIC   0x4E584745u   // "NXGE"
#define NXGEO_VERSION 1

struct InfoSnapshot {
  uint32_t magic;            // NXGEO_MAGIC
  uint16_t version;          // NXGEO_VERSION，不匹配则浮窗显式报错
  uint16_t kind;             // ObjKind
  uint64_t gen;
  uint32_t tag_hex;          // 以 tag 值展示
  uint32_t fields_mask;      // 见 §5.3
  char     type_name[32];    // "实心体 Bhd" / "圆柱面 CylFace" ...
  int32_t  color;            // NXColor 索引
  int32_t  layer;
  double   bbox_min[3], bbox_max[3];
  double   length;           // 边/曲线
  double   area;             // 面
  double   perimeter;        // 面周长
  double   volume;           // 体
  double   distance;         // D3
  char     distance_to[48];  // 距离对象名
  char     note[64];         // "L2 查询中超时" / "面未找到" 等
};
```

### 5.3 字段分级与 `fields_mask`

| 级别 | 字段 | 时机 | 预算 |
|---|---|---|---|
| L0 | kind、tag、type_name、color、layer | 捕获时同步 | ≤0.3ms |
| L1 | bbox、area/perimeter、length | 同步查询 | ≤2ms |
| L2 | volume、distance | 异步 job | ≤500ms，过期丢弃 |

bit0=L0、bit1=bbox、bit2=area、bit3=length、bit4=volume、bit5=distance。
浮窗只渲染已置位的字段——这就是「查到什么显示什么」的落点。

### 5.4 查询 API 映射（已核实存在）

| 字段 | API | 备注 |
|---|---|---|
| 类型 | `UF_OBJ_ask_type_and_subtype` | 需内置 type/subtype→中文名映射表 |
| 颜色/图层 | `UF_OBJ_ask_display_properties` | 返回 `UF_OBJ_disp_props_t`，含 color/layer |
| 包围盒（体） | `UF_MODL_ask_bounding_box` | 面/边需从采样点派生 |
| 面积/周长（面） | `MeasureManager::NewFaceProperties` → `MeasureFaces::Area()/Perimeter()` | 需 `ScCollector` |
| 长度（边/曲线） | `MeasureManager::NewLength(units, objects)` → `SingleMeasure::Value()` | 同上 |
| 体积（体） | `MeasureManager::NewMassProperties` → `MeasureBodies` | L2 |
| 距离 | `MeasureManager::NewDistance(a, b)` | L2；备选 `UF_MODL_ask_minimum_dist_2` |

> Measure 系列若走 `CreateFeature` 会落特征树；v1 只用瞬时值（`Value()`/`Area()`），不建特征。

---

## 6. 组件规格

### 6.1 `ICaptureSource`

```cpp
class ICaptureSource {
public:
  virtual ~ICaptureSource() {}
  virtual bool Start() = 0;                 // 注册回调或启动轮询
  virtual void Stop() = 0;
  virtual void SetSink(std::function<void(const ObjRef&)>) = 0;
};
```

- `SelectionSource`：轮询 `UF_UI_ask_global_sel_object_list`（50ms），diff 出新增/变更对象；选中空则发空 ref 隐藏浮窗。
- `HoverSourceA/B`：M0 结论后实现，接口不变。

**约束**：Sink 必须**非阻塞**，只做入队；禁止在回调里查几何。

### 6.2 `QueryEngine`

- 单线程（NX 主线程），L0/L1 同步、L2 投递到有序 job 队列。
- 缓存：`(tag, part_stamp)` → 字段值。`part_stamp` 取部件修改计数/时间戳，模型一变整表失效。
- **latest-wins**：拿到结果时比对 `gen`，不是最新的直接丢弃（不做取消）。

### 6.3 `SnapshotWriter / Reader`（seqlock）

- 共享内存：`Local\NXGEO_<nx_pid>`，16B 头 + 两个 512B 槽。
- 头里除 `active_gen` 外还放 `nx_hwnd`（NX 主窗口句柄，插件启动时写入）与 `nx_pid`，
  浮窗靠它设 Owner、clamp 定位、判断前台，不需要自己去找 NX 进程。
- 写：`slot = gen & 1`；先写槽内容，`MemoryBarrier()`，再写 `active_gen`。
- 读：读 `active_gen` → 拷槽 → 重读 `active_gen`，不一致重试（最多 3 次，失败用上次快照）。
- 唤醒：命名事件 `Local\NXGEO_EVT_<nx_pid>`；浮窗另设 16ms 兜底轮询。

### 6.4 `OverlayWindow`（WPF）

| 项 | 规格 |
|---|---|
| 样式 | `WindowStyle=None`、`AllowsTransparency`、`ShowInTaskbar=false`、`Topmost=true` |
| 扩展样式 | `WS_EX_NOACTIVATE`、`WS_EX_TOOLWINDOW`、`WS_EX_TRANSPARENT`（点穿） |
| Owner | `WindowInteropHelper(hwnd).Owner = nxMainHwnd`，跟随 NX 最小化/焦点 |
| 定位 | `GetCursorPos` + 偏移(16,20)，clamp 到 NX 窗口矩形；光标离开 NX 或 NX 失焦即隐藏 |
| 交互模式 | 默认点穿；`Alt+\`` 热键切「可交互」（可拖、可固定、可复制） |
| 渲染 | 字段级 `DirtyMask`，只更新变化行；文本用固定宽度栏位，避免整窗 re-layout |
| 生命周期 | 由插件用 `CreateProcess` 拉起，加入 Job Object（`KILL_ON_JOB_CLOSE`），NX 退出浮窗即死 |

**DPI**：浮窗进程声明 Per-Monitor DPI Aware V2，坐标全部用物理像素换算。

### 6.5 部署与加载

| 项 | 做法 |
|---|---|
| 插件 | `nxgeo.dll`（NXOpen C++），放 `%UGII_USER_DIR%\startup\` 由 NX 启动时自动加载；或往 `custom_dirs.dat` 追加目录 |
| 开关 | 工具栏按钮 + 热键 `Ctrl+Alt+G` 全局开关浮窗 |
| 浮窗 | `nxgeo-overlay.exe`（自包含 WPF），插件按需 `CreateProcess` 启动，加入 Job Object |
| 退出 | 关 NX → Job Object 回收浮窗进程；浮窗单独关闭 → 插件下次事件再拉起 |
| 卸载 | 删两个文件即可（v1 不做安装器） |
| 双版本 | 2512 复用同一份 exe；插件按 `UGII_BASE_DIR` 重编一次即可 |

## 7. 数据流（时序）

```
用户点选面
  → SelectionSource 轮询发现新 tag
  → ObjRef{gen=N}
  → QueryEngine: L0(type/color) 同步 → 写快照(gen=N, mask=bit0) → SetEvent
  → 浮窗渲染骨架（~30ms 可见）
  → QueryEngine: L1(bbox/area/perimeter) 同步 → 写快照(mask|=bit2|bit1) → 更新
  → QueryEngine: L2(volume/distance) 入队 → 完成时若 gen==N 才写入，否则丢弃
  → 用户改选另一个对象
  → gen=N+1，所有在途 N 的结果被丢弃
```

---

## 8. 性能预算（验收口径）

| 环节 | 目标 | 测量方法 |
|---|---|---|
| 捕获到快照写入 | ≤ 0.3 ms | 插件内 `QueryPerformanceCounter` 打点，写日志 |
| L1 查询（面/边） | ≤ 2 ms | 同上 |
| L2 查询 | ≤ 500 ms | job 日志 |
| 端到端可见 | ≤ 100 ms | 屏幕录制抽帧 |
| 旋转/缩放掉帧 | 无 >50ms 卡顿 | NX 内置性能 + 目测 1 小时 |
| 内存 | ≤ 20 MB（浮窗）/ ≤ 5 MB（插件） | 任务管理器 |

---

## 9. 错误处理与降级

| 场景 | 行为 |
|---|---|
| `version` 不匹配 | 浮窗显示「协议版本不符，请重启 NX」，不发数据请求 |
| 共享内存创建失败 | 插件退回写临时文件中转（低频，10Hz），浮窗显示「降级模式」 |
| L2 超时/异常 | 该字段留空，`note` 显示「体积查询失败」，不影响其余字段 |
| 浮窗进程不在 | 插件静默重试 3 次后放弃，不影响 NX |
| 选中对象已删除 | 发空 ref，浮窗隐藏 |
| 部件切换 | 清缓存 + 发空 ref |

---

## 10. 测试与验收

| 层 | 内容 | 需要 NX |
|---|---|---|
| 离线单测 | seqlock 读写并发、`fields_mask` 合并、latest-wins 丢帧、版本校验、字段格式化 | 否 |
| 真机冒烟 | 体/面/边各 3 个 → 8 字段全出；框选多对象取首个；空选取隐藏 | 是 |
| 性能 | 60Hz 连续切换对象 10 分钟：无泄漏、无 >50ms 卡顿 | 是 |
| 异常 | 删对象、切部件、关浮窗再开、NX 强杀 | 是 |

**v1 完成定义**：冒烟全过 + 性能达标 + 1 小时 soak 无泄漏。

---

## 11. 里程碑

| 阶段 | 内容 | 验收 | 预估 |
|---|---|---|---|
| M0 | 捕获层验证：U1（用户 API）+ U2（许可/工具链）+ `SelectionSource` 打通 | 控制台打出选中对象 tag | 0.5 天 |
| M1 | 插件：QueryEngine + 字段映射 + 缓存 | 单对象 8 字段全出，L1 ≤2ms | 2 天 |
| M2 | IPC + 浮窗：seqlock、WPF 窗口、跟随、增量 | 端到端 ≤100ms | 2 天 |
| M3 | 打磨：距离、热键、点穿/交互、配置、日志 | 模具实际件上连续用一天 | 1 天 |
| M4 | 验收：§10 全跑 | 完成定义达成 | 0.5 天 |

合计约 6 个工作日（不含 U1 不确定项）。

---

## 12. 风险与对策

| 风险 | 概率 | 对策 |
|---|---|---|
| U1 的 hover 接口不存在或不稳定 | 中 | 捕获层接口化，选择驱动版先交付；自建拾取作为独立子项评估 |
| NX10 C++ 工具链（VS2010）缺失 | 中 | M0 先验；不行退「纯 Python 插件 + 文件队列」降级版（延迟 ~250ms） |
| `UF_FACET` 自建拾取与 NX 显示不完全一致 | 高 | 只在无官方 hover 时启用，容差可配置，接受「偶尔偏一格」 |
| Measure 查询在复杂面上偏慢（>2ms） | 中 | 降级到 L2 异步，浮窗先显示骨架 |
| 共享内存结构体对齐/版本漂移 | 低 | 固定宽度类型 + `#pragma pack(1)` + `version` 字段 + 单测 |
| 浮窗遮挡图形区影响操作 | 中 | 点穿 + 热键切换 + 透明度可调 |

---

## 13. 待确认（默认值已给，无异议按默认执行）

| # | 问题 | 默认 |
|---|---|---|
| Q1 | 用户所指 hover C 接口的类名/签名 | 无 → 用 `SelectionSource` |
| Q2 | 距离语义 | 到上一次选中对象 |
| Q3 | 浮窗主题/字号 | 深色半透明 #CC1E1E1E，12pt 等宽 |
| Q4 | 是否需要「固定/钉住」浮窗 | 需要，热键 `Alt+\`` |
| Q5 | 是否需要中英文 | 中文优先，类型名双语 |
