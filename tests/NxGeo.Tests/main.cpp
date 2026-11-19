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
        r.kind = ObjKind::Face;
        sink(r);
    }
};

struct FakeQuery : INxQuery {
    int l0 = 0, l1 = 0, l2 = 0;
    InfoSnapshot QueryL0(const ObjRef& r) override {
        ++l0;
        InfoSnapshot s{};
        s.tag = r.tag;
        s.kind = (unsigned short)ObjKind::Face;
        s.fields = Field_L0;
        return s;
    }
    void FillL1(const ObjRef&, InfoSnapshot& s) override {
        ++l1;
        s.area = 1.0;
        s.fields |= Field_Area;
    }
    void FillL2(const ObjRef&, InfoSnapshot& s) override {
        ++l2;
        s.volume = 42.0;
        s.fields |= Field_Volume;
    }
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
    CHECK(KindOf(70, 1) == ObjKind::Body);   // 片体
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

int main() {
    TestConst();
    TestSnapshotRoundTrip();
    TestSnapshotRejectsBadHeader();
    TestTypeNameMap();
    TestChannelRoundTrip();
    TestChannelRejectsVersionMismatch();
    TestSchedulerDedupesAndDropsStale();
    TestSchedulerEmitsNoneOnEmptySelection();
    TestViewModelShowsOnlyFilledFields();
    TestPlacementFollowsCursorAndStaysInside();

    std::cout << (Check::Failures() == 0 ? "ALL PASS" : "FAILED") << "\n";
    return Check::Failures() == 0 ? 0 : 1;
}
