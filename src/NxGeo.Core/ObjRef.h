#pragma once

#include "ObjKind.h"

struct ObjRef {
    unsigned long long gen = 0;
    unsigned           tag = 0;
    ObjKind            kind = ObjKind::None;
    double             cursor[3] = { 0.0, 0.0, 0.0 };
};
