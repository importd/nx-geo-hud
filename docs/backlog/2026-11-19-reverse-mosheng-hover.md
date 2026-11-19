# 归档：逆向「几何速览（悬浮显示）」功能

> 日期：2026-11-19
> 状态：调研归档（未开始深度逆向）
> 目标软件：模圣 NX 工具 v6.127（`D:\MS_NXTOOLS`）
> 我方项目：`E:\nxgeo`（NX10 几何信息悬浮窗）

---

## 1. 为什么要归档这件事

我方 v1 已实现**选中即显示**（点选对象 → 浮窗显示类型/tag/颜色/包围盒/体积）。用户要的是**悬停即显示**（鼠标划过就出信息）。

NX10 公开 API 里没有 hover/预选回调（已全量扫 `UGOPEN\*.h`、`NXOpen\*.hxx`、`NXOpen.xml` 证实）。
用户记得模圣有该功能，因此把「逆向模圣的实现方式」作为一条独立技术路线归档。

## 2. 我方已验证的事实（避免重复踩坑）

| 结论 | 证据 |
|---|---|
| `UF_OBJ_ask_display_properties().highlight_status` 在悬停时**不变** | 实测：每次 tick 扫全部体，悬停方块时日志 `highlighted bodies: []` 无变化 |
| 普通点选拿到的是 **特征**（type 205 `UF_feature_type`），不是体/面/边 | 日志 `selCount=1 globalCount=1 selTag=38467`，`UF_OBJ_ask_type_and_subtype` → 205 |
| 特征可映射到体 | `UF_MODL_ask_feat_body(feature, &body)` 成功，浮窗显示实体信息 |
| `SelectionManager` 与 `UF_UI_ask_global_sel_object_list` 结果一致 | 日志两个计数/tag 相同 |
| NXOpen/UF_UI 调用不能在后台线程跑 | 后台线程调 `UF_UI_ask_global_sel_object_list` 直接 `c0000005` 崩 NX |
| 主线程 tick 可行 | `message-only` 窗口 + `SetTimer`，在 NX 消息循环里跑，稳定 |
| ~~`UI::SelectionManager()->GetSelectedObject`~~ | 语义是「已选中」，不是悬停 |

## 3. 模圣二进制的静态观察（dumpbin）

### 3.1 `Startup\libmsstartup_x64.dll`（170 KB，NX 启动加载）

导出：`msstartup`、`ufusr`、`ufusr_ask_unload`、`ufusr_cleanup`（标准 NX 用户出口）。

导入（关键）：
- `KERNEL32`: `CreateFileMappingW` / `MapViewOfFile` / `UnmapViewOfFile` / `CreateEventW` → **共享内存 + 事件** IPC
- `USER32`: `SetTimer` / `KillTimer` → **主线程定时器**
- 无 NX 库导入，但二进制里有 `GetProcAddress` / `LoadLibraryW`

二进制中的 NX 符号字符串：
- `UF_UDOBJ_register_select_cb`
- `UF_UDOBJ_set_owned_object_selection`
- `UF_UI_select_with_single_dialog`
- `UF_UI_select_with_class_dialog`
- `UF_STYLER_ask_select_dialog_id`

→ 说明它们**运行时 `GetProcAddress` 动态解析 UFUN**，不做链接期依赖。

**与我方架构的对照**：共享内存 + 事件 + 主线程定时器，和我们 v1 的做法几乎一致（我们用的是 `message-only` 窗口 + `SetTimer` + seqlock 共享内存）。这条已被独立验证。

### 3.2 `Application\dlls\x64\libmosmtools.dll`（39 MB，主要功能）

依赖：**MFC 12.0**（`mfc120u.dll`）、`gdiplus.dll`、`USER32`、`GDI32`、`COMCTL32`、`UxTheme`。

导入（关键）：
- `SetWindowsHookExA` / `CallNextHookEx` / `UnhookWindowsHookEx` → **装了 Windows 钩子**
- `SetTimer` / `KillTimer` / `CreateTimerQueueTimer` / `SetWaitableTimer` → 多种定时器
- `GdiplusStartup` → GDI+ 绘制

→ 它们既装钩子也用定时器；UI 是 MFC 对话框 + GDI+。

## 4. 已实测排除的假设（2026-11-19 晚）

| 假设 | 实测结果 |
|---|---|
| `highlight_status` 能反映预选 | ❌ 体和特征在悬停时都不变 |
| `UF_UDOBJ_register_select_cb` 能给原生几何 hover | ❌ 只对 **UDO** 生效 |
| `UF_UI_select_with_single_dialog` 的 filter 能当 hover | ✅ **回调确实在悬停时触发**（日志抓到三个不同 tag），**但**它让 NX 进入**模态命令状态**，弹「名称」对话框；把窗口藏掉，命令状态仍在，用户无法建模 —— **不可用** |
| Block UI 选择块的 Filter 回调 | 官方文档说悬停触发，但需要 `.dlx` 对话框，同样是模态命令状态 —— **不可用** |

**结论：NX10 没有任何"无命令状态的悬停回调"。** 悬停只剩自建拾取一条路。

## 5. 待验证的假设（下一步）

按可能性排序：

**H1｜Win32 钩子只用来触发采样，拾取仍是 NX 的能力**
钩子（`WH_MOUSE`/`WH_GETMESSAGE`）只告诉它"鼠标动了"，真正的拾取靠某个 NX 接口。
→ 需要确认：它若有拾取能力，用的哪个导出？我们已排除公开 API，所以要找**非公开导出**。

**H2｜自己在 NX 视图里做射线求交**
用 `UF_VIEW_ask_view_matrix` + 反投影 + 与显示三角面求交。工作量最大但不需要 NX 内部接口。
→ 我们的 spec 里 `HoverSourceB` 就是这条路。

**H3｜`UF_UI_select_with_single_dialog` 的"预选回调"**
字符串里有这个 API。若它支持"选择过程中回调"（不点也能回调），可借它做悬停；但它通常是模态阻塞的。
→ 需要实测：在 NX10 里用 Python 跑一次，看回调触发时机。

**H4｜`UF_UDOBJ_register_select_cb`（UDO 选择回调）**
UDO 的选择回调是否在预选阶段触发，未验证。

## 5. 建议的验证顺序（成本从低到高）

1. **H3/H4 实测**（1~2 小时）：在 NX10 里用 Python 注册 `UF_UI_select_with_single_dialog` 和 UDO 回调，观察鼠标悬停时是否触发。不触发就排除。
2. **H1 导出表排查**（半天）：`dumpbin /exports` 扫模圣所有 DLL，找类似 `pick`、`hover`、`ray`、`locate` 的导出；同时看它是否链接了 NX 的非公开 DLL。
3. **H2 自建拾取**（1~2 天）：不依赖逆向，直接实现射线求交。作为保底路线。
4. 深度逆向（可选）：`libmosmtools.dll` 里定位几何速览的代码路径，看它到底调用什么。

## 6. 边界与风险

- 模圣是**商业软件**：只做静态观察与行为对照，**不复制其代码、不 patch 其二进制**。
- 深度逆向（反汇编 39 MB MFC DLL）成本高、收益不确定；H2 自建更可控。
- 若 H2 落地，精度与 NX 原生拾取可能有差异（容差需可配）。

## 7. 当前产品状态（与本归档分离）

`E:\nxgeo` 的 v1 已可用：点选 → 浮窗显示 类型/tag/颜色/包围盒/体积。
悬停未实现，产品上以「选择驱动」交付，等本归档的结论决定走 H2 还是 H1。
