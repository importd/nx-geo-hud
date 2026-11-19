#include "TypeNameMap.h"

#include <map>
#include <utility>

namespace {

constexpr int kSolidType = 70;
constexpr int kBodySubtype = 0;
constexpr int kSheetSubtype = 1;
constexpr int kFaceSubtype = 2;
constexpr int kEdgeSubtype = 3;

const std::map<std::pair<int, int>, std::string>& Table() {
    static const std::map<std::pair<int, int>, std::string> t = {
        { { kSolidType, kBodySubtype },  "实体" },
        { { kSolidType, kSheetSubtype }, "片体" },
        { { kSolidType, kFaceSubtype },  "面" },
        { { kSolidType, kEdgeSubtype },  "边" },
    };
    return t;
}

}  // namespace

namespace TypeNameMap {

ObjKind KindOf(int type, int subtype) {
    if (type != kSolidType) return ObjKind::Other;
    switch (subtype) {
        case kBodySubtype:
        case kSheetSubtype: return ObjKind::Body;
        case kFaceSubtype:  return ObjKind::Face;
        case kEdgeSubtype:  return ObjKind::Edge;
        default:            return ObjKind::Other;
    }
}

std::string NameOf(int type, int subtype) {
    const auto& t = Table();
    auto it = t.find({ type, subtype });
    if (it != t.end()) return it->second;
    return "未知(" + std::to_string(type) + "/" + std::to_string(subtype) + ")";
}

}  // namespace TypeNameMap
