#include "TypeNameMap.h"

#include <map>
#include <utility>

namespace {

constexpr int kSolidType = 70;
constexpr int kBodySubtype = 0;
constexpr int kSweptSubtype = 1;
constexpr int kFaceSubtype = 2;
constexpr int kEdgeSubtype = 3;
constexpr int kFeatureType = 205;   // UF_feature_type

const std::map<std::pair<int, int>, std::string>& Table() {
    static const std::map<std::pair<int, int>, std::string> t = {
        { { kSolidType, kBodySubtype },  "实体" },
        // 70/1 是 UF_solid_swept_body_subtype：内部用、不可显示，不是片体。
        // 片体也报 (70,0)，实体/片体要靠 UF_MODL_ask_body_type 区分。
        { { kSolidType, kSweptSubtype }, "扫掠体(内部)" },
        { { kSolidType, kFaceSubtype },  "面" },
        { { kSolidType, kEdgeSubtype },  "边" },
        { { kFeatureType, 0 },           "特征" },
    };
    return t;
}

}  // namespace

namespace TypeNameMap {

ObjKind KindOf(int type, int subtype) {
    if (type == kFeatureType) return ObjKind::Other;   // 特征由插件映射到它生成的体
    if (type != kSolidType) return ObjKind::Other;
    switch (subtype) {
        case kBodySubtype: return ObjKind::Body;
        case kFaceSubtype: return ObjKind::Face;
        case kEdgeSubtype: return ObjKind::Edge;
        default:           return ObjKind::Other;
    }
}

std::string NameOf(int type, int subtype) {
    const auto& t = Table();
    auto it = t.find({ type, subtype });
    if (it != t.end()) return it->second;
    return "未知(" + std::to_string(type) + "/" + std::to_string(subtype) + ")";
}

}  // namespace TypeNameMap
