# NxGeo — NX10 几何信息悬浮窗

在 Siemens NX 10 里选中任意对象（体 / 面 / 边 / 特征），旁边立刻浮出一块信息面板：类型、尺寸、包围盒、拔模角、质量、到上一个对象的距离……
**不打断建模，不进命令状态，面板不动手。**

```
┌──────────────────────────────────────┐
│  ○̸   ↻   ✕                           │  ← 工具条（取消选择/复位/关闭）
│  名称    块(0)                        │
│  类型    特征·BLOCK                   │
│  tag     38467                        │
│  颜色    129                          │
│  图层    1                            │
│  包围盒  0,0,0 → 50,10,10 mm          │
│  体积    946.350 mm³                  │
│  表面积  567.810 mm²                  │
│  质量    7.41 g                       │
└──────────────────────────────────────┘
```

## 能显示什么

| 字段 | 适用对象 | 数据来源 |
|---|---|---|
| 名称 | 全部 | `UF_OBJ_ask_name` |
| 类型（含子类型 + 类型号） | 全部 | 特征 `UF_MODL_ask_feat_type`；面 `UF_MODL_ask_face_data`；边 `UF_MODL_ask_edge_type` |
| tag / 颜色 / 图层 | 全部 | `UF_OBJ_ask_display_properties` |
| 包围盒 | 全部 | `UF_MODL_ask_bounding_box` |
| 面积 / 周长 | 面 | NXOpen Measure |
| 长度 | 边 / 曲线 | NXOpen Measure |
| 半径 | 圆弧边 | `UF_EVAL_ask_arc` |
| 法向 / 拔模角 | **仅平面** | `UF_MODL_ask_face_data` + `norm_dir` |
| 体积 / 表面积 / 质量 | 体（特征按其生成的体） | `UF_MODL_ask_mass_props_3d` |
| 距离（到上一个对象） | 全部 | `UF_MODL_ask_minimum_dist_2` |
| 特征 | 面 / 边 | `UF_MODL_ask_face_feats` / `ask_edge_feats` |
| 零件 | 装配 occurrence | `UF_OBJ_ask_owning_part` + `UF_PART_ask_part_name` |

空字段自动隐藏。曲面不显示法向/拔模角——`UF_MODL_ask_face_data` 对圆柱/圆锥/圆环返回的是**轴线**而不是法向，与其显示假数据，不如不显示。

## 交互

| 操作 | 效果 |
|---|---|
| 选中对象 | 面板出现在**点击位置**右下方 |
| 拖动面板（任意空白处） | 面板跟着手走；之后新对象的面板出现在**拖到的位置** |
| 工具条 ↻ 复位 | 回到"新对象出现在点击位置" |
| 工具条 ○̸ 取消选择 | 给 NX 发 Esc，由 NX 清空选择与高亮；**NX 有命令对话框时不发送** |
| 工具条 ✕ 关闭 | 隐藏，选中下一个对象时自动回来 |
| `Ctrl+Alt+G` | 开关整个浮窗（唯一的全局快捷键） |

位置与模式存在 `nxgeo.layout`（与 exe 同目录）。

## 架构

```
NX 主线程                                独立进程
┌──────────────────────────┐            ┌───────────────────────┐
│ NxGeo.NxPlugin.dll       │  共享内存  │ nxgeo-overlay.exe     │
│  · 消息窗口 + 50ms 定时器 │  seqlock  │  · 分层窗口(LWA_ALPHA)│
│  · 轮询选择列表           │ ────────▶ │  · GDI 双缓冲绘制     │
│  · L0/L1/L2 查询          │  事件唤醒 │  · 只读快照，不加载 NX│
└──────────────────────────┘            └───────────────────────┘
```

- **所有 NX API 调用都在 NX 主线程**（定时器 tick 内）。用后台线程调 NXOpen/UFUN 会让 NX 崩溃——这一点实测过。
- **浮窗是独立进程**，只读共享内存，不链接任何 NX 库；它崩了不影响 NX，插件看门狗会重新拉起。
- 快照是 504 字节的 POD 结构，带协议版本号；两侧版本不一致时浮窗直接提示，不会读出乱七八糟的数据。

## 构建

要求：

- Windows x64
- Siemens NX 10.0（`UGOPEN` 头文件与库）
- Visual Studio 2019（MSVC v142 工具集）+ MSBuild

```powershell
.\build.cmd          # 编译全部工程并跑单元测试
```

NX 不在 `D:\Program Files\Siemens\NX 10.0` 时，改 `src\NxGeo.NxPlugin\NxGeo.NxPlugin.vcxproj` 里的 `NX10_DIR`，或传参：

```powershell
& $msbuild .\src\NxGeo.NxPlugin\NxGeo.NxPlugin.vcxproj /p:NX10_DIR="C:\Program Files\Siemens\NX 10.0"
```

## 安装

```powershell
install\install.cmd -UserDir "<UGII_USER_DIR>"
```

（用 `.cmd` 而不是 `.ps1`：后者在默认执行策略下会被拦。）

它会复制两个文件到 `<UserDir>\startup`：

- `NxGeo.NxPlugin.dll` — NX 启动时加载，走 `ufsta`
- `nxgeo-overlay.exe` — 由插件按需拉起

重启 NX 生效。卸载用 `install\uninstall.ps1`。

## 性能

分层按成本切，**点击瞬间不出重活**：

| 层 | 内容 | 典型耗时 | 时机 |
|---|---|---|---|
| L0 | 名称/类型/tag/颜色/图层/包围盒 | 微秒 | 选中立即 |
| L1 | 面积/周长/长度/半径 | 毫秒 | 选中立即 |
| L2 | 体积/表面积/质量/距离 | 10~100 ms | **停手 120 ms 后** |

- 连续快速点选时，L2 只保留**最新一个**请求，前面直接丢弃
- 去抖期间又换了对象，旧任务**连算都不算**
- 浮窗靠**命名事件**唤醒，不空转轮询
- 超过 8 ms 的操作写日志：`slow: poll=Nms l2=Nms`

## 已知限制

- **没有 hover**。NX10 没有"不进命令状态"的悬停回调，实测排除了 4 种方案（见 `docs/backlog/2026-11-19-reverse-mosheng-hover.md`）。当前是"选中即显示"。
- 多选只显示**最新选中**的那个。
- 距离只有"到上一个对象"的最小距离，不支持方向/投影距离。
- 拔模角相对 +Z，且只对平面给出。
- "零件"显示零件文件名，同一零件多次装配时无法区分实例。
- 无几何特征（草图、基准面等）只显示名称/类型/tag/颜色/图层，不做测量。
- 只处理工作部件。
- 协议版本 v5：插件与浮窗必须同版本。

## 排障

日志：`%TEMP%\nxgeo.log`

| 现象 | 看什么 |
|---|---|
| 完全没面板 | 日志里有没有 `overlay created:`；没有 = 插件没加载 |
| 面板不显示 | `tick: state=0` = 当前无选择 |
| 取消选择按钮无反应 | `deselect: blocked, NX busy` = NX 有命令对话框，护栏按设计拦截 |
| 快捷键无效 | `hotkey toggle FAILED` = Ctrl+Alt+G 被别的软件占用 |
| 卡顿 | `slow: poll=Nms l2=Nms` |

## 目录结构

```
src/NxGeo.Core/       协议、快照编解码、seqlock 通道、调度器、选择差异、日志
src/NxGeo.NxPlugin/   NX 插件：ufusr 入口、主线程 tick、选择轮询、NX 查询适配器
src/NxGeo.Overlay/    浮窗进程：窗口、视图模型、定位、工具条
tests/NxGeo.Tests/    单元测试（21 组）
docs/                 设计、实施计划、验收、悬停归档
install/              安装 / 卸载脚本
```

## 测试

`build.cmd` 会编译并运行 `tests\NxGeo.Tests`，覆盖协议编解码、seqlock 读写、调度器去抖与过期丢弃、选择差异、视图模型字段与单位、浮窗定位夹取。

## License

MIT，见 [LICENSE](LICENSE)。
