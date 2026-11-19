#pragma once

#include <string>

#include "ObjKind.h"

namespace TypeNameMap {
    // NX 常量：UF_solid_type = 70；subtype 0=实体 1=片体 2=面 3=边
    ObjKind     KindOf(int type, int subtype);
    std::string NameOf(int type, int subtype);
}
