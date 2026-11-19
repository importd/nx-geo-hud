#include "Check.h"

#include <cstring>
#include <stdexcept>
#include <string>

#include "InfoSnapshot.h"
#include "NxGeoConst.h"
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

int main() {
    TestConst();
    TestSnapshotRoundTrip();
    TestSnapshotRejectsBadHeader();
    TestTypeNameMap();

    std::cout << (Check::Failures() == 0 ? "ALL PASS" : "FAILED") << "\n";
    return Check::Failures() == 0 ? 0 : 1;
}
