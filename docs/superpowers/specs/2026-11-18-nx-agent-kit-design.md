# NX Agent Kit 设计规格（v1）

> 状态：待用户评审（brainstorming 阶段 6/7）
> 日期：2026-11-18
> 定位：给 AI Agent 操作 Siemens NX 的「契约先行」运行时 —— MCP 是手，Skill 是脑
> 目标版本：Siemens NX 10（Python 3.3）→ NX 2512（Python 3.12）
> 决策记录见 §2；本文件是行动依据，不是调研笔记。

---

## 1. 背景与调研结论

### 1.1 已确认的调研输入（2026-11-18）

| 来源 | 关键结论 |
|---|---|
| [DreamEnding/NX_MCP](https://github.com/DreamEnding/NX_MCP) 184★ MIT | 双进程（Python sidecar + NX 内桥）、随机端口 + 256bit token、`execution_state` 禁止自动重放、`bridge.json` 描述符每次重读。未签名 .NET 需 `dotnet_author` 许可 → 桥走纯 Python 更稳 |
| [CAD-Agent-Hub](https://github.com/Cai-aa/CAD-Agent-Hub) 179★ MIT | 危险操作三重闸（env + dry_run + 确认串）、`skills/<域>/SKILL.md` 渐进加载；70+ 工具全常驻导致模型选不准 |
| [kongbaic/NX_MCP-Enhanced](https://github.com/kongbaic/NX_MCP-Enhanced) MIT | Loader DLL + 命名管道、`install.ps1` 一键装、`BACKEND=auto` 降级 |
| [Matt-wzy/nx12-mcp-bridge](https://github.com/Matt-wzy/nx12-mcp-bridge) GPL-3.0 | 68 工具、NX12 实战坑表最全；GPL 传染，仅可读不可抄 |
| [27painter/dsh-nx](https://github.com/27painter/dsh-nx) MIT | 永远自起批处理 NX；**日志标记判成败（退出码不可信）**；超时 `taskkill /T /F` 杀进程树；probe→verify→deliver |
| [EVOL233awa/nxopen-journal](https://github.com/EVOL233awa/nxopen-journal) 7★ MIT | Skill 型：SKILL.md + references 九域 + AST 坑规则库 + 50 回归用例；批处理不能截图，用 PDF/STL 离线渲染 |
| [mingfeng6684/nxopen-mcp](https://github.com/mingfeng6684/nxopen-mcp) 21★ MIT | 本机 NX 文档索引（9.8 万成员）+ 混合检索，API 幻觉 21%→0%；BGE-M3 约 2GB 不适合内网 → 改用 FTS5/BM25 |
| 官方 NX 2612 `DcMcpServer` | 12 工具；检索 / 沙箱执行 / 会话执行三分。2512 无此组件 |

行业共识：① 必有「NX 外服务 + NX 内桥」两进程；② 工具 16–40 个 + 按需发现是甜区；③ 执行前静态校验是替代编译器报错的关键；④ 成败看日志/实测几何，不看退出码；⑤ 经验沉淀形态是 `SKILL.md 铁律 + references + 回归用例`。

### 1.2 本机存量资产（只借鉴设计，不搬代码）

| 资产 | 可借鉴 | 明确不继承 |
|---|---|---|
| `D:\NX_Agent\ngxz-ya`（NX-YA 本地改版） | 工具注册表、catalog 按需发现（5 常驻 ≈765 token）、经验库 `v=1` 闸门、安装器、多版本扫描、文件队列 + NX 内 Py 桥 | 纯 PS 宿主、`tools/list` 字符串化 JSON、包/运行时双份漂移 |
| `D:\NX_Agent\NXAgentLink` | 38 工具的命令表、`nx_check` 21 条实测规则、`verify.evidence` 语义、快照 | 无 batch/无异步/超时后 job 仍执行 |
| `D:\NX_Agent\nx-agent` | `verify/`（nc_lint/nc_diff）、快照 | 与 ngxz-ya 重复的桥 |
| `D:\NX_Agent\docs\NX10-AI-Agent-方案.md`、`NX2512-AI-Agent-方案.md` | 两版 NX 底座实测事实（目录、.NET、Py 版本、批处理入口、签名、无 pip） | — |
| `D:\code\nxopen-mcp` | 知识库索引与检索思路、评测 | 嵌入模型（体积/离线） |

### 1.3 当前实机状态（2026-11-18 实测）

- NX 2512（pid 8016）已连接但 60s 未响应，可能在对话框/长命令中；本机另装 NX 10 / NX 2412。
- 当前会话挂的 nx MCP 来自 `D:\NX_Agent\NXAgentLink\nx_mcp_server.ps1`（旧链路），**不要**在它上面继续加功能。

---

## 2. 已确认决策（brainstorming 问答记录）

| # | 决策 | 结论 | 否决的选项 |
|---|---|---|---|
| D1 | v1 第一成功标准 | **日常模具二开提效**（自己每天用得上） | 通用建模 / 插件验证平台 / 社区发布 |
| D2 | v1 日常任务覆盖 | 改现有零件、模具结构模板化建模、写/调插件代码验证（批量与图纸 P1） | — |
| D3 | 部署边界 | 原型优先，暂不限制（零安装不作为 v1 硬约束） | 纯 PS 硬约束 / 绿色 Python / 离线装 Python |
| D4 | 路线 | **新仓库从零重写**，只吸收设计 | 演进 NX-YA / 双进程重构旧资产 / 只做 Skill |
| D5 | MCP host 技术栈 | **Python 3.12**；NX 内桥保持 **Py 3.3 语法下限** | PS 5.1 host / 双实现 |
| D6 | v1 工具面 | **6 常驻 + 分域 22**，`nx_find`/`nx_call` 兜底 | 更收敛 16 / 更扩展 14+ 建模 |

---

## 3. 目标与非目标

### 3.1 目标（v1）

1. 一句话改件：`nx_inspect` 定位 → 改（工具或代码）→ 实测验证 → 快照可退，NX10 与 2512 双版本可用。
2. 模板化建模：模架/镶件/滑块/流道做成数据驱动模板（`template.json`），参数一填即出，出后自动判据。
3. 插件验证闭环：`nx_check`（语法/符号/arity/坑）→ 有头或 `run_journal` 无头执行 → 日志判据 + 几何回读 → 回归对比。
4. 经验可积累：条目带 NX 版本/使用统计/成功率，可被 `exp_search` 命中并重放；导入导出。
5. 小模型可用：常驻工具 ≤6、`tools/list` ≤1k token；错误带 `next`；Skill 提供模板句。

### 3.2 非目标（v1 不做）

CAM、装配干涉、图纸/PMI 读取、曲面、云端同步、零安装发布、`2612` 官方 MCP 对接。

---

## 4. 功能需求

### 4.1 场景与优先级

| 场景 | 典型动作 | 优先级 |
|---|---|---|
| A 改现有零件 | 打开 .prt → 识体/特征 → 改尺寸/移动面/打孔 → 量了验证 → 存盘/快照 | P0 |
| B 模板建模 | `nx_template apply` 模架/镶件/滑块/流道 → 多步 → 判据 | P0 |
| C 插件代码验证 | 写 NXOpen Python → 预检 → 有头/无头跑 → 看错误/截图/量测 → 回归 | P0 |
| D 批量/无人值守 | 多件改参、批量导出、出图 | P1 |
| E 读图/PMI | 工程图标注当判据 | P2 |

### 4.2 能力需求（对应 6 条原始诉求）

| 能力诉求 | v1 落点 |
|---|---|
| 1 用对用准 | `nx_api`（本机 XML/pyi 索引 + 许可检查）+ `nx_check` 预检 + matrix 标注 verified/expected/unavailable |
| 2 看懂图档 | `nx_inspect`（体/特征/面/邻接/包围盒/体积）、`nx_measure`、`nx_screenshot`（命名视图/局部） |
| 3 中断/回滚/记录 | undo mark + `.prt` 快照 + plan 级回滚；job 取消；`nx_journal` 操作日志 |
| 4 执行前后验证 | 三层闸（预检/事务/实测）；`nx_assert` 声明式判据；evidence 只能来自 NX 实测 |
| 5 自进化 | 经验库 v2（版本/usage/成功率）+ 模板 pack + 导出导入 |
| 6 模板提示 | 3 个 SKILL.md + references + 模板句 |

### 4.3 非功能需求

| 项 | 要求 |
|---|---|
| 兼容 | 桥代码 Py 3.3 语法下限；生成代码按 `target_ver` 选语法级别；矩阵驱动可用性 |
| 上下文 | 常驻 6 工具；`tools/list` ≤1k token（catalog 档） |
| 时延 | 小调用 P50 < 1.5s；>2s 一律进 job |
| 可靠 | 超时 job 不重复执行（`request_id` 幂等）；NX 崩溃不丢 host 侧 job 记录 |
| 安全 | 文件只写工作区；`dry_run` 默认开；破坏性工具需确认串；无自动重放 |
| 可测 | offline 秒级、live、headless 三层；双版本矩阵 |

---

## 5. 架构

### 5.1 三层（契约 / 运行时 / 桥）

```
① contract/   语言无关唯一权威：commands · tools · result · entry · assert · matrix
② runtime/    Python 3.12 MCP host：stdio、注册、发现、闸门、调度、校验、验证、库
③ bridge/     NX 内 Python（3.3 语法下限）：有头 tick + 无头 run_journal 入口
```

### 5.2 进程与传输

- **有头**：文件队列 `io/<nx_pid>/{inbox,outbox}`（一行一个 JSON 文件）↔ NX 内 bridge（UI 线程 250ms tick + `LockUgAccess`）。**v1 只用文件队列**：可留痕、无端口、崩溃可复盘；命名管道只作为以后的可选加速，不进 v1。
- **无头**：host 拉起 `run_journal.exe` 子进程，同一 `commands.json`；判据 = 日志标记 + 产物量测，**退出码不可信**。
- **异步**：所有 `timeout_s` 以上操作返回 `job_id`；有头取消 = 协作式 STOP；无头取消 = `taskkill /T /F`。
- **多 NX**：`nx_status pid=...` 钉住会话；一个会话一条队列，同一时刻一个 job 执行，其余排队。

### 5.3 仓库结构

```
nx-agent-kit/
├─ contract/
│  ├─ commands.json          # 桥侧命令表
│  ├─ tools/{meta,core,model,query,mold,exec,api,exp}.json
│  ├─ schemas/{result,entry,assert,matrix}.schema.json
│  └─ matrix/{nx10,nx2412,nx2512}.json   # probe 产出（可提交）
├─ runtime/nxkit/
│  ├─ server.py registry.py router.py bridge.py jobs.py
│  ├─ precheck.py verify.py journal.py
│  ├─ library/{index.py,store.py}
│  ├─ kb/{build.py,search.py}
│  └─ tests/  (offline 单测)
├─ bridge/
│  ├─ startup/nx_agent_kit.py   # NX 启动加载，3.3 兼容
│  ├─ commands/model.py ...     # 每个文件一个域
│  └─ headless/run.py           # run_journal 入口
├─ skills/{nx-modify,nx-mold-template,nx-plugin-verify}/
│  ├─ SKILL.md  references/  templates/
├─ deploy/{probe.ps1,install.ps1,sync.ps1,uninstall.ps1}
├─ evals/{tasks/*.json,run.py,report/}
├─ library/{index.json,moves,pits,recipes,templates}/   # 运行期数据，不进版本库
├─ tests/{offline.ps1 or run_tests.py, live.py, headless.py}
└─ docs/
```

### 5.4 部署（单机内网）

1. `deploy/probe.ps1` 扫描已装 NX（注册表 / `UGII_BASE_DIR` / Program Files）→ 写 `contract/matrix/*.json`。
2. `deploy/install.ps1`：把 `bridge/startup/nx_agent_kit.py` 与 home 指针复制到 `UGII_USER_DIR\startup` 或向 `custom_dirs.dat` 追加路径（不覆盖已有内容）。
3. 输出 MCP 配置片段（Claude Code / Desktop 通用）：

```json
{"mcpServers":{"nx":{"command":"<venv>\\Scripts\\python.exe",
 "args":["D:\\NX_Agent\\nx-agent-kit\\runtime\\server.py"]}}}
```

4. 冒烟：`nx_new_part` → `nx_block` → `nx_measure` → 快照 → 回滚 → `RESULT: OK`。

---

## 6. 核心契约（先定义后写码）

### 6.1 `result.json`（统一信封）

```json
{"ok": false,
 "request_id": "6f1c…",
 "data": null,
 "verify": null,
 "error": {
   "code": "NX_BUILDER_FIRST_ARG",
   "stage": "nx",
   "msg": "CreateBlockBuilder 第一个参数应为 BlockBuilder.Null",
   "next": "nx_api members=BlockBuilder；用 BlockBuilder.Null 重试",
   "hint_tool": "nx_api"
 },
 "job": null,
 "elapsed_ms": 812}
```

规则：`ok:true` 且改了几何时 `verify` 必有 `what/expect/got/tol/evidence`；`stage ∈ precheck|bridge|nx|verify|rollback`；错误码前缀对应 stage。

### 6.2 `commands.json` 条目

```json
{"name":"nx_hole","group":"model","py":"commands.model.hole",
 "args_schema":{"type":"object","required":["target","position","diameter"],
   "properties":{"target":{"type":"string"},"position":{"type":"array"},"diameter":{"type":"number"}},
   "additionalProperties":false},
 "changes_geometry":true,"undo":"auto","timeout_s":120,
 "min_py":"3.3","matrix":{"nx10":"verified","nx2512":"verified"}}
```

### 6.3 `tools/*.json` 条目

```json
{"group":"model","label":"建模","order":20,"tools":[
 {"name":"nx_hole","s":"按点/面打孔","k":["孔","hole","钻孔"],
  "desc":"在 NX 当前工作零件上创建简单孔或沉头孔；返回 verify。",
  "route":{"kind":"nx","cmd":"nx_hole"},
  "flags":{"always":false,"verify":true,"gate":true,"expstep":true},
  "schema":{"type":"object","required":["target","position","diameter"],
    "properties":{"target":{"type":"string","description":"体：last / JournalIdentifier / 几何选择"},
      "position":{"type":"array","items":{"type":"number"},"description":"[x,y,z] mm"},
      "diameter":{"type":"number","description":"孔径 mm"},
      "depth":{"type":"number","description":"可选，默认通孔"}},
    "additionalProperties":false},
  "next":"失败先 nx_errors；位置不确定先 nx_inspect point=[x,y,z]",
  "matrix":{"nx10":"verified","nx2512":"verified"}}]}
```

### 6.4 `entry.json`（经验库 v2）

```json
{"id":"M0044","c":"招式","t":"按选择器批量倒角","k":["倒角","chamfer"],
 "sw":"NX10+|Py3.3","general":"…","impl":[{"tool":"nx_chamfer","params":{"target":"last","width":1.0}}],
 "evidence":{"nx_ver":"2512","expect":2.0,"got":2.0,"tol":0.01},
 "v":1,"usage":{"count":7,"success":6,"fail":1,"last":"2026-11-18"},
 "requires":{"py":"3.3","lic":[]},"source":"agent","lib_version":2}
```

- 分目录：`moves/(M)`、`pits/(K)`、`recipes/(R)`、`templates/(T)`。
- 索引：SQLite FTS5；中文按双字切分写入（规避 unicode61 分词差）。
- 入库条件：`evidence` 来自 NX 实测且与 `expect` 差值 ≤ `tol` 才 `v=1`；否则进 `_drafts`。
- 闸门：`hard` 默认（本轮未搜库且库内有相关条目则拦，`skip_search=1` 逃生，按零件重置）；`soft`/`off` 可切。

### 6.5 `assert.json`（判据）

```json
{"name":"hole_pitch_ok","kind":"pos","measure":{"type":"distance",
  "a":{"tool":"nx_inspect","expr":"holes[0].center"},"b":{"tool":"nx_inspect","expr":"holes[1].center"}},
 "expect":{"value":50,"tol":0.05},"unit":"mm"}
```

`kind ∈ geom|pos|rel`；`rel` 用于壁厚、间隙、面距。

### 6.6 `matrix/*.json`

```json
{"nx":"2512","ugraf":"…\\NXBIN\\ugraf.exe","py":"3.12.11","syntax":"3.12",
 "stubs":true,"xml":["NXBIN\\managed\\NXOpen.xml"],"run_journal":"…\\NXBIN\\run_journal.exe",
 "tools":{"nx_block":"verified","nx_screenshot":"expected","nx_template":"verified"}}
```

---

## 7. 工具清单（v1，6 + 22）

| 域 | 工具（22 个） | 说明 |
|---|---|---|
| meta(6, 常驻) | `nx_status` `nx_find` `nx_help` `nx_call` `nx_job` `exp_search` | 发现/调度/经验入口 |
| part(3) | `nx_part`(action=new\|open) `nx_save` `nx_snapshot`(save\|list\|restore) | 改件起点与退路 |
| model(8) | `nx_block` `nx_extrude` `nx_revolve` `nx_hole` `nx_fillet` `nx_chamfer` `nx_boolean` `nx_move_face` | 改件与插件验证最低集 |
| mold(1) | `nx_template`(list\|apply) | 模板数据驱动 |
| query(3) | `nx_inspect` `nx_measure` `nx_screenshot`(view=命名视图/局部) | 看得懂图档 |
| exec(5) | `nx_check` `nx_run_python` `nx_plan` `nx_batch` `nx_assert` | 写码/多步/无头/判据 |
| api(1) | `nx_api` | 本机 XML/pyi 检索 + `lic` |
| exp(1) | `nx_exp`(get\|save\|replay\|export\|import) | 自进化 |

**按需发现**：域工具默认不列出；`tools.expose=catalog|model,query|all`；改变时推 `notifications/tools/list_changed`。`nx_find` 始终搜全库，`nx_call` 可调任意注册工具。

### 7.1 模板 pack（模具杠杆）

```json
{"id":"T002","name":"镶件-圆形","params":[{"n":"d","type":"number","min":10,"max":200}],
 "steps":[{"tool":"nx_block",...},{"tool":"nx_hole",...}],
 "asserts":[{"kind":"rel","measure":{"type":"wall_thickness"},"expect":{"value":3,"tol":0.2}}],
 "matrix":{"nx10":"verified","nx2512":"verified"}}
```

首批模板：模架、圆形镶件（P0）；滑块、流道（P1）。

---

## 8. 数据流

**① 改件**：`nx_inspect` →（`exp_search`）→ `nx_api` 查签名 → `nx_check` → `nx_call`/`nx_run_python`（job）→ `verify` → `nx_assert` → `exp_save`。
**② 模板**：`nx_template list` → `apply` → 逐步 verify → 整体 assert → 快照/存盘。
**③ 插件**：`nx_check(code,target_ver)` → 执行（有头/`nx_batch`）→ `nx_errors` → `nx_screenshot`/`nx_assert` → 回归 diff。

失败统一路径：预检拦（不执行）→ 执行失败自动落坑草稿 → `exp_save id=坑` 补 `fix`。

---

## 9. 验证、事务与幂等

- 三层闸：预检（AST/语法级别/符号/arity/坑规则）→ 事务（undo mark；失败回滚）→ 实测（facts vs expect）。
- 三级退路：undo mark（~190 上限）→ `.prt` 快照（`index.jsonl` 清单）→ plan 级回滚。
- 取消：有头协作式；无头杀进程树；超时 job 不得在之后偷偷执行。
- 幂等：`request_id` 去重；重复请求返回同一结果。

## 10. Skill 层

| Skill | 内容 | 渐进加载 |
|---|---|---|
| `nx-modify` | 读图→定位→改→验 | `references/{locate,modify,verify}.md` + `templates/step-prompts.md` |
| `nx-mold-template` | 选模板→填参→判据 | `references/template-catalog.md` + 参数表 |
| `nx-plugin-verify` | 写码→预检→跑→回归 | `references/pitfalls.md`（错误码→修法） |

SKILL.md 只放：何时用、下一步调什么工具/说什么话、硬规则；细节进 references。

## 11. 测试与评测

| 层 | 内容 | NX |
|---|---|---|
| offline | 契约↔命令表↔桥三方一致性、schema 校验、注册派生、闸门、预检规则 | 否，秒级 |
| live | 起 host + 真 NX：建块→布尔→测量→改面→快照→回滚→截图→存盘（15 项） | 是 |
| headless | 10 个 journal 批处理：日志标记 + 产物量测判据 | 是（独立进程） |
| evals | 10 个模具任务（改件 5/模板 3/插件 2）× NX10+2512 | 是 |

评测指标：任务成功率、平均工具步数、耗时、回滚成功率、`tools/list` token、`nx_find` 命中率。

## 12. 里程碑与验收

| 阶段 | 内容 | 验收（实测） |
|---|---|---|
| M0 契约骨架（2d） | contract 五件 + host 起 + `status/find/help/call` + offline 绿 | catalog 只出 6 个；`nx_call` 能调任意域工具 |
| M1 执行内核（3–4d） | 有头桥 + 无头桥 + job + verify + 快照回滚 | NX10 与 2512 各跑通建块/量体积/回滚 |
| M2 工具面（3–4d） | 22 域工具 + 模板 pack + KB | 自然语言建"带孔法兰"并改尺寸，无手点 |
| M3 Skill+评测（3d） | 3 Skill + 10 评测 + 双矩阵 | 新会话一句话跑通改件，成功率 ≥70% |
| M4 收尾（2d） | 安装/文档/演示件 | 干净机器 15 分钟可用 |

**v1 完成定义**：双版本 live + headless 冒烟通过；offline 全绿；10 个 eval 成功率 ≥70% 且回滚 100%；经验库条目带版本/usage 且能被复用一次；3 个 SKILL.md 在 Claude Code 新会话可用。

## 13. 风险与对策

| 风险 | 对策 |
|---|---|
| 契约与实现漂移 | 单一来源 + CI parity（PS/Py 曾静默失效） |
| NX10 仅 Py 3.3 | 语法下限 + 预检 + 代码模板 |
| 有头/无头判据不一 | 同一 `assert.json`，两执行器共用 |
| undo 上限 | plan 分段 + 大改前快照 |
| 小模型选错工具 | 常驻 6 + 闸门 + `next` + find 卡片 + Skill 模板句 |
| 任意代码执行 | 工作区路径、dry_run、确认串、只读白名单 |
| 未签 .NET 许可 | 桥走纯 Python journal，不引入 C# 依赖 |
| 绿色 Python 打包延后 | v1 先用本机 Python；部署期再定绿色包 |

## 14. 待确认（默认值已给，无异议按默认执行）

| # | 问题 | 默认 |
|---|---|---|
| Q1 | 仓库名与位置 | `nx-agent-kit`，`D:\NX_Agent\nx-agent-kit`（git 初始化） |
| Q2 | 首选客户端 | Claude Code（MCP stdio）；Desktop/DSH 兼容即可 |
| Q3 | 开源与许可 | 默认私有、暂不发布；将来发布默认 MIT |
| Q4 | 工具描述语言 | 中文为主，工具名/参数英文 |
| Q5 | NX10 首期是否实机 | 是：M1 就要双版本冒烟 |

## 15. 附录：本规格的调研来源

- 竞品：[DreamEnding/NX_MCP](https://github.com/DreamEnding/NX_MCP)、[xupeiwust/NX_MCP](https://github.com/xupeiwust/NX_MCP)、[CAD-Agent-Hub](https://github.com/Cai-aa/CAD-Agent-Hub)、[kongbaic/NX_MCP-Enhanced](https://github.com/kongbaic/NX_MCP-Enhanced)、[Matt-wzy/nx12-mcp-bridge](https://github.com/Matt-wzy/nx12-mcp-bridge)、[27painter/dsh-nx](https://github.com/27painter/dsh-nx)、[EVOL233awa/nxopen-journal](https://github.com/EVOL233awa/nxopen-journal)、[mingfeng6684/nxopen-mcp](https://github.com/mingfeng6684/nxopen-mcp)。
- 本机实测：`D:\NX_Agent\docs\NX10-AI-Agent-方案.md`、`D:\NX_Agent\docs\NX2512-AI-Agent-方案.md`、`D:\NX_Agent\ngxz-ya`、`D:\NX_Agent\NXAgentLink`、`D:\NX_Agent\nx-agent`。
