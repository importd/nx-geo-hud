# NX Agent Kit 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 从零构建一个契约驱动的 NX Agent 运行时：Python 3.12 MCP host + NX 内 Py3.3 桥，支持改件、模板建模、插件验证三条 v1 主链路。

**Architecture:** 契约（`contract/` 五份 JSON）为唯一权威；Python host 负责 MCP 协议、注册发现、预检、调度、验证与经验库；NX 内桥只执行命令并回传实测 facts；有头走文件队列，无头走 `run_journal` 子进程，两者共用同一 `assert` 判据。

**Tech Stack:** Python 3.12、`mcp` 官方 SDK（lowlevel Server + stdio）、`jsonschema`、`pytest`、SQLite FTS5（标准库）、PowerShell 5.1（部署脚本）；禁止 numpy/PIL/pydantic。

**Spec:** `docs/superpowers/specs/2026-11-18-nx-agent-kit-design.md`

## Global Constraints

- Host 运行于 Python 3.12；`bridge/` 下所有文件必须通过 **Python 3.3 语法**校验（无 f-string、无变量注解、无 `:=`、无 dataclass）。
- 目标 NX：`10.0.0.24`（内嵌 Py 3.3.2）与 `2512`（Py 3.12）；矩阵在 `contract/matrix/` 标注 `verified|expected|unavailable`。
- 依赖只允许：`mcp`、`jsonschema`、`pytest`；其余用标准库。禁止 numpy/PIL/pydantic。
- 所有桥命令必须登记在 `contract/commands.json`，并有 offline 三方一致性测试（契约↔命令表↔桥实现）。
- 传输协议：`io/<nx_pid>/inbox|<outbox>/<request_id>.json`；`request_id` 幂等，重复请求返回同一结果。
- 结果信封固定：`{ok, request_id, data, verify, error, job, elapsed_ms}`；改了几何时 `verify` 必须含 `what/expect/got/tol/evidence`，`evidence` 只能来自 NX 实测。
- 常驻工具 6 个（`nx_status/nx_find/nx_help/nx_call/nx_job/exp_search`），域工具 22 个；`tools/list` catalog 档 ≤1k token。
- 仓库根：`D:\NX_Agent\nx-agent-kit`（git 初始化）。DSH 沙箱默认只写 session workspace；在该路径创建/写入需按提示申请一次工作区权限。
- 标识符英文、工具描述与 Skill 中文；所有 JSON 文件 UTF-8 无 BOM，读写显式 `utf-8`。
- 每个写几何命令执行前必须在桥内 `SetUndoMark`；运行期数据（`io/`、`library/`）不进版本库。

## Review Focus

以下 6 类输入/失败模式规格隐含但任务测试最容易漏，逐条在归属任务里用测试钉住：

1. **中文路径与中文关键词**：`D:\零件库\E-0021.prt`、`q=孔` → 读 JSON/日志不乱码、不抛 `UnicodeDecodeError`（T3 测试用中文目录）。
2. **NX 重启/换 pid**：旧 `outbox` 残留、`status.json` 过期 → 返回 `BRIDGE_UNAVAILABLE`，不许挂起或误读旧结果（T3）。
3. **并发与重入**：同一会话两个 job 同时到达 → 串行执行，不许 UI 线程重入（T10）。
4. **超时与迟到结果**：超时后迟到的 `outbox` 不得被当作成功；超时 job 不得在之后偷偷执行（T10 幂等测试）。
5. **工具暴露切换**：`expose=catalog` 后客户端缓存旧列表仍能用 `nx_call` 调域工具；变更推送 `list_changed`（T1 expose 测试、T6）。
6. **Py3.3 语法漂移**：`bridge/` 下任何 3.4+ 语法（f-string、注解）必须在 offline 被拦下（T8）。

---

## 里程碑映射

| 里程碑 | 任务 |
|---|---|
| M0 契约骨架 | T1–T2 |
| M1 执行内核 | T3–T5、T8–T10 |
| M2 工具面 | T6–T7、T11–T14 |
| M3 Skill+评测 | T15、T17 |
| M4 收尾+验收 | T16、T18 |

---

### Task 1: 仓库骨架与契约加载器

**Files:**
- Create: `pyproject.toml`, `.gitignore`, `README.md`, `tests/offline/conftest.py`
- Create: `contract/tools/meta.json`
- Create: `contract/schemas/tool.schema.json`
- Create: `runtime/nxkit/registry.py`
- Test: `tests/offline/test_registry.py`

**Interfaces:**
- Produces: `ToolDef`（frozen dataclass：`name, group, s, k, desc, route, flags, schema, next, matrix`）；`Registry(contract_dir: Path)` 提供 `.tools: dict[str, ToolDef]`、`.list(expose: str = "all") -> list[dict]`、`.get(name: str) -> ToolDef`；异常 `ContractError`。
- Consumes: 无（首任务）。

- [ ] **Step 1: 写失败测试** `tests/offline/test_registry.py`

```python
def test_meta_tools_load(contract_dir):
    reg = Registry(contract_dir)
    assert set(reg.tools) == {"nx_status", "nx_find", "nx_help",
                              "nx_call", "nx_job", "exp_search"}
    assert all(t.flags.get("always") for t in reg.tools.values())

def test_expose_catalog_only_always(contract_dir):
    reg = Registry(contract_dir)
    assert {t["name"] for t in reg.list("catalog")} == set(reg.tools)

def test_duplicate_tool_name_raises(contract_dir, tmp_path):
    # 复制 meta.json，追加同名工具
    with pytest.raises(ContractError):
        Registry(tmp_path)
```

- [ ] **Step 2: 跑测试确认失败**：`pytest tests/offline/test_registry.py -v` → `ModuleNotFoundError: nxkit.registry`

- [ ] **Step 3: 实现**
- `pyproject.toml`：`requires-python = ">=3.12"`，依赖 `mcp`、`jsonschema`，dev 依赖 `pytest`，包目录 `runtime`。
- `contract/tools/meta.json`：6 个工具，每个含 `name/s/k/desc/route/flags/schema/next/matrix`，`flags.always=true`，`route.kind ∈ {nx,local,batch}`。
- `contract/schemas/tool.schema.json`：JSON Schema，`required: [name, group, s, desc, route, schema]`，`additionalProperties: false`。
- `registry.py`：遍历 `contract/tools/*.json`，校验每项符合 schema，构建 `ToolDef`，同名抛 `ContractError`；`list(expose)` 中 `catalog` 只返回 `always`，其余值按 group 过滤，`all` 全量；输出 MCP 格式 `{"name","description","inputSchema"}`。

- [ ] **Step 4: 跑测试确认通过**：`pytest tests/offline/test_registry.py -v` → PASS
- [ ] **Step 5: 提交**：`git init && git add -A && git commit -m "feat: contract registry and meta tool definitions"`

---

### Task 2: MCP stdio 服务器骨架

**Files:**
- Create: `runtime/nxkit/server.py`
- Test: `tests/offline/test_server.py`

**Interfaces:**
- Consumes: `Registry`（T1）。
- Produces: `main(argv: list[str] | None = None) -> int`；支持方法 `initialize`、`tools/list`、`tools/call`、`ping`；stdin/stdout 逐行 JSON-RPC（UTF-8，`ensure_ascii=False`）；可用 `python -m nxkit.server` 启动，契约目录由 `NXKIT_CONTRACT_DIR` 覆盖。
- 本任务 `tools/call` 只做参数校验与占位错误（T6 再接入 Router）：未知工具返回 `{"code":"TOOL_NOT_FOUND"}`。

- [ ] **Step 1: 写失败测试**：启动子进程，依次发 `initialize`、`tools/list`、未知工具 `tools/call`

```python
def test_initialize_and_tools_list():
    proc = subprocess.Popen([sys.executable, "-m", "nxkit.server"],
                            stdin=PIPE, stdout=PIPE, text=True, encoding="utf-8")
    assert json.loads(proc.stdout.readline())["result"]["serverInfo"]["name"] == "nx-agent-kit"
    # 发送 tools/list → 断言 6 个名字；发送未知工具 call → error.code == "TOOL_NOT_FOUND"
```

- [ ] **Step 2: 跑测试确认失败**：`pytest tests/offline/test_server.py -v` → FAIL
- [ ] **Step 3: 实现** `server.py`：`mcp.server.lowlevel.Server` 注册 `list_tools` 回调（调 `Registry.list(expose)`）；`call_tool` 回调交 `Router`；`initialize` 返回 `protocolVersion: "2024-11-05"`；`capabilities.tools.listChanged=true`；日志走 stderr，stdout 只允许协议帧。
- [ ] **Step 4: 跑测试确认通过**：`pytest tests/offline/test_server.py -v` → PASS
- [ ] **Step 5: 提交**：`git add -A && git commit -m "feat: stdio MCP server skeleton"`

---

### Task 3: 文件队列桥客户端

**Files:**
- Create: `runtime/nxkit/bridge.py`
- Test: `tests/offline/test_bridge.py`

**Interfaces:**
- Consumes: 无（信封组装在 T4；本任务只返回桥原始 payload：成功 `{"data":..., "facts":...}`，失败 `{"error":{...}}`）。
- Produces: `BridgeClient(io_dir: Path)`；`.sessions() -> list[dict]`（读 `io/*/status.json`，字段 `pid, nx, py, alive, heartbeat_age_s`）；`.submit(pid: int, cmd: str, args: dict, timeout_s: float = 120.0) -> dict`（写 `inbox/<request_id>.json`，轮询 `outbox/<request_id>.json`，返回桥原始 payload）；异常 `BridgeUnavailable`、`BridgeTimeout`；`request_id` 用 `uuid4().hex`，重复请求（同 id）返回缓存结果。
- 传输只读/写 JSON 文件，路径支持中文，UTF-8。

- [ ] **Step 1: 写失败测试**：临时目录模拟桥（线程把 inbox 文件读到 outbox）；断言返回值；中文路径；过期 `outbox` 不误读

```python
def test_submit_roundtrip(tmp_path):
    io = tmp_path / "零件库" / "io"; io.mkdir(parents=True)
    run_fake_bridge(io)          # 线程：读 inbox/*.json → 写 outbox/同id.json
    client = BridgeClient(io)
    res = client.submit(8016, "nx_block", {"x": 0, "y": 0, "z": 0}, timeout_s=2)
    assert res["data"]["echo"] == "nx_block"

def test_stale_outbox_not_reused(tmp_path):
    # outbox 里预置旧 request_id 文件；新请求必须用新 id 且返回新结果
```

- [ ] **Step 2: 确认失败**：`pytest tests/offline/test_bridge.py -v` → FAIL
- [ ] **Step 3: 实现** `bridge.py`：`sessions()` 跳过损坏 JSON 并计入 `stale`；`submit()` 先写 `inbox` 再轮询 40ms 间隔；超时抛 `BridgeTimeout` 并留下 job 记录（T10 复用）；无 `io/` 目录抛 `BridgeUnavailable`。
- [ ] **Step 4: 确认通过**：`pytest tests/offline/test_bridge.py -v` → PASS
- [ ] **Step 5: 提交**：`git commit -am "feat: file-queue bridge client with idempotent request ids"`

---

### Task 4: 结果信封、verify 与 assert

**Files:**
- Create: `runtime/nxkit/result.py`、`runtime/nxkit/verify.py`
- Create: `contract/schemas/result.schema.json`
- Test: `tests/offline/test_result.py`

**Interfaces:**
- Produces: `make_ok(request_id, data=None, verify=None, elapsed_ms=None) -> dict`；`make_error(request_id, code, stage, msg, next=None, hint_tool=None) -> dict`；`build_verify(facts: dict, expect: dict | None, tol: float, what: str) -> dict`；`assert_holds(spec: dict, facts: dict) -> dict{"pass": bool, "got": float, "expect": float, "delta": float}`。
- `stage ∈ precheck|bridge|nx|verify|rollback`；`ok:true` 且 `data` 改动几何时 `verify` 必填。

- [ ] **Step 1: 写失败测试**

```python
def test_error_envelope_shape():
    e = make_error("r1", "NX_BOOL_EMPTY", "nx", "布尔不相交", next="检查两体是否相交", hint_tool="nx_inspect")
    assert e["ok"] is False and e["error"]["stage"] == "nx"

def test_verify_tolerance():
    v = build_verify({"volume": 1000.0}, {"volume": 1000.2}, 0.01, "volume")
    assert v["pass"] is True and v["delta"] == pytest.approx(0.2)

def test_assert_geom_measure_mismatch():
    spec = {"kind": "geom", "measure": {"type": "volume"}, "expect": {"value": 1000, "tol": 0.1}}
    assert assert_holds(spec, {"volume": 900})["pass"] is False
```

- [ ] **Step 2: 确认失败**：`pytest tests/offline/test_result.py -v` → FAIL
- [ ] **Step 3: 实现**：`result.py` 构造信封并校验 schema；`verify.py` 按 `geom|pos|rel` 取 `facts` 中量测、算 `delta`、`pass = delta <= tol`；缺量测时返回 `pass:false, got:null` 且 `error.code="VERIFY_NO_MEASURE"`。
- [ ] **Step 4: 确认通过**：`pytest tests/offline/test_result.py -v` → PASS
- [ ] **Step 5: 提交**：`git commit -am "feat: result envelope, verify and assert"`

---

### Task 5: 经验库 v2（SQLite FTS5 + 双字切分）

**Files:**
- Create: `runtime/nxkit/library/store.py`、`runtime/nxkit/library/index.py`
- Create: `contract/schemas/entry.schema.json`
- Test: `tests/offline/test_library.py`

**Interfaces:**
- Produces: `Library(root: Path)`：`.init()`、`.save(entry: dict) -> str`、`.get(id: str) -> dict | None`、`.search(q: str, limit: int = 10) -> list[dict]`、`.export_pack(dst: Path) -> int`、`.import_pack(src: Path) -> int`、`.bump_usage(id: str, ok: bool) -> None`。
- 规则：`evidence` 缺失或 `|got-expect| > tol` → 写 `_drafts/` 返回 `"draft:<id>"` 且不进索引；只索引 `v=1`。

- [ ] **Step 1: 写失败测试**

```python
def test_chinese_search(tmp_path):
    lib = Library(tmp_path); lib.init()
    lib.save({"id": "M0001", "c": "招式", "t": "按选择器批量倒角",
              "k": ["倒角", "chamfer"], "evidence": {"expect": 2.0, "got": 2.0, "tol": 0.01}})
    assert lib.search("倒角")[0]["id"] == "M0001"

def test_unverified_entry_goes_to_drafts(tmp_path):
    lib = Library(tmp_path); lib.init()
    assert lib.save({"id": "M0002", "c": "招式", "evidence": {"expect": 2.0, "got": 2.4, "tol": 0.01}}).startswith("draft:")
    assert lib.search("M0002") == []
```

- [ ] **Step 2: 确认失败**：`pytest tests/offline/test_library.py -v` → FAIL
- [ ] **Step 3: 实现**：`store.py` 负责文件读写（`moves/pits/recipes/templates` 与 `_drafts`）、schema 校验、`bump_usage`；`index.py` 建 `entry_fts`（含中文 bigram token 列），`search` 用 FTS5 MATCH + 标题/关键词加权，失败回退 `LIKE`。
- [ ] **Step 4: 确认通过**：`pytest tests/offline/test_library.py -v` → PASS
- [ ] **Step 5: 提交**：`git commit -am "feat: experience library with FTS5 and evidence gate"`

---

### Task 6: 元工具与经验闸门

**Files:**
- Create: `runtime/nxkit/router.py`、`runtime/nxkit/gate.py`、`runtime/nxkit/handlers/meta.py`、`runtime/nxkit/handlers/exp.py`
- Modify: `runtime/nxkit/server.py`（接入 Router）
- Test: `tests/offline/test_meta_tools.py`

**Interfaces:**
- Produces: `Router(registry, bridge, library, gate)` `.call(tool_name: str, args: dict) -> dict`；`Gate(workflow_path: Path)` `.check(tool: str, part: str) -> dict | None`、`.mark_searched(part)`、`.reset(part)`；handlers：`nx_status`、`nx_find(q|module)`、`nx_help(t)`、`nx_call(t,args)`、`exp_search(q)`、`nx_exp(action,...)`。
- 闸门：`hard` 默认，本轮未搜库且库内有匹配 `c=招式/方案` 时返回 `{"ok":False,"blocked":"no-search","have":[...],"next":...}`；`skip_search=1` 放行；按零件重置。

- [ ] **Step 1: 写失败测试**：`nx_find q=孔` 返回卡片；闸门拦住 `nx_block`；`exp_search` 后放行；`ng_call` 等价直调

```python
def test_gate_blocks_until_search(router, library_with_move):
    blocked = router.call("nx_block", {"target": "last", "x": 0, "y": 0, "z": 0})
    assert blocked["error"]["code"] == "SEARCH_REQUIRED"
    router.call("exp_search", {"q": "孔"})
    assert router.call("nx_block", {"skip_search": 1, "x": 0, "y": 0, "z": 0})["ok"] is True
```

- [ ] **Step 2: 确认失败**：`pytest tests/offline/test_meta_tools.py -v` → FAIL
- [ ] **Step 3: 实现**：`router` 按 `route.kind` 分发；`meta.py` 输出 `{t,g,s,need}` 卡片（`need` 来自 schema.required）；`exp.py` 包装 Library；`gate` 持久化 `io/workflow.json`（`part/searched/done` 三键）。
- [ ] **Step 4: 确认通过**：`pytest tests/offline/test_meta_tools.py -v` → PASS
- [ ] **Step 5: 提交**：`git commit -am "feat: meta tools and search gate"`

---

### Task 7: 预检（Py3.3 语法 + 规则）

**Files:**
- Create: `runtime/nxkit/precheck.py`、`contract/rules/py33.json`
- Test: `tests/offline/test_precheck.py`

**Interfaces:**
- Consumes: `contract/commands.json`（命令名与 arity 参考）。
- Produces: `Issue`（dataclass：`code, msg, node, next, hint_tool`）；`Precheck(contract_dir: Path)` `.check(code: str, target_ver: str = "nx2512") -> list[Issue]`；`target_ver` 由 matrix 映射语法级别（`nx10 → 3.3`，`nx2512 → 3.12`）。

- [ ] **Step 1: 写失败测试**

```python
def test_fstring_rejected_for_nx10(precheck):
    issues = precheck.check('print(f"{1}")', target_ver="nx10")
    assert [i.code for i in issues] == ["PY33_FSTRING"]

def test_missing_commit_detected(precheck):
    code = "b = work_part.Features.CreateBlockBuilder(None)\nb.SetLength(1)"
    assert any(i.code == "BUILDER_NO_COMMIT" for i in precheck.check(code))

def test_valid_code_passes(precheck):
    assert precheck.check('block = work_part.Features.CreateBlockBuilder(None)\nblock.Commit()\nblock.Destroy()') == []
```

- [ ] **Step 2: 确认失败**：`pytest tests/offline/test_precheck.py -v` → FAIL
- [ ] **Step 3: 实现**：`ast.parse(code, feature_version=(3, 3))` 不可用时回退 `(3, 4)` + 正则拦截 f-string/注解/海象；`py33.json` 存规则（正则、消息、`next`）；Builder 规则=存在 `Create*Builder` 但无 `Commit()`/`Destroy()` 配对时报 `BUILDER_NO_COMMIT`。
- [ ] **Step 4: 确认通过**：`pytest tests/offline/test_precheck.py -v` → PASS
- [ ] **Step 5: 提交**：`git commit -am "feat: precheck with py3.3 syntax and builder rules"`

---

### Task 8: NX 内桥（有头，Py3.3 语法）

**Files:**
- Create: `bridge/startup/nx_agent_kit.py`、`bridge/commands/__init__.py`、`bridge/commands/model.py`、`bridge/commands/query.py`
- Test: `tests/offline/test_bridge_commands.py`、`tests/offline/test_bridge_py33.py`

**Interfaces:**
- Produces: `handle(st, cmd: str, args: dict) -> dict`（返回 `{"data":..., "facts":...}` 或 `{"error":{...}}`，不抛异常）；`COMMANDS: dict[str, callable]`；命令实现 `create_block(st, args)`、`create_hole(st, args)`、`inspect_part(st, args)`、`measure(st, args)`；`st` 提供 `.work_part`、`.session`、`.uf`、`.undo_mark(name)`。
- 桥内每个几何命令执行前调 `st.undo_mark("nxkit:<cmd>")`。

- [ ] **Step 1: 写失败测试**：用 stub `NXOpen` 模块 + 假 `st`，断言 `create_block` 返回 `facts.bbox`；`test_bridge_py33.py` 扫描 `bridge/**/*.py`，用 `ast.parse(feature_version=(3,3))`（不可用则 `(3,4)` + 正则）断言无 f-string/注解
- [ ] **Step 2: 确认失败**：`pytest tests/offline/test_bridge_commands.py tests/offline/test_bridge_py33.py -v` → FAIL
- [ ] **Step 3: 实现**：`startup/nx_agent_kit.py` 注册 `SetTimer`（250ms）tick：读 `inbox/*.json`（按文件名排序取第一条）→ `LockUgAccess` → `handle` → 写 `outbox`，异常写 `{"error":{"code":"BRIDGE_EXCEPTION"}}`；`commands/model.py`、`query.py` 按契约实现最小命令。
- [ ] **Step 4: 确认通过**：`pytest tests/offline/test_bridge_commands.py tests/offline/test_bridge_py33.py -v` → PASS
- [ ] **Step 5: 提交**：`git commit -am "feat: in-process NX bridge (py3.3 compatible)"`

---

### Task 9: 无头执行器（run_journal + 日志判据）

**Files:**
- Create: `bridge/headless/run.py`、`runtime/nxkit/headless.py`
- Test: `tests/offline/test_headless.py`

**Interfaces:**
- Produces: `HeadlessRunner(nx_root: Path, python_exe: str | None = None)` `.run(plan: dict, timeout_s: float = 600) -> dict{"ok": bool, "markers": list[str], "log": str, "artifacts": list[str], "elapsed_ms": int}`；`bridge/headless/run.py` 读 `plan.json`、逐条执行、写 `result.json`，打印 `NXKIT_BEGIN` / `NXKIT_OK` / `NXKIT_FAIL:<code>`。
- **退出码不作为判据**；`ok` 由 `NXKIT_OK` marker 且无 `NXKIT_FAIL` 决定；超时杀进程树（Windows `taskkill /T /F`）。

- [ ] **Step 1: 写失败测试**：用 python 脚本冒充 `run_journal.exe`，分别产出成功/失败/吞异常仍 exit 0 三种日志

```python
def test_exit_code_ignored(tmp_path):
    fake = write_fake_run_journal(tmp_path)   # 打印 NXKIT_FAIL:NX_BOOL_EMPTY 后 exit 0
    res = HeadlessRunner(tmp_path).run({"steps": []})
    assert res["ok"] is False and res["markers"] == ["NXKIT_FAIL:NX_BOOL_EMPTY"]
```

- [ ] **Step 2: 确认失败**：`pytest tests/offline/test_headless.py -v` → FAIL
- [ ] **Step 3: 实现**：`headless.py` 拼命令 `run_journal.exe bridge/headless/run.py plan.json`，捕获 stdout/stderr，解析 marker，超时 `subprocess` + `taskkill`；`run.py` 校验 plan schema 后执行（失败即打印 marker 并写 `result.json`）。
- [ ] **Step 4: 确认通过**：`pytest tests/offline/test_headless.py -v` → PASS
- [ ] **Step 5: 提交**：`git commit -am "feat: headless run_journal executor with log verdicts"`

---

### Task 10: 异步 job 调度

**Files:**
- Create: `runtime/nxkit/jobs.py`
- Modify: `runtime/nxkit/handlers/meta.py`（接入 `nx_job`）
- Test: `tests/offline/test_jobs.py`

**Interfaces:**
- Produces: `Jobs(io_dir: Path)`：`.submit(kind: str, payload: dict) -> str`、`.status(job_id) -> dict`、`.cancel(job_id) -> bool`、`.recover() -> list[str]`；状态文件 `io/jobs/<job_id>.json`；有头取消写 `STOP` 文件，无头取消杀进程树；**同一会话 job 串行**（队列文件 + 锁）。

- [ ] **Step 1: 写失败测试**：两个 job 同时提交 → 第二个 `state=queued`；取消 → `state=cancelled`；超时后迟到结果被丢弃并标记 `stale`；重复 `request_id` 不重复执行

```python
def test_serial_jobs_and_cancel(jobs):
    a = jobs.submit("nx", {"cmd": "nx_block"}); b = jobs.submit("nx", {"cmd": "nx_block"})
    assert jobs.status(b)["state"] == "queued"
    jobs.cancel(b)
    assert jobs.status(b)["state"] == "cancelled"
```

- [ ] **Step 2: 确认失败**：`pytest tests/offline/test_jobs.py -v` → FAIL
- [ ] **Step 3: 实现**：文件锁（`io/jobs/.lock` 用 `os.open(O_CREAT|O_EXCL)`），状态机 `queued→running→done|failed|cancelled|stale`；`nx_job` 支持 `id`、`action=status|cancel|list`。
- [ ] **Step 4: 确认通过**：`pytest tests/offline/test_jobs.py -v` → PASS
- [ ] **Step 5: 提交**：`git commit -am "feat: async job scheduler with cancel and idempotency"`

---

### Task 11: 快照/回滚 + 计划事务

**Files:**
- Create: `runtime/nxkit/snapshot.py`、`runtime/nxkit/plan.py`
- Test: `tests/offline/test_snapshot.py`

**Interfaces:**
- Produces: `SnapshotStore(root: Path)` `.save(part_path: Path, note: str = "") -> str`、`.list() -> list[dict]`、`.restore(snapshot_id: str, to: Path | None = None) -> Path`；`run_plan(router, steps: list[dict], snapshot_store, part_path: Path) -> dict{"ok","steps","rollback"}`——任一步失败时恢复快照到计划起点。

- [ ] **Step 1: 写失败测试**：临时 `.prt` 文件 save→list→restore 内容一致；plan 第二步失败时第一步改动被回滚
- [ ] **Step 2: 确认失败**：`pytest tests/offline/test_snapshot.py -v` → FAIL
- [ ] **Step 3: 实现**：`index.jsonl` 清单（id/时间/源路径/备份路径/note）；`restore` 到不同路径时直接复制，覆盖源路径时要求目标未被 NX 打开（由调用方传入 `nx_status` 结果判断）；`plan.py` 先校验每步 `nx_check` 再执行。
- [ ] **Step 4: 确认通过**：`pytest tests/offline/test_snapshot.py -v` → PASS
- [ ] **Step 5: 提交**：`git commit -am "feat: snapshots, rollback and plan transaction"`

---

### Task 12: 全量工具面（22 域工具）

**Files:**
- Create: `contract/tools/{part,model,mold,query,exec,api,exp}.json`、`contract/commands.json`
- Create: `bridge/commands/{part,view,exec}.py`
- Test: `tests/offline/test_tool_catalog.py`

**Interfaces:**
- Produces: 注册表总数 28（6 常驻 + 22 域），每个工具含 `route/schema/matrix`；`contract/commands.json` 覆盖所有 `route.kind=nx` 的 `cmd`。
- 域清单（锚定规格 §7）：part 3、model 8、mold 1、query 3、exec 5、api 1、exp 1。

- [ ] **Step 1: 写失败测试**：`count == 28`；每个 `route.kind` 与 `cmd` 在 commands.json；每个 `schema` 通过 JSON Schema 校验；`nx_find module=model` 返回 8 个
- [ ] **Step 2: 确认失败**：`pytest tests/offline/test_tool_catalog.py -v` → FAIL
- [ ] **Step 3: 实现**：按规格 §7 表格补齐 JSON 与命令实现（`nx_part/nx_save/nx_snapshot/nx_template/nx_inspect/nx_measure/nx_screenshot/nx_check/nx_run_python/nx_plan/nx_batch/nx_assert/nx_api/nx_exp` 的 handler 或路由）；matrix 标注每工具 `verified|expected`。
- [ ] **Step 4: 确认通过**：`pytest tests/offline/test_tool_catalog.py -v` → PASS
- [ ] **Step 5: 提交**：`git commit -am "feat: full v1 tool surface (6+22) with command parity"`

---

### Task 13: 模板 pack（nx_template + 首批模板）

**Files:**
- Create: `runtime/nxkit/templates.py`、`contract/templates/T001.json`（模架）、`contract/templates/T002.json`（圆形镶件）
- Test: `tests/offline/test_templates.py`

**Interfaces:**
- Produces: `TemplateStore(contract_dir: Path)` `.list() -> list[dict]`、`.apply(name: str, params: dict) -> dict{"steps": list[dict], "asserts": list[dict]}`；参数按 `params` schema 校验，缺参/越界抛 `TemplateParamError`。

- [ ] **Step 1: 写失败测试**：`list` 返回 2 个；`apply("圆形镶件", {"d": 60})` 返回 steps；`apply(..., {"d": 5})` 抛参数越界
- [ ] **Step 2: 确认失败**：`pytest tests/offline/test_templates.py -v` → FAIL
- [ ] **Step 3: 实现**：模板 JSON 含 `id/name/params/steps/asserts/matrix`；`apply` 做参数替换（`{{d}}`）并返回步骤；`nx_template` handler 串 `run_plan`。
- [ ] **Step 4: 确认通过**：`pytest tests/offline/test_templates.py -v` → PASS
- [ ] **Step 5: 提交**：`git commit -am "feat: data-driven mold templates"`

---

### Task 14: NX API 知识库（XML/pyi → FTS5）

**Files:**
- Create: `runtime/nxkit/kb/build.py`、`runtime/nxkit/kb/search.py`
- Create: `tests/offline/fixtures/NXOpen.xml`、`tests/offline/fixtures/pythonStubs/NXOpen/Features.pyi`
- Test: `tests/offline/test_kb.py`

**Interfaces:**
- Produces: `Kb.build(nx_root: Path, db_path: Path) -> dict{"members": int, "indexed_py": int}`；`Kb(db_path).search(q: str, src: str = "all", k: int = 8) -> list[dict]`，字段 `name/sig/py/nxv/lic/doc`；`lic` 从 XML `License requirements` 提取。

- [ ] **Step 1: 写失败测试**：fixture 建索引后 `search("ExtrudeBuilder")` 命中且 `py` 精确；`search("cam_base")` 返回 `lic="CAM BASE"`；`src=pyi` 只查 stubs
- [ ] **Step 2: 确认失败**：`pytest tests/offline/test_kb.py -v` → FAIL
- [ ] **Step 3: 实现**：`build.py` 解析 `NXOpen*.xml`（`System.Xml` 文档结构 → member 名/签名/lic/nxv）与 `.pyi`（AST/正则提取 class/func 签名），写 FTS5 `api_fts`；`search` 先精确名后 FTS，`src` 过滤来源。
- [ ] **Step 4: 确认通过**：`pytest tests/offline/test_kb.py -v` → PASS
- [ ] **Step 5: 提交**：`git commit -am "feat: local NXOpen API knowledge base"`

---

### Task 15: Skill 包（3 个 SKILL.md）

**Files:**
- Create: `skills/nx-modify/SKILL.md`、`skills/nx-modify/references/{locate,modify,verify}.md`
- Create: `skills/nx-mold-template/SKILL.md`、`skills/nx-mold-template/templates/step-prompts.md`
- Create: `skills/nx-plugin-verify/SKILL.md`、`skills/nx-plugin-verify/references/pitfalls.md`
- Test: `tests/offline/test_skills.py`

**Interfaces:**
- Produces: 每个 `SKILL.md` 必含三节：`## 何时用`、`## 步骤`、`## 硬规则`；正文引用的工具名必须存在于 Registry；引用的 references 文件存在。

- [ ] **Step 1: 写失败测试**：遍历 `skills/*/SKILL.md` 断言三节存在、工具名可解析、引用文件存在
- [ ] **Step 2: 确认失败**：`pytest tests/offline/test_skills.py -v` → FAIL
- [ ] **Step 3: 实现**：按规格 §10 写三个技能（模板句 + 硬规则：不猜 API、量了才说完成、写前快照、数据只写工作区）；references 放细节。
- [ ] **Step 4: 确认通过**：`pytest tests/offline/test_skills.py -v` → PASS
- [ ] **Step 5: 提交**：`git commit -am "docs(skills): modify, mold-template and plugin-verify playbooks"`

---

### Task 16: 部署脚本与 probe 矩阵

**Files:**
- Create: `deploy/probe.ps1`、`deploy/install.ps1`、`deploy/sync.ps1`、`deploy/uninstall.ps1`
- Test: `tests/offline/test_deploy.py`

**Interfaces:**
- Produces: `probe.ps1` 扫描 `UGII_BASE_DIR`/注册表/Program Files → 写 `contract/matrix/<ver>.json`；`install.ps1` 复制 `bridge/startup/nx_agent_kit.py` 到目标 `startup`，向 `custom_dirs.dat` **追加**（绝不覆盖已有行），写 `mcp_config.json` 片段；`sync.ps1 -Check` 用 SHA256 比对包与运行时副本。

- [ ] **Step 1: 写失败测试**：临时目录模拟 NX 根 → 断言 matrix 字段；预置含他人条目的 `custom_dirs.dat` → 断言只追加不覆盖；`sync.ps1 -Check` 对差异返回非零
- [ ] **Step 2: 确认失败**：`pytest tests/offline/test_deploy.py -v` → FAIL
- [ ] **Step 3: 实现**：PowerShell 5.1 兼容（UTF-8 BOM、无 7.x 语法）；probe 用 `Test-Path`/`Get-ItemProperty` 探测；install 输出 `RESULT: OK`。
- [ ] **Step 4: 确认通过**：`pytest tests/offline/test_deploy.py -v` → PASS
- [ ] **Step 5: 提交**：`git commit -am "feat(deploy): probe, install, sync and uninstall scripts"`

---

### Task 17: 评测 harness + 10 个金标准任务

**Files:**
- Create: `evals/run.py`、`evals/tasks/t01..t10.json`
- Test: `tests/offline/test_evals.py`

**Interfaces:**
- Produces: `evals/run.py` 接受 `--bridge fake|live --nx 10|2512`，输出 `{"success_rate","avg_steps","avg_seconds","rollback_rate","failures":[...]}`；任务 JSON 字段 `id/goal/steps/asserts/expect_ok`。

- [ ] **Step 1: 写失败测试**：10 个任务 schema 合法；fake bridge 下跑完得到指标 dict 且含 5 个键
- [ ] **Step 2: 确认失败**：`pytest tests/offline/test_evals.py -v` → FAIL
- [ ] **Step 3: 实现**：任务覆盖改件 5（改尺寸/打孔/倒角/移动面/删特征）、模板 3（镶件/模架/流道）、插件 2（生成+验证）；`run.py` 复用 Router，失败逐步记录。
- [ ] **Step 4: 确认通过**：`pytest tests/offline/test_evals.py -v` → PASS
- [ ] **Step 5: 提交**：`git commit -am "test(evals): 10 golden tasks for mold workflows"`

---

### Task 18: 真机验收（live + headless）

**Files:**
- Create: `tests/live.py`、`tests/headless.py`、`docs/acceptance.md`
- Test: 本任务以验收清单为主，无新单测；`pytest -m live` 在无 NX 时整体 skip。

**Interfaces:**
- Consumes: 前面全部任务。
- Produces: `tests/live.py`（起 host + 真 NX，走 15 项冒烟）、`tests/headless.py`（`run_journal` 10 个 journal）、`docs/acceptance.md`（勾选清单）。

- [ ] **Step 1: 写验收脚本**：`live.py` 用例 = 建块→布尔→测量→打孔→倒角→移动面→快照→回滚→截图→存盘；每步断言信封 `ok` 与 `verify`；无 NX 时 `pytest.skip`。
- [ ] **Step 2: 跑 offline 回归**：`pytest tests/offline -q` → 全绿
- [ ] **Step 3: 跑 live（NX 2512）**：`python tests/live.py` → 15 项全过
- [ ] **Step 4: 跑 live（NX 10）**：切换 `NXKIT_MATRIX=nx10` 重跑 → 通过项与矩阵 `verified` 一致
- [ ] **Step 5: 跑 headless**：`python tests/headless.py` → 10 个 journal 判据通过
- [ ] **Step 6: 跑评测**：`python evals/run.py --bridge live --nx 2512` → 成功率 ≥70%、回滚率 100%
- [ ] **Step 7: 提交**：`git commit -am "test: live and headless acceptance"`

---

## Self-Review

**1. 规格覆盖**：§5 架构→T1–T3、T8–T9；§6 五契约→T1/T4/T5/T12/T13；§7 工具面→T12；§8 数据流→T6/T11/T13；§9 验证事务→T4/T7/T10/T11；§10 Skill→T15；§11 测试评测→T17/T18；§12 里程碑→任务表；§14 决策→Global Constraints。无缺口。

**2. 步骤粒度**：每步一个动作 + 可检查结果；无"处理边界情况""补充校验"类空话；代码块只放测试与关键签名。

**3. 类型一致性**：`Registry.list`（T1）与 T2 server、T6 router 一致；`BridgeClient.submit(pid, cmd, args, timeout_s)`（T3）与 T10 串行队列一致；`make_error(code, stage, msg, next, hint_tool)`（T4）与 T6 闸门、T7 precheck 一致。

**4. Review Focus 落测试**：中文路径→T3；重启/陈旧 outbox→T3；并发→T10；超时迟到→T10；expose 切换→T1/T6；py3.3 漂移→T8。

**5. 比例**：计划含 18 个任务的测试断言与接口签名，无大段实现代码；长度来自任务数而非转录程序。
