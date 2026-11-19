# NX10 几何信息悬浮窗 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 NX10 里点选体/面/边即弹出一个跟随光标的浮窗，就地显示类型、tag、颜色、包围盒、面积/周长/长度，并异步补齐体积与距离。

**Architecture:** 进程内 C++ 插件只做「捕获 + 查询 + 写快照」，进程外 Win32 浮窗只做「读快照 + 绘制」，两侧通过一块 seqlock 保护的共享内存和命名事件通信；NX 侧不知道浮窗存在，浮窗不加载任何 NXOpen 库。

**Tech Stack:** C++14 / MSVC v142（VS2019）、NXOpen C++（NX10）、UFUN C API、Win32 分层窗口 + GDI+、MSBuild。

**Spec:** `docs/superpowers/specs/2026-11-19-nx10-geometry-hover-overlay-design.md`

## Global Constraints

- 目标平台：Siemens NX 10.0（`D:\Program Files\Siemens\NX 10.0`），32/64 位跟随 NX 安装（本机为 64 位）。
- 工具链：`PlatformToolset=v142`（VS2019，`C:\Program Files (x86)\Microsoft Visual Studio\2019\Community`），**C++14**（`/std:c++14`），`/utf-8`。
- 构建入口唯一：根目录 `build.cmd`（遍历 `*.vcxproj` → `msbuild` → 跑测试 exe → 合并退出码）。不维护 `.sln`。
- 头/库路径：`$(NX10_DIR)\UGOPEN` 与 `$(NX10_DIR)\UGOPEN\NXOpen`；链接 `libnxopencpp.lib`、`libufun.lib`（未解析符号时按报错补 `libnxopencpp_<domain>.lib`）。
- 不引入任何第三方库；测试用自带 `Check.h` 宏 + 控制台 exe，不用 gtest。
- 协议常量：`Magic = 0x4E584745`、`ProtocolVersion = 1`、`SlotSize = 512`、共享内存 `Local\NXGEO_<pid>`、事件 `Local\NXGEO_EVT_<pid>`。
- NX 侧**只读**：不得改对象颜色/高亮/图层，不得建特征（Measure 只用瞬时值重载，禁止 `CreateFeature`）。
- **跨 NXOpen 边界只传指针、引用、基本类型和 `NXString`**；禁止传/收 `std::string`、`std::vector` 的按值返回，避免 MSVC 版本间 STL ABI 差异。
- 所有面向用户的字符串内部用 UTF-8 `std::string`，只在 GDI+ 绘制前转 UTF-16。

## Review Focus

1. **用户点空白处取消选择** → `GetNumSelectedObjects()==0`，浮窗必须立刻隐藏，不能残留上一个对象。
2. **无工作部件**（NX 刚启动/没开图）→ 查询返回空快照并写 note，不抛异常、不弹 NX 报错框。
3. **查询途中对象被删除**（tag 失效）→ 捕获 `NXException`、该字段留空、note 写「对象已删除」，进程不崩。
4. **旧浮窗连新插件**（协议版本不匹配）→ 浮窗显示版本提示并停止解读数据。
5. **150% 缩放或双屏** → 浮窗定位不偏移、不出屏、不跳到别的显示器。

## 文件结构

```
build.cmd                              # 唯一构建入口
.gitignore
tools/probe-selection.py               # 已存在：判定 SelectionManager 里装的是什么
src/NxGeo.Core/                        # 静态库，不引用 NXOpen，可离线单测
  NxGeoConst.h
  ObjKind.h  ObjRef.h
  FieldFlags.h  InfoSnapshot.h
  SnapshotCodec.h/.cpp                 # InfoSnapshot <-> 512B
  SnapshotChannel.h/.cpp               # seqlock 共享内存
  TypeNameMap.h/.cpp
  ICaptureSource.h
  INxQuery.h
  QueryScheduler.h/.cpp
src/NxGeo.NxPlugin/                    # DLL，NXOpen C++
  NxQueryAdapter.h/.cpp
  SelectionCaptureSource.h/.cpp
  PluginEntry.h/.cpp
  ufusr.cpp                            # ufsta/ufusr 入口
src/NxGeo.Overlay/                     # EXE，Win32 + GDI+
  main.cpp
  OverlayPlacement.h/.cpp              # 纯函数，可测
  InfoViewModel.h/.cpp                 # 字段级 dirty，可测
  OverlayWindow.h/.cpp
  Native.h
tests/NxGeo.Tests/                     # 控制台测试宿主
  Check.h  main.cpp
install/
  install.ps1  uninstall.ps1  nxgeo.men
docs/superpowers/plans/2026-11-19-nx10-acceptance.md
```

---

### Task 0: 仓库与构建骨架

**Files:**
- Create: `.gitignore`, `build.cmd`
- Create: `src/NxGeo.Core/NxGeoConst.h`, `src/NxGeo.Core/NxGeo.Core.vcxproj`（StaticLibrary）
- Create: `tests/NxGeo.Tests/Check.h`, `tests/NxGeo.Tests/main.cpp`, `tests/NxGeo.Tests/NxGeo.Tests.vcxproj`（ConsoleApplication）

**Interfaces:**
- Consumes: 无
- Produces: `NxGeoConst::Magic`、`NxGeoConst::ProtocolVersion`、`NxGeoConst::SlotSize`、`NxGeoConst::HeaderSize`、`NxGeoConst::SharedMemoryName(int)`、`NxGeoConst::EventName(int)`；`CHECK(cond)` / `CHECK_EQ(a,b)` 宏；`build.cmd` 成功返回 0

- [ ] **Step 1: 初始化仓库与目录**

```powershell
git init
New-Item -ItemType Directory -Force src/NxGeo.Core, src/NxGeo.NxPlugin, src/NxGeo.Overlay, tests/NxGeo.Tests, install | Out-Null
```

`.gitignore`：`bin/`、`obj/`、`x64/`、`.vs/`、`*.user`

- [ ] **Step 2: 写失败的测试**

`tests/NxGeo.Tests/Check.h`

```cpp
#pragma once
#include <iostream>
#include <string>
namespace Check {
    inline int& Failures() { static int n = 0; return n; }
    inline void Expect(bool ok, const char* expr, const char* file, int line) {
        if (!ok) { ++Failures(); std::cerr << "[FAIL] " << file << ":" << line << "  " << expr << "\n"; }
    }
    template <typename A, typename B>
    void ExpectEq(const A& a, const B& b, const char* ea, const char* eb, const char* file, int line) {
        if (!(a == b)) { ++Failures(); std::cerr << "[FAIL] " << file << ":" << line
            << "  " << ea << " == " << eb << "  (got " << a << " vs " << b << ")\n"; }
    }
}
#define CHECK(x)        ::Check::Expect((x), #x, __FILE__, __LINE__)
#define CHECK_EQ(a, b)  ::Check::ExpectEq((a), (b), #a, #b, __FILE__, __LINE__)
```

`tests/NxGeo.Tests/main.cpp`

```cpp
#include "Check.h"
#include "NxGeoConst.h"
int main() {
    CHECK_EQ(NxGeoConst::SlotSize, 512);
    CHECK_EQ(std::string(NxGeoConst::SharedMemoryName(42)), std::string("Local\\NXGEO_42"));
    std::cout << (Check::Failures() == 0 ? "ALL PASS" : "FAILED") << "\n";
    return Check::Failures() == 0 ? 0 : 1;
}
```

- [ ] **Step 3: 跑一次确认失败**

Run: `build.cmd`
Expected: 编译失败（`NxGeoConst.h` 不存在），退出码非 0

- [ ] **Step 4: 实现最小骨架**

`src/NxGeo.Core/NxGeoConst.h`

```cpp
#pragma once
#include <string>
namespace NxGeoConst {
    constexpr unsigned Magic = 0x4E584745u;   // "NXGE"
    constexpr unsigned short ProtocolVersion = 1;
    constexpr int SlotSize = 512;
    constexpr int HeaderSize = 32;
    inline std::string SharedMemoryName(int pid) { return "Local\\NXGEO_" + std::to_string(pid); }
    inline std::string EventName(int pid)        { return "Local\\NXGEO_EVT_" + std::to_string(pid); }
}
```

两个 `.vcxproj` 的公共设置：`PlatformToolset=v142`、`LanguageStandard=stdcpp14`、`AdditionalOptions=/utf-8 %(AdditionalOptions)`、`IncludePath` 含 `..\NxGeo.Core`。

`build.cmd`：遍历 `src` 与 `tests` 下所有 `*.vcxproj`，逐个
`msbuild <proj> /p:Configuration=Debug /p:Platform=x64 /p:PlatformToolset=v142 /v:m /nologo`，
全部成功后运行 `tests\NxGeo.Tests\x64\Debug\NxGeo.Tests.exe`，把编译与测试退出码合并返回。

- [ ] **Step 5: 跑测试确认通过**

Run: `build.cmd`
Expected: `ALL PASS`，退出码 0

- [ ] **Step 6: 提交**

```bash
git add -A && git commit -m "chore: repo skeleton, v142 build entry and assert test host"
```

---

### Task 1: 协议快照与编解码

**Files:**
- Create: `src/NxGeo.Core/ObjKind.h`, `src/NxGeo.Core/ObjRef.h`, `src/NxGeo.Core/FieldFlags.h`, `src/NxGeo.Core/InfoSnapshot.h`, `src/NxGeo.Core/SnapshotCodec.h`, `src/NxGeo.Core/SnapshotCodec.cpp`
- Test: `tests/NxGeo.Tests/main.cpp`

**Interfaces:**
- Consumes: `NxGeoConst`
- Produces:
  - `enum class ObjKind : unsigned char { None=0, Body=1, Face=2, Edge=3, Curve=4, Other=5 }`
  - `enum FieldFlags : unsigned { Field_None=0, Field_L0=1, Field_BBox=2, Field_Area=4, Field_Length=8, Field_Volume=16, Field_Distance=32 }`
  - `struct InfoSnapshot`：`magic, version, kind, gen, tag, fields, typeName[32], color, layer, bboxMin[3], bboxMax[3], length, area, perimeter, volume, distance, distanceTo[48], note[64]`（全部定长，POD）
  - `void EncodeSnapshot(const InfoSnapshot&, unsigned char* out512)`
  - `InfoSnapshot DecodeSnapshot(const unsigned char* in512)`（魔数/版本不符抛 `std::runtime_error`）
  - `inline bool HasField(unsigned fields, unsigned f) { return (fields & f) != 0; }`

- [ ] **Step 1: 写失败的测试**

```cpp
InfoSnapshot s{};                       // 全 0
s.kind = (unsigned short)ObjKind::Face;
s.gen = 7; s.tag = 0x1234; s.fields = Field_L0 | Field_Area;
std::strncpy(s.typeName, "圆柱面", sizeof s.typeName - 1);
s.color = 186; s.layer = 1; s.area = 12.5;
unsigned char buf[NxGeoConst::SlotSize];
EncodeSnapshot(s, buf);
InfoSnapshot b = DecodeSnapshot(buf);
CHECK_EQ(b.gen, 7ull);
CHECK_EQ(b.tag, 0x1234u);
CHECK_EQ(std::string(b.typeName), std::string("圆柱面"));
CHECK_EQ(b.area, 12.5);
CHECK(HasField(b.fields, Field_Area));
CHECK(!HasField(b.fields, Field_Volume));
```

- [ ] **Step 2: 跑测试确认失败**

Run: `build.cmd`
Expected: 编译失败（`InfoSnapshot` 未定义）

- [ ] **Step 3: 实现**

`EncodeSnapshot` 先 `memset(out512, 0, SlotSize)`，再用 `memcpy` 按固定偏移写字段（`offsetof` 断言在 Task 1 的测试里不做，改为直接 memcpy 整个结构体再补 0——结构体本身小于 512）。`DecodeSnapshot` 先 `memcpy` 出结构体，校验 `magic == NxGeoConst::Magic && version == ProtocolVersion`，不符抛 `std::runtime_error("protocol mismatch")`。结构体加 `static_assert(sizeof(InfoSnapshot) <= NxGeoConst::SlotSize, "snapshot too large")`。

- [ ] **Step 4: 跑测试确认通过**

Run: `build.cmd`
Expected: `ALL PASS`

- [ ] **Step 5: 提交**

```bash
git add -A && git commit -m "feat(core): fixed-size snapshot struct and codec"
```

---

### Task 2: 类型名映射

**Files:**
- Create: `src/NxGeo.Core/TypeNameMap.h`, `src/NxGeo.Core/TypeNameMap.cpp`
- Test: `tests/NxGeo.Tests/main.cpp`

**Interfaces:**
- Consumes: `ObjKind`
- Produces: `ObjKind KindOf(int type, int subtype)`；`std::string NameOf(int type, int subtype)`

- [ ] **Step 1: 写失败的测试**

```cpp
using namespace TypeNameMap;
CHECK(KindOf(70, 0) == ObjKind::Body);
CHECK(KindOf(70, 1) == ObjKind::Body);   // 片体
CHECK(KindOf(70, 2) == ObjKind::Face);
CHECK(KindOf(70, 3) == ObjKind::Edge);
CHECK(KindOf(999, 0) == ObjKind::Other);
CHECK_EQ(NameOf(70, 3), std::string("边"));
CHECK_EQ(NameOf(999, 7), std::string("未知(999/7)"));
```

- [ ] **Step 2: 跑测试确认失败**

Run: `build.cmd`
Expected: 编译失败（`TypeNameMap` 未定义）

- [ ] **Step 3: 实现**

NX 常量（来自 `uf_object_types.h`）：`UF_solid_type = 70`，`body_subtype = 0`（实体）、`1`（片体）、`2`（面）、`3`（边）。用 `std::map<std::pair<int,int>, std::string>` 查表，键不存在时返回 `"未知(type/subtype)"` 并用 `std::to_string` 拼。

- [ ] **Step 4: 跑测试确认通过**

Run: `build.cmd`
Expected: `ALL PASS`

- [ ] **Step 5: 提交**

```bash
git add -A && git commit -m "feat(core): NX type/subtype mapping"
```

---

### Task 3: seqlock 共享内存通道

**Files:**
- Create: `src/NxGeo.Core/SnapshotChannel.h`, `src/NxGeo.Core/SnapshotChannel.cpp`
- Test: `tests/NxGeo.Tests/main.cpp`

**Interfaces:**
- Consumes: `NxGeoConst`、`SnapshotCodec`
- Produces:
  - `class SnapshotChannel`（不可拷贝）
  - `SnapshotChannel(const std::string& name, bool createNew)`（内部 `MultiByteToWideChar` 转宽字符）
  - `void SetNxHwnd(long long hwnd)`、`void SetNxPid(int pid)`
  - `unsigned long long Write(const InfoSnapshot&)` → 返回本次 gen
  - `bool TryRead(InfoSnapshot& out) const`、`bool VersionMismatch() const`

- [ ] **Step 1: 写失败的测试**

```cpp
using namespace std::chrono;
std::string name = NxGeoConst::SharedMemoryName(9000 + GetCurrentProcessId() % 1000);
SnapshotChannel w(name, true), r(name, false);
w.SetNxHwnd(0x1234); w.SetNxPid(42);
InfoSnapshot a{}; a.tag = 11;
InfoSnapshot b{}; b.tag = 22;
unsigned long long g1 = w.Write(a);
unsigned long long g2 = w.Write(b);
CHECK_EQ(g2, g1 + 1);
InfoSnapshot got{};
CHECK(r.TryRead(got));
CHECK_EQ(got.tag, 22u);
CHECK_EQ(got.gen, g2);
CHECK_EQ(r.NxHwnd(), 0x1234ll);
CHECK(!r.VersionMismatch());
```

- [ ] **Step 2: 跑测试确认失败**

Run: `build.cmd`
Expected: 编译失败（`SnapshotChannel` 未定义）

- [ ] **Step 3: 实现**

头 32B：`magic(4) version(2) pad(2) activeGen(8) nxHwnd(8) nxPid(4) pad2(4)`；随后两个 512B 槽，总 1056B。
写：`slot = int(gen & 1)`，`memcpy` 到槽，`MemoryBarrier()`，再写 `activeGen`。
读：读 `activeGen`/`version`；版本不符 → `VersionMismatch()=true`、返回 false；否则拷贝槽并重读 `activeGen` 校验，最多重试 3 次。
用 `CreateFileMappingW(INVALID_HANDLE_VALUE, ...)` + `MapViewOfFile`；`createNew=false` 时 `OpenFileMappingW` 失败返回未初始化状态（`TryRead` 返回 false）。

- [ ] **Step 4: 跑测试确认通过**

Run: `build.cmd`
Expected: `ALL PASS`

- [ ] **Step 5: 提交**

```bash
git add -A && git commit -m "feat(core): seqlock snapshot channel"
```

---

### Task 4: 捕获抽象与调度

**Files:**
- Create: `src/NxGeo.Core/ICaptureSource.h`, `src/NxGeo.Core/INxQuery.h`, `src/NxGeo.Core/QueryScheduler.h`, `src/NxGeo.Core/QueryScheduler.cpp`
- Test: `tests/NxGeo.Tests/main.cpp`

**Interfaces:**
- Consumes: `InfoSnapshot`、`SnapshotChannel`
- Produces:
  - `struct ICaptureSource { virtual void Start(std::function<void(const ObjRef&)> sink) = 0; virtual void Stop() = 0; virtual ~ICaptureSource() {} };`
  - `struct INxQuery { virtual InfoSnapshot QueryL0(const ObjRef&) = 0; virtual void FillL1(const ObjRef&, InfoSnapshot&) = 0; virtual void FillL2(const ObjRef&, InfoSnapshot&) = 0; virtual ~INxQuery() {} };`
  - `class QueryScheduler { QueryScheduler(ICaptureSource*, INxQuery*, SnapshotChannel*); void Start(); void Stop(); unsigned long long CurrentGen() const; };`

- [ ] **Step 1: 写失败的测试**

```cpp
struct FakeSource : ICaptureSource {
    std::function<void(const ObjRef&)> sink;
    void Start(std::function<void(const ObjRef&)> s) override { sink = s; }
    void Stop() override {}
    void Emit(unsigned tag) { ObjRef r{}; r.tag = tag; r.kind = ObjKind::Face; sink(r); }
};
struct FakeQuery : INxQuery {
    int l0 = 0, l1 = 0, l2 = 0;
    InfoSnapshot QueryL0(const ObjRef& r) override { ++l0; InfoSnapshot s{}; s.tag = r.tag; return s; }
    void FillL1(const ObjRef&, InfoSnapshot& s) override { ++l1; s.area = 1.0; s.fields |= Field_Area; }
    void FillL2(const ObjRef&, InfoSnapshot&) override { ++l2; }
};
// 同一 tag 连续 3 次 -> l0 == 1（去重）
// tag == 0 -> 写入的快照 kind == ObjKind::None
// gen 过期 -> L2 结果不覆盖新快照
```

- [ ] **Step 2: 跑测试确认失败**

Run: `build.cmd`
Expected: 编译失败（`QueryScheduler` 未定义）

- [ ] **Step 3: 实现**

`Start()` 把 Sink 注册进 source；Sink 内 `gen = ++_gen`（`std::atomic<unsigned long long>`），`tag==0 || tag==_lastTag` 时只更新快照的 `gen` 不重查；否则 `Write(L0)` → `FillL1` 写第二帧 → `std::thread` 跑 `FillL2`，完成时 `if (gen == _gen) Write(result)` 否则丢弃。所有 `Write` 用 `std::mutex` 串行化。`Stop()` 置 `_stopping` 并 join 后台线程。

- [ ] **Step 4: 跑测试确认通过**

Run: `build.cmd`
Expected: `ALL PASS`

- [ ] **Step 5: 提交**

```bash
git add -A && git commit -m "feat(core): capture abstraction and latest-wins scheduler"
```

---

### Task 5: NX 查询适配器（真机）

**Files:**
- Create: `src/NxGeo.NxPlugin/NxQueryAdapter.h`, `src/NxGeo.NxPlugin/NxQueryAdapter.cpp`, `src/NxGeo.NxPlugin/NxGeo.NxPlugin.vcxproj`（DynamicLibrary，`$(NX10_DIR)\UGOPEN` 头/库，链接 `libnxopencpp.lib;libufun.lib`）
- Create: `tools/probe-nx.ps1`
- Test: 真机探针（NX 边界必须实测）

**Interfaces:**
- Consumes: `INxQuery`、`InfoSnapshot`、`TypeNameMap`
- Produces: `class NxQueryAdapter : public INxQuery`，`static bool SelfTest(std::string& report)`

- [ ] **Step 1: 真机探针（先验工具链能加载）**

在 NX10 里加载本 DLL 的一个最小导出（`ufusr` 里只打印 `NXOpen::Session::GetSession()->Parts().Work()` 是否为空），确认：
(a) v142 编出的 DLL 能被 NX10 加载；(b) NXOpen 调用不崩。
Expected: 输出 `work=1` 且 NX 不崩。若崩 → 停止本任务，问题在 STL/ABI，改为只用 UFUN C API + 指针，不碰 NXOpen 类。

- [ ] **Step 2: 实现 L0**

```cpp
NXOpen::TaggedObject* obj = ...;   // 由 tag 恢复：NXOpen::TaggedObject 无公开 tag→指针构造，
                                   // 改用 UFUN：UF_OBJ_ask_type_and_subtype(tag,...) 与
                                   // UF_OBJ_ask_display_properties(tag,&props)，以及
                                   // UF_MODL_ask_bounding_box(tag, min, max)
```
类型名走 `TypeNameMap::NameOf`；颜色/图层取 `UF_OBJ_disp_props_t`；所有 UFUN 返回码非 0 时写 `note`，不抛。

- [ ] **Step 3: 实现 L1**

面：`workPart->MeasureManager()->NewFaceProperties(areaUnit, lengthUnit, 0.99, faces)` → `MeasureFaces::Area()`、`Perimeter()`。
边：`NewLength(lengthUnit, edges)` → `SingleMeasure::Value()`。
单位：`workPart->UnitCollection()->FindObject("SquareMillimeter")` / `"Millimeter"`，找不到用 `FindObject("Millimeter")` 兜底。
**注意**：`NewFaceProperties`/`NewLength` 的重载接受 `std::vector<...*>`，跨边界只传元素指针、不收回容器。

- [ ] **Step 4: 实现 L2**

体积：`UF_MODL_ask_mass_props_3d(&tag, 1, 1 /*solid*/, 4 /*kg-m*/, 0.0, 1, acc, mp)`，`acc[0]=0.99`，取 `mp[1]`（volume）、`mp[0]`（surface area）。
距离：`MeasureManager::NewDistance(lengthUnit, MeasureManager::MeasureTypeMinimum, prevObj, curObj)` → `MeasureDistance::Value()`；`prevObj` 由浮窗/插件记住的「上次选中对象」提供。

- [ ] **Step 5: 真机验证**

Run: NX10 打开带孔方块 → 点选面 → 调 `SelfTest`
Expected: `face: area>0 perimeter>0 bbox ok`，日志无 `NXException`

- [ ] **Step 6: 提交**

```bash
git add -A && git commit -m "feat(plugin): NXOpen/UFUN query adapter L0-L2"
```

---

### Task 6: 浮窗 ViewModel（行与格式）

**Files:**
- Create: `src/NxGeo.Overlay/InfoViewModel.h`, `src/NxGeo.Overlay/InfoViewModel.cpp`
- Test: `tests/NxGeo.Tests/main.cpp`

**Interfaces:**
- Consumes: `InfoSnapshot`、`FieldFlags`
- Produces:
  - `struct InfoRow { std::string title; std::string value; bool visible; };`
  - `class InfoViewModel { public: void Apply(const InfoSnapshot&); const std::vector<InfoRow>& Rows() const; int Revision() const; };`
  - 行顺序固定：类型/tag/颜色/图层/包围盒/面积/周长/长度/体积/距离/备注；只显示 `fields` 已置位的行（备注非空才显示）。

- [ ] **Step 1: 写失败的测试**

```cpp
InfoViewModel vm;
InfoSnapshot s{}; s.kind = (unsigned short)ObjKind::Face; s.tag = 0x2A;
std::strncpy(s.typeName, "面", sizeof s.typeName - 1); s.fields = Field_L0;
vm.Apply(s);
int visible = 0; for (auto& r : vm.Rows()) if (r.visible) ++visible;
CHECK_EQ(visible, 3);                                  // 类型 / tag / 颜色 三行
s.fields |= Field_Area; s.area = 12.5;
vm.Apply(s);
auto& areaRow = vm.Rows()[5];
CHECK(areaRow.visible);
CHECK_EQ(areaRow.value, std::string("12.500"));
int rev = vm.Revision();
vm.Apply(s);                                            // 完全相同
CHECK_EQ(vm.Revision(), rev);
```

- [ ] **Step 2: 跑测试确认失败**

Run: `build.cmd`
Expected: 编译失败（`InfoViewModel` 未定义）

- [ ] **Step 3: 实现**

`Apply` 按字段生成目标行集合，与现有行逐行比较 `(title,value,visible)`，有变化才替换并 `++_revision`。数值统一 3 位小数（`snprintf("%.3f")`）；tag 显示 `0x%08X`；颜色显示 `索引 (R,G,B)`。
可见规则写死并测试：`Field_L0` → 类型/tag/颜色 三行；`Field_BBox` → 包围盒；`Field_Area` → 面积 + 周长；`Field_Length` → 长度；`Field_Volume` → 体积；`Field_Distance` → 距离；`note` 非空 → 备注。

- [ ] **Step 4: 跑测试确认通过**

Run: `build.cmd`
Expected: `ALL PASS`

- [ ] **Step 5: 提交**

```bash
git add -A && git commit -m "feat(overlay): row view model with field-level dirty"
```

---

### Task 7: 分层窗口（定位 / 点穿 / 热键 / 绘制）

**Files:**
- Create: `src/NxGeo.Overlay/OverlayPlacement.h`, `src/NxGeo.Overlay/OverlayPlacement.cpp`, `src/NxGeo.Overlay/Native.h`, `src/NxGeo.Overlay/OverlayWindow.h`, `src/NxGeo.Overlay/OverlayWindow.cpp`, `src/NxGeo.Overlay/main.cpp`, `src/NxGeo.Overlay/NxGeo.Overlay.vcxproj`（Application，链接 `gdiplus.lib;user32.lib;gdi32.lib`）
- Test: `tests/NxGeo.Tests/main.cpp`（只测纯函数）

**Interfaces:**
- Consumes: `InfoViewModel`、`SnapshotChannel`
- Produces:
  - `struct Placement { int x; int y; };`
  - `Placement ComputePlacement(int cx, int cy, int w, int h, int left, int top, int right, int bottom);`
  - `class OverlayWindow { bool Create(HINSTANCE); int Run(); void UpdateSnapshot(const InfoSnapshot&); };`

- [ ] **Step 1: 写失败的测试**

```cpp
Placement a = ComputePlacement(500, 500, 300, 200, 0, 0, 1920, 1080);
CHECK_EQ(a.x, 516);
CHECK_EQ(a.y, 520);
Placement p = ComputePlacement(1900, 1070, 300, 200, 0, 0, 1920, 1080);
CHECK(p.x + 300 <= 1920);
CHECK(p.y + 200 <= 1080);
```

- [ ] **Step 2: 跑测试确认失败**

Run: `build.cmd`
Expected: 编译失败（`ComputePlacement` 未定义）

- [ ] **Step 3: 实现定位纯函数**

先试 `(cx+16, cy+20)`；越界则 `(cx-w-16, cy-h-20)`；仍越界则夹到 `[left+4, right-w-4] × [top+4, bottom-h-4]`。

- [ ] **Step 4: 实现窗口**

`WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW`，`WS_POPUP`。
绘制：`CreateDIBSection` 32bpp 预乘 alpha → `GDI+ Graphics` 画半透明底板 + 文本（`TextRenderingHintAntiAliasGridFit`）→ `UpdateLayeredWindow`。
跟随：`GetCursorPos` + `GetWindowRect(nxHwnd)` → `ComputePlacement` → `SetWindowPos(..., SWP_NOACTIVATE|SWP_NOZORDER)`。
循环：`WaitForSingleObject(event, 16)`，超时也重绘一次（光标可能移动）；`Alt+\`` 用 `RegisterHotKey` 切换点穿。
无工作部件/空快照：`ShowWindow(SW_HIDE)`。

- [ ] **Step 5: 跑测试确认通过**

Run: `build.cmd`
Expected: `ALL PASS`

- [ ] **Step 6: 手工验收**

用假数据写入器往共享内存灌快照，移动鼠标：浮窗跟手、不出 NX 窗口、点穿时能点到后面的模型、150% 缩放下不飘
Expected: 全过

- [ ] **Step 7: 提交**

```bash
git add -A && git commit -m "feat(overlay): layered follow window with click-through"
```

---

### Task 8: 插件入口、部署与真机验收

**Files:**
- Create: `src/NxGeo.NxPlugin/SelectionCaptureSource.h/.cpp`, `src/NxGeo.NxPlugin/PluginEntry.h/.cpp`, `src/NxGeo.NxPlugin/ufusr.cpp`
- Create: `install/install.ps1`, `install/uninstall.ps1`, `install/nxgeo.men`
- Create: `docs/superpowers/plans/2026-11-19-nx10-acceptance.md`

**Interfaces:**
- Consumes: `QueryScheduler`、`NxQueryAdapter`、`SnapshotChannel`
- Produces: `namespace PluginEntry { void Start(); void Stop(); bool IsRunning(); }`；导出 `ufsta` / `ufusr`

- [ ] **Step 1: 捕获源**

`SelectionCaptureSource` 用 `std::thread` 每 50ms 调：
```cpp
NXOpen::Selection* sel = NXOpen::UI::GetUI()->SelectionManager();
int n = sel->GetNumSelectedObjects();                 // 0 -> 发空 ref 隐藏浮窗
NXOpen::TaggedObject* o = (n > 0) ? sel->GetSelectedTaggedObject(0) : nullptr;
```
**只取 tag，不做几何查询**；与上次 tag 相同则不发（去重由调度器再做一层）。
注意：`Selection` 调用必须在 NX 主线程——本线程用 `UF_UI_lock_ug_access` / `unlock` 包住那一小段。

- [ ] **Step 2: 生命周期**

`PluginEntry::Start()`：建 `SnapshotChannel`、`NxQueryAdapter`、`QueryScheduler`，`CreateProcessW` 拉起 `nxgeo-overlay.exe`，`AssignProcessToJobObject`（`JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`）。`Stop()` 反向清理并 `TerminateJobObject`。

- [ ] **Step 3: 入口与部署**

`ufusr.cpp`：
```cpp
extern "C" DllExport void ufsta(char* param, int* retCode, int rlen) { (void)param; (void)rlen; *retCode = 0; PluginEntry::Start(); }
extern "C" DllExport int  ufusr(char* param, int* retCode, int paramLen) { ufsta(param, retCode, paramLen); return 0; }
```
`install.ps1`：按 `$env:UGII_BASE_DIR` 定位，DLL 与 exe 拷到 `%UGII_USER_DIR%\startup` 与 `%\nxgeo`，`nxgeo.men` 拷到 `%\menus`；`uninstall.ps1` 逆操作。

- [ ] **Step 4: 真机验收**

| # | 场景 | 期望 |
|---|---|---|
| 1 | 点选面/边/体 | 浮窗 100ms 内出现，字段齐全 |
| 2 | 点空白取消选择 | 浮窗立即隐藏 |
| 3 | 无工作部件 | 不弹错，浮窗隐藏或显示「无工作部件」 |
| 4 | 查询中删除对象 | note 显示「对象已删除」，不崩 |
| 5 | 旧浮窗 + 新插件 | 显示「协议版本不符」 |
| 6 | 150% 缩放/双屏 | 定位不飘 |
| 7 | 连续切对象 10 分钟 | 无泄漏、无 >50ms 卡顿 |
| 8 | 关闭浮窗进程 | 下次选择自动重启 |

- [ ] **Step 5: 提交**

```bash
git add -A && git commit -m "feat(plugin): capture, lifecycle, deploy scripts, acceptance"
```

---

## 交付顺序与依赖

```
Task 0 -> Task 1 -> Task 2 -> Task 3 -> Task 4 -> Task 6 -> Task 7 -> Task 8
                                     \-> Task 5 (需 NX 在线，可与 6/7 并行) -/
```

Task 5 是唯一需要 NX 在线的任务，其余全部可离线完成。

## 自审记录

- **Spec 覆盖**：§5.1~5.4 → Task 1/2；§6.1~6.3 → Task 3/4/5；§6.4~6.5 → Task 7/8；§8 性能 → Task 8 第 7 项；§9 降级 → Review Focus 1~5 + Task 8 第 2/3/4/5 项；§10 → 各任务 Step + Task 8。
- **类型一致性**：`InfoSnapshot.fields` 在 Task 1 定义、Task 4/6 使用；`SnapshotChannel::Write` 返回 `unsigned long long` 与 `CurrentGen()` 同类型；`ObjRef.gen` 由调度器赋值。
- **Review Focus 落点**：1→Task 4（空 ref）+ Task 8(2)；2→Task 8(3)；3→Task 5 Step 2（返回码非 0 写 note）；4→Task 3 Step 1 版本用例 + Task 8(5)；5→Task 7 Step 6 + Task 8(6)。
- **ABI 风险**：Global Constraints 已禁止跨边界 STL 容器；Task 5 Step 1 用最小导出先验「v142 DLL 能否被 NX10 加载」，崩了立刻退回纯 UFUN 方案。
