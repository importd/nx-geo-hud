# NX10 几何信息悬浮窗 使用与验收（当前版本）

> 位置：`E:\nxgeo`（源码 + 构建）；安装目录：`%UGII_USER_DIR%\startup`
> 更新：2026-11-19 22:12

## 1. 安装

```
build.cmd
install\install.cmd -UserDir "<UGII_USER_DIR>"     # 用 .cmd，.ps1 会被执行策略拦
重启 NX10
```

产物：`NxGeo.NxPlugin.dll`（NX 启动加载）+ `nxgeo-overlay.exe`（插件拉起）+ `nxgeo.ini`（可选，自定义快捷键）

## 2. 能显示什么

选中对象后，浮窗按内容自动出字段（空字段不显示）：

| 字段 | 来源 | 适用 |
|---|---|---|
| 名称 | `UF_OBJ_ask_name` | 全部 |
| 类型 | 特征 `UF_MODL_ask_feat_type`；面 `UF_MODL_ask_face_data`（含半径/半角）；边 `UF_MODL_ask_edge_type` | 全部 |
| tag / 颜色 / 图层 | `UF_OBJ_ask_display_properties` | 全部 |
| 包围盒 | `UF_MODL_ask_bounding_box` | 全部 |
| 面积 / 周长 | NXOpen Measure | 面 |
| 长度 | NXOpen Measure | 边/曲线 |
| 半径 | `UF_EVAL_ask_arc` | 圆弧边 |
| 法向 / 拔模角 | `UF_MODL_ask_face_data` + 相对 +Z | 面 |
| 体积 / 表面积 / 质量 | `UF_MODL_ask_mass_props_3d` | 体 |
| 距离（到上一个对象） | `UF_MODL_ask_minimum_dist_2` | 全部，且需要先选过另一个对象 |
| 特征 | `UF_MODL_ask_face_feats` / `ask_edge_feats` | 面/边 |
| 组件 | `UF_OBJ_ask_owning_part` + `UF_PART_ask_part_name` | 装配 occurrence |

## 3. 交互

| 操作 | 效果 |
|---|---|
| `Ctrl+Alt+G` | 开关浮窗（唯一全局快捷键） |
| 面板上**右键** | 菜单：复制信息 / 钉住面板 / 鼠标点穿 / 隐藏面板 / 退出 |
| 托盘图标右键 | 同一个菜单 |
| 托盘图标双击 | 开关浮窗 |
| 鼠标移到面板上 | 面板冻结位置（否则永远点不到） |

## 4. 已知限制

- **没有 hover**：NX10 无「不进命令状态」的悬停回调（详见 `docs/backlog/2026-11-19-reverse-mosheng-hover.md` 的实测排除记录）。当前是「选中即显示」。
- 多选只显示**最新选中**的那个。
- 距离只到「上一个选中的对象」，不支持指定方向/投影距离。
- 质量按 NX 质量属性（kg），小于 1 kg 显示为 g。
- 拔模角固定相对 +Z，不跟随自定义开模方向。
- 只处理工作部件；装配下的组件名只在 occurrence 上显示。
- 协议版本 v5：插件与浮窗必须同版本，否则浮窗显示「协议版本不符」。

## 5. 排障

| 现象 | 先看 |
|---|---|
| 没浮窗 | `%TEMP%\nxgeo.log` 有没有 `ufsta` 行；没有 = 插件没加载 |
| 浮窗不显示 | 日志 `tick: state=0` = 空选择；`state=2 stay=0` = 前台/光标判断 |
| 右键无菜单 | 日志有没有 `menu: WM_RBUTTONUP received` |
| 快捷键无效 | 日志 `hotkey toggle FAILED` = 被 NX 占用，改用托盘 |
| 字段缺失 | 日志 `poll: count=… newest=…`，再对照第 2 节的适用对象 |
