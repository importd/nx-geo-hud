#include "Check.h"

#include <cstring>
#include <stdexcept>
#include <string>
#include <windows.h>

#include "InfoSnapshot.h"
#include "NxGeoConst.h"
#include "SnapshotChannel.h"
#include "SnapshotCodec.h"
#include "TypeNameMap.h"

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

int main() {
    TestConst();
    TestSnapshotRoundTrip();
    TestSnapshotRejectsBadHeader();
    TestTypeNameMap();
    TestChannelRoundTrip();
    TestChannelRejectsVersionMismatch();

    std::cout << (Check::Failures() == 0 ? "ALL PASS" : "FAILED") << "\n";
    return Check::Failures() == 0 ? 0 : 1;
}
