#include "Check.h"

#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include <windows.h>

#include "ICaptureSource.h"
#include "INxQuery.h"
#include "InfoSnapshot.h"
#include "InfoViewModel.h"
#include "NxGeoConst.h"
#include "OverlayPlacement.h"
#include "OverlayState.h"
#include "QueryScheduler.h"
#include "SnapshotChannel.h"
#include "SnapshotCodec.h"
#include "TypeNameMap.h"

namespace {

struct FakeSource : ICaptureSource {
    std::function<void(const ObjRef&)> sink;
    void Start(std::function<void(const ObjRef&)> s) override { sink = s; }
    void Stop() override {}
    void Emit(unsigned tag) {
        ObjRef r;
        r.tag = tag;
        r.kind = ObjKind::Other;   // 捕获层只知道 tag，kind 由 L0 查出来
        sink(r);
    }
};

struct FakeQuery : INxQuery {
    int l0 = 0, l1 = 0, l2 = 0;
    unsigned distanceTarget = 0;
    ObjKind lastL1Kind = ObjKind::None;
    InfoSnapshot QueryL0(const ObjRef& r) override {
        ++l0;
        InfoSnapshot s{};
        s.tag = r.tag;
        s.kind = (unsigned short)ObjKind::Face;
        s.fields = Field_L0;
        return s;
    }
    void FillL1(const ObjRef& r, InfoSnapshot& s) override {
        ++l1;
        lastL1Kind = r.kind;
        s.area = 1.0;
        s.fields |= Field_Area;
    }
    void FillL2(const ObjRef&, InfoSnapshot& s) override {
        ++l2;
        s.volume = 42.0;
        s.fields |= Field_Volume;
    }
    void SetDistanceTarget(unsigned tag) override { distanceTarget = tag; }
};

struct ThrowingQuery : INxQuery {
    InfoSnapshot QueryL0(const ObjRef&) override { throw std::runtime_error("boom"); }
    void FillL1(const ObjRef&, InfoSnapshot&) override {}
    void FillL2(const ObjRef&, InfoSnapshot&) override {}
};

}  // namespace

static void TestConst() {
    CHECK_EQ(NxGeoConst::SlotSize, 512);
    CHECK_EQ(std::string(NxGeoConst::SharedMemoryName(42)), std::string("Local\\NXGEO_42"));
    CHECK_EQ(std::string(NxGeoConst::EventName(42)), std::string("Local\\NXGEO_EVT_42"));
}

static void TestSnapshotRoundTrip() {
    InfoSnapshot s{};
    s.kind = (unsigned short)ObjKind::Face;
    s.gen = 7;
    s.tag = 0x1234;
    s.fields = Field_L0 | Field_Area;
    std::strncpy(s.typeName, "圆柱面", sizeof s.typeName - 1);
    s.color = 186;
    s.layer = 1;
    s.area = 12.5;

    unsigned char buf[NxGeoConst::SlotSize];
    EncodeSnapshot(s, buf);
    InfoSnapshot b = DecodeSnapshot(buf);

    CHECK_EQ(b.gen, 7ull);
    CHECK_EQ(b.tag, 0x1234u);
    CHECK_EQ(std::string(b.typeName), std::string("圆柱面"));
    CHECK_EQ(b.area, 12.5);
    CHECK(HasField(b.fields, Field_Area));
    CHECK(!HasField(b.fields, Field_Volume));
}

static void TestSnapshotRejectsBadHeader() {
    unsigned char buf[NxGeoConst::SlotSize] = {0};
    bool threw = false;
    try {
        DecodeSnapshot(buf);   // magic == 0
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
}

static void TestTypeNameMap() {
    using namespace TypeNameMap;
    CHECK(KindOf(70, 0) == ObjKind::Body);   // 实体
    CHECK(KindOf(70, 2) == ObjKind::Face);
    CHECK(KindOf(70, 3) == ObjKind::Edge);
    CHECK(KindOf(999, 0) == ObjKind::Other);
    CHECK_EQ(NameOf(70, 3), std::string("边"));
    CHECK_EQ(NameOf(999, 7), std::string("未知(999/7)"));
}

static void TestChannelRoundTrip() {
    std::string name = NxGeoConst::SharedMemoryName(9000 + GetCurrentProcessId() % 1000);

    SnapshotChannel w(name, true);
    SnapshotChannel r(name, false);
    w.SetNxHwnd(0x1234);
    w.SetNxPid(42);

    InfoSnapshot a{};
    a.tag = 11;
    InfoSnapshot b{};
    b.tag = 22;

    unsigned long long g1 = w.Write(a);
    unsigned long long g2 = w.Write(b);
    CHECK_EQ(g2, g1 + 1);

    InfoSnapshot got{};
    CHECK(r.TryRead(got));
    CHECK_EQ(got.tag, 22u);
    CHECK_EQ(got.gen, g2);
    CHECK_EQ(r.NxHwnd(), 0x1234ll);
    CHECK(!r.VersionMismatch());
}

static void TestChannelRejectsVersionMismatch() {
    std::string name = NxGeoConst::SharedMemoryName(9100 + GetCurrentProcessId() % 1000);

    SnapshotChannel w(name, true);
    SnapshotChannel r(name, false);
    InfoSnapshot a{};
    a.tag = 11;
    w.Write(a);

    // 模拟旧版本写入方：直接把头里的 version 改成 99
    std::wstring wname(name.begin(), name.end());
    HANDLE map = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, wname.c_str());
    CHECK(map != nullptr);
    SharedHeader* h = static_cast<SharedHeader*>(MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedHeader)));
    CHECK(h != nullptr);
    h->version = 99;
    UnmapViewOfFile(h);
    CloseHandle(map);

    InfoSnapshot got{};
    CHECK(!r.TryRead(got));
    CHECK(r.VersionMismatch());
}

static void TestTypeNameMapSweptBodyIsNotSheet() {
    // uf_object_types.h:501 —— 70/1 是 UF_solid_swept_body_subtype（内部用、不可显示），不是片体
    CHECK(TypeNameMap::KindOf(70, 1) != ObjKind::Body);
    CHECK(TypeNameMap::NameOf(70, 1) != std::string("片体"));
}

static void TestSchedulerDedupesAndDropsStale() {
    std::vector<QueryScheduler::Task> tasks;
    FakeSource src;
    FakeQuery q;
    std::string name = NxGeoConst::SharedMemoryName(9200 + GetCurrentProcessId() % 500);
    SnapshotChannel ch(name, true);

    QueryScheduler sch(&src, &q, &ch, [&](QueryScheduler::Task t) { tasks.push_back(t); });
    sch.Start();

    src.Emit(5);
    src.Emit(5);
    src.Emit(5);
    CHECK_EQ(q.l0, 1);   // 同一 tag 只查一次
    CHECK_EQ(q.l1, 1);
    CHECK(q.lastL1Kind == ObjKind::Face);   // L1 必须拿到 L0 查出的真实 kind

    tasks.clear();
    src.Emit(7);
    src.Emit(8);

    tasks[0]();   // tag=7 的 L2，已过期
    InfoSnapshot afterStale{};
    CHECK(ch.TryRead(afterStale));
    CHECK_EQ(afterStale.tag, 8u);
    CHECK(!HasField(afterStale.fields, Field_Volume));

    tasks[1]();   // tag=8 的 L2，最新
    InfoSnapshot afterFresh{};
    CHECK(ch.TryRead(afterFresh));
    CHECK_EQ(afterFresh.tag, 8u);
    CHECK(HasField(afterFresh.fields, Field_Volume));

    sch.Stop();
}

static void TestSchedulerEmitsNoneOnEmptySelection() {
    FakeSource src;
    FakeQuery q;
    std::string name = NxGeoConst::SharedMemoryName(9300 + GetCurrentProcessId() % 500);
    SnapshotChannel ch(name, true);

    QueryScheduler sch(&src, &q, &ch, [](QueryScheduler::Task) {});
    sch.Start();
    src.Emit(0);

    InfoSnapshot got{};
    CHECK(ch.TryRead(got));
    CHECK_EQ(static_cast<int>(got.kind), static_cast<int>(ObjKind::None));

    sch.Stop();
}

static void TestViewModelShowsOnlyFilledFields() {
    InfoViewModel vm;

    InfoSnapshot s{};
    s.kind = static_cast<unsigned short>(ObjKind::Face);
    s.tag = 0x2A;
    std::strncpy(s.typeName, "面", sizeof s.typeName - 1);
    s.fields = Field_L0;

    vm.Apply(s);
    int visible = 0;
    for (const auto& row : vm.Rows()) {
        if (row.visible) ++visible;
    }
    CHECK_EQ(visible, 3);   // 类型 / tag / 颜色

    s.fields |= Field_Area;
    s.area = 12.5;
    vm.Apply(s);

    const InfoRow& areaRow = vm.Rows()[5];
    CHECK(areaRow.visible);
    CHECK_EQ(areaRow.value, std::string("12.500"));

    const int revision = vm.Revision();
    vm.Apply(s);   // 完全相同，不应该产生新版本
    CHECK_EQ(vm.Revision(), revision);
}

static void TestPlacementFollowsCursorAndStaysInside() {
    Placement a = ComputePlacement(500, 500, 300, 200, 0, 0, 1920, 1080);
    CHECK_EQ(a.x, 516);   // 光标右下 16px
    CHECK_EQ(a.y, 520);   // 光标右下 20px

    Placement p = ComputePlacement(1900, 1070, 300, 200, 0, 0, 1920, 1080);
    CHECK(p.x + 300 <= 1920);
    CHECK(p.y + 200 <= 1080);
    CHECK(p.x >= 0);
    CHECK(p.y >= 0);
}

// 面板比 NX 窗口还大时，结果仍必须落在窗口内（不能算出负坐标）
static void TestPlacementWhenPanelIsWiderThanWindow() {
    Placement p = ComputePlacement(50, 50, 800, 600, 0, 0, 400, 300);
    CHECK(p.x >= 0);
    CHECK(p.y >= 0);
}

static void TestPointInRect() {
    CHECK(PointInRect(10, 10, 0, 0, 100, 100));
    CHECK(!PointInRect(101, 10, 0, 0, 100, 100));
    CHECK(!PointInRect(10, -1, 0, 0, 100, 100));
}

static void TestOverlayStateDecisions() {
    CHECK(DecideOverlayState(false, false, false) == OverlayState::Hide);          // 没读到
    CHECK(DecideOverlayState(false, true, false) == OverlayState::ShowMessage);    // 版本不符
    CHECK(DecideOverlayState(true, false, true) == OverlayState::Hide);            // 空选择
    CHECK(DecideOverlayState(true, false, false) == OverlayState::ShowSnapshot);   // 正常
}

// C2 回归：选择没变时，50ms 的一次重复轮询不能把在途的 L2 结果作废
static void TestSchedulerKeepsL2AliveAcrossDuplicatePolls() {
    std::vector<QueryScheduler::Task> tasks;
    FakeSource src;
    FakeQuery q;
    std::string name = NxGeoConst::SharedMemoryName(9400 + GetCurrentProcessId() % 400);
    SnapshotChannel ch(name, true);

    QueryScheduler sch(&src, &q, &ch, [&](QueryScheduler::Task t) { tasks.push_back(t); });
    sch.Start();
    src.Emit(9);
    src.Emit(9);   // 下一次轮询，选择没变
    CHECK_EQ(tasks.size(), 1u);
    tasks[0]();

    InfoSnapshot got{};
    CHECK(ch.TryRead(got));
    CHECK_EQ(got.tag, 9u);
    CHECK(HasField(got.fields, Field_Volume));
    sch.Stop();
}

// C3 回归：查询抛异常不能逃出捕获线程（逃出去 NX 会终止）
static void TestSchedulerContainsQueryException() {
    FakeSource src;
    ThrowingQuery q;
    std::string name = NxGeoConst::SharedMemoryName(9500 + GetCurrentProcessId() % 300);
    SnapshotChannel ch(name, true);

    QueryScheduler sch(&src, &q, &ch, [](QueryScheduler::Task) {});
    sch.Start();

    bool escaped = false;
    try {
        src.Emit(3);
    } catch (...) {
        escaped = true;
    }
    CHECK(!escaped);

    InfoSnapshot got{};
    CHECK(ch.TryRead(got));
    CHECK(std::string(got.note).size() > 0);   // 至少留下失败原因
    sch.Stop();
}

// I2：距离目标应该是「上一个选中的对象」
static void TestSchedulerTracksPreviousTagAsDistanceTarget() {
    FakeSource src;
    FakeQuery q;
    std::string name = NxGeoConst::SharedMemoryName(9600 + GetCurrentProcessId() % 300);
    SnapshotChannel ch(name, true);

    QueryScheduler sch(&src, &q, &ch, [](QueryScheduler::Task) {});
    sch.Start();
    src.Emit(100);
    src.Emit(200);
    CHECK_EQ(q.distanceTarget, 100u);
    sch.Stop();
}

// I11：Stop 之后还能再 Start 起来
static void TestSchedulerCanRestart() {
    FakeSource src;
    FakeQuery q;
    std::string name = NxGeoConst::SharedMemoryName(9700 + GetCurrentProcessId() % 200);
    SnapshotChannel ch(name, true);

    QueryScheduler sch(&src, &q, &ch);   // 默认工作线程
    sch.Start();
    src.Emit(11);
    bool first = false;
    for (int i = 0; i < 100 && !first; ++i) {
        InfoSnapshot s{};
        if (ch.TryRead(s) && s.tag == 11 && HasField(s.fields, Field_Volume)) first = true;
        if (!first) Sleep(10);
    }
    CHECK(first);

    sch.Stop();
    sch.Start();
    src.Emit(12);
    bool second = false;
    for (int i = 0; i < 100 && !second; ++i) {
        InfoSnapshot s{};
        if (ch.TryRead(s) && s.tag == 12 && HasField(s.fields, Field_Volume)) second = true;
        if (!second) Sleep(10);
    }
    CHECK(second);
    sch.Stop();
}

static void TestViewModelMessageReplacesRows() {
    InfoViewModel vm;
    InfoSnapshot s{};
    s.kind = static_cast<unsigned short>(ObjKind::Face);
    s.tag = 1;
    s.fields = Field_L0;
    std::strncpy(s.typeName, "面", sizeof s.typeName - 1);
    vm.Apply(s);

    vm.ShowMessage("协议版本不符，请重启 NX");
    int visible = 0;
    for (const auto& row : vm.Rows()) {
        if (row.visible) ++visible;
    }
    CHECK_EQ(visible, 1);
    CHECK_EQ(vm.Rows()[0].value, std::string("协议版本不符，请重启 NX"));

    vm.Apply(s);   // 恢复快照后消息行必须消失
    CHECK_EQ(vm.Rows()[0].value, std::string("面"));
}

int main() {
    TestConst();
    TestSnapshotRoundTrip();
    TestSnapshotRejectsBadHeader();
    TestTypeNameMap();
    TestTypeNameMapSweptBodyIsNotSheet();
    TestChannelRoundTrip();
    TestChannelRejectsVersionMismatch();
    TestSchedulerDedupesAndDropsStale();
    TestSchedulerKeepsL2AliveAcrossDuplicatePolls();
    TestSchedulerContainsQueryException();
    TestSchedulerTracksPreviousTagAsDistanceTarget();
    TestSchedulerCanRestart();
    TestSchedulerEmitsNoneOnEmptySelection();
    TestViewModelShowsOnlyFilledFields();
    TestViewModelMessageReplacesRows();
    TestPlacementFollowsCursorAndStaysInside();
    TestPlacementWhenPanelIsWiderThanWindow();
    TestPointInRect();
    TestOverlayStateDecisions();

    std::cout << (Check::Failures() == 0 ? "ALL PASS" : "FAILED") << "\n";
    return Check::Failures() == 0 ? 0 : 1;
}
