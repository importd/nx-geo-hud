#pragma once

#include "FieldFlags.h"
#include "NxGeoConst.h"
#include "ObjKind.h"

// 跨进程共享的快照：定长、POD、无指针，直接 memcpy 到 512B 槽。
struct InfoSnapshot {
    unsigned           magic = NxGeoConst::Magic;
    unsigned short     version = NxGeoConst::ProtocolVersion;
    unsigned short     kind = 0;
    unsigned long long gen = 0;
    unsigned           tag = 0;
    unsigned           fields = Field_None;

    char               typeName[32] = { 0 };
    int                color = 0;
    int                layer = 0;

    double             bboxMin[3] = { 0.0, 0.0, 0.0 };
    double             bboxMax[3] = { 0.0, 0.0, 0.0 };

    double             length = 0.0;
    double             area = 0.0;
    double             perimeter = 0.0;
    double             volume = 0.0;
    double             distance = 0.0;

    char               distanceTo[48] = { 0 };
    char               note[64] = { 0 };
};

static_assert(sizeof(InfoSnapshot) <= NxGeoConst::SlotSize, "InfoSnapshot exceeds one slot");
