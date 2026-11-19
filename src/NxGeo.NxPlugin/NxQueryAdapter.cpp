#include "NxQueryAdapter.h"

#include <cmath>
#include <cstring>
#include <exception>
#include <vector>

#include <uf.h>
#include <uf_assem.h>
#include <uf_eval.h>
#include <uf_modl.h>
#include <uf_obj.h>
#include <uf_object_types.h>
#include <uf_part.h>

#include <NXOpen/DisplayableObject.hxx>
#include <NXOpen/Edge.hxx>
#include <NXOpen/Face.hxx>
#include <NXOpen/IParameterizedSurface.hxx>
#include <NXOpen/MeasureDistance.hxx>
#include <NXOpen/MeasureFaces.hxx>
#include <NXOpen/MeasureLength.hxx>
#include <NXOpen/MeasureManager.hxx>
#include <NXOpen/NXObjectManager.hxx>
#include <NXOpen/Part.hxx>
#include <NXOpen/PartCollection.hxx>
#include <NXOpen/Session.hxx>
#include <NXOpen/SingleMeasure.hxx>
#include <NXOpen/Unit.hxx>
#include <NXOpen/UnitCollection.hxx>

#include "FieldFlags.h"
#include "Log.h"
#include "TypeNameMap.h"

namespace {

void SetNote(InfoSnapshot& s, const std::string& text) {
    std::strncpy(s.note, text.c_str(), sizeof s.note - 1);
    s.note[sizeof s.note - 1] = '\0';
}

NXOpen::Part* WorkPart() {
    NXOpen::Session* session = NXOpen::Session::GetSession();
    if (!session) return nullptr;
    return session->Parts()->Work();
}

// NX 的单位对象名并不统一（MilliMeter / Millimeter / mm 都见过），
// FindObject 找不到会抛 "No object found with this name"，所以逐个试并吞掉异常。
NXOpen::Unit* FindUnit(NXOpen::Part* part, const char* const* candidates) {
    if (!part) return nullptr;
    for (int i = 0; candidates[i] != nullptr; ++i) {
        try {
            NXOpen::Unit* unit = part->UnitCollection()->FindObject(candidates[i]);
            if (unit != nullptr) return unit;
        } catch (...) {
            // 换下一个候选名
        }
    }
    return nullptr;
}

const char* const kLengthUnits[] = { "MilliMeter", "Millimeter", "mm", nullptr };
const char* const kAreaUnits[] = { "SquareMilliMeter", "SquareMillimeter", "mm^2", nullptr };

}  // namespace

namespace {

// 对象名（没名字时 NX 返回空串）
void ReadObjectName(tag_t tag, char* out, size_t outSize) {
    char buffer[256] = { 0 };
    if (UF_OBJ_ask_name(tag, buffer) == 0 && buffer[0] != '\0') {
        std::strncpy(out, buffer, outSize - 1);
        out[outSize - 1] = '\0';
    }
}

const char* FaceTypeName(int surfType) {
    switch (surfType) {
        case 16: return "圆柱面";
        case 17: return "圆锥面";
        case 18: return "球面";
        case 19: return "圆环面";
        case 20: return "拉伸面";
        case 22: return "平面";
        case 23: return "倒圆面";
        case 43: return "B曲面";
        case 65: return "偏置面";
        case 66: return "外来面";
        default: return "面";
    }
}

const char* EdgeTypeName(int edgeType) {
    switch (edgeType) {
        case 3001: return "直线边";
        case 3002: return "圆弧边";
        case 3003: return "椭圆弧边";
        case 3004: return "相交线边";
        case 3005: return "样条边";
        case 3006: return "SP曲线边";
        case 3007: return "外来边";
        case 3008: return "等参边";
        case 3009: return "裁剪曲线边";
        default: return "边";
    }
}

}  // namespace

InfoSnapshot NxQueryAdapter::QueryL0(const ObjRef& r) {

    InfoSnapshot s{};
    s.tag = r.tag;
    s.kind = static_cast<unsigned short>(r.kind);

    const tag_t t0 = static_cast<tag_t>(r.tag);
    tag_t t = t0;
    try {
        ReadObjectName(t0, s.objectName, sizeof s.objectName);

        int type = 0;
        int subtype = 0;
        if (UF_OBJ_ask_type_and_subtype(t0, &type, &subtype) == 0) {
            if (type == UF_feature_type) {
                // NX10 在无命令状态下点选，拿到的是特征而不是体/面/边：
                // 映射到该特征生成的体，后续按体处理。
                tag_t body = NULL_TAG;
                if (UF_MODL_ask_feat_body(t0, &body) == 0 && body != NULL_TAG) {
                    t = body;
                    s.tag = static_cast<unsigned>(body);
                    if (s.objectName[0] == '\0') {
                        ReadObjectName(body, s.objectName, sizeof s.objectName);
                    }
                }
                s.kind = static_cast<unsigned short>(ObjKind::Body);
                std::strncpy(s.typeName, "实体(来自特征)", sizeof s.typeName - 1);
            } else {
                s.kind = static_cast<unsigned short>(TypeNameMap::KindOf(type, subtype));
                std::string name = TypeNameMap::NameOf(type, subtype);

                if (s.kind == static_cast<unsigned short>(ObjKind::Face)) {
                    // 具体子类型 + 半径/半角
                    int surfType = 0;
                    double point[3] = { 0 }, dir[3] = { 0 }, box[6] = { 0 };
                    double radius = 0.0, radData = 0.0;
                    int normDir = 0;
                    if (UF_MODL_ask_face_data(t, &surfType, point, dir, box,
                                              &radius, &radData, &normDir) == 0) {
                        char extra[64] = { 0 };
                        switch (surfType) {
                            case 16:
                            case 18:
                                std::snprintf(extra, sizeof extra, " R=%.3f", radius);
                                break;
                            case 17:
                                std::snprintf(extra, sizeof extra, " R=%.3f 半角=%.1f°",
                                              radius, radData * 57.29577951308232);
                                break;
                            case 19:
                                std::snprintf(extra, sizeof extra, " R=%.3f r=%.3f",
                                              radius, radData);
                                break;
                            default:
                                break;
                        }
                        name = std::string(FaceTypeName(surfType)) + " " + std::to_string(surfType) + extra;

                        // 法向 + 拔模角（相对 +Z，即默认开模方向）
                        s.normal[0] = dir[0];
                        s.normal[1] = dir[1];
                        s.normal[2] = dir[2];
                        double dz = dir[2];
                        if (dz > 1.0) dz = 1.0;
                        if (dz < -1.0) dz = -1.0;
                        s.draftAngle = 90.0 - std::acos(dz) * 57.29577951308232;
                        s.fields |= Field_Normal;
                    }

                    // 生成该面的特征
                    uf_list_p_t faceFeats = nullptr;
                    if (UF_MODL_ask_face_feats(t, &faceFeats) == 0 && faceFeats != nullptr) {
                        int featCount = 0;
                        if (UF_MODL_ask_list_count(faceFeats, &featCount) == 0 && featCount > 0) {
                            tag_t feat = NULL_TAG;
                            if (UF_MODL_ask_list_item(faceFeats, 0, &feat) == 0 && feat != NULL_TAG) {
                                ReadObjectName(feat, s.featureName, sizeof s.featureName);
                            }
                        }
                        UF_MODL_delete_list(&faceFeats);
                    }
                } else if (s.kind == static_cast<unsigned short>(ObjKind::Edge)) {
                    int edgeType = 0;
                    if (UF_MODL_ask_edge_type(t, &edgeType) == 0) {
                        name = std::string(EdgeTypeName(edgeType)) + " " + std::to_string(edgeType);

                        // 圆弧边取半径
                        if (edgeType == 3002) {
                            UF_EVAL_p_t eval = nullptr;
                            if (UF_EVAL_initialize(t, &eval) == 0 && eval != nullptr) {
                                logical isArc = FALSE;
                                if (UF_EVAL_is_arc(eval, &isArc) == 0 && isArc) {
                                    UF_EVAL_arc_t arc{};
                                    if (UF_EVAL_ask_arc(eval, &arc) == 0) {
                                        s.radius = arc.radius;
                                        s.fields |= Field_Radius;
                                    }
                                }
                                UF_EVAL_free(eval);
                            }
                        }
                    }

                    uf_list_p_t edgeFeats = nullptr;
                    if (UF_MODL_ask_edge_feats(t, &edgeFeats) == 0 && edgeFeats != nullptr) {
                        int featCount = 0;
                        if (UF_MODL_ask_list_count(edgeFeats, &featCount) == 0 && featCount > 0) {
                            tag_t feat = NULL_TAG;
                            if (UF_MODL_ask_list_item(edgeFeats, 0, &feat) == 0 && feat != NULL_TAG) {
                                ReadObjectName(feat, s.featureName, sizeof s.featureName);
                            }
                        }
                        UF_MODL_delete_list(&edgeFeats);
                    }
                }

                std::strncpy(s.typeName, name.c_str(), sizeof s.typeName - 1);
            }
        }

        UF_OBJ_disp_props_t props{};
        if (UF_OBJ_ask_display_properties(t, &props) == 0) {
            s.color = props.color;
            s.layer = props.layer;
        }

        double box[6] = { 0, 0, 0, 0, 0, 0 };
        if (UF_MODL_ask_bounding_box(t, box) == 0) {
            for (int i = 0; i < 3; ++i) {
                s.bboxMin[i] = box[i];
                s.bboxMax[i] = box[i + 3];
            }
            s.fields |= Field_BBox;
        }

        s.fields |= Field_L0;

        // 装配上下文里标出这个对象属于哪个零件
        if (UF_ASSEM_is_occurrence(t0)) {
            tag_t owningPart = NULL_TAG;
            char partPath[512] = { 0 };
            if (UF_OBJ_ask_owning_part(t0, &owningPart) == 0 && owningPart != NULL_TAG &&
                UF_PART_ask_part_name(owningPart, partPath) == 0) {
                std::string path(partPath);
                const size_t slash = path.find_last_of("\\/");
                if (slash != std::string::npos) path = path.substr(slash + 1);
                std::strncpy(s.componentName, path.c_str(), sizeof s.componentName - 1);
            }
        }
    } catch (const std::exception& e) {
        SetNote(s, e.what());
    } catch (...) {
        SetNote(s, "L0 查询失败");
    }
    return s;
}

void NxQueryAdapter::FillL1(const ObjRef& r, InfoSnapshot& s) {

    // 体不需要 L1：包围盒在 L0 就有了，面积/长度/体积分别属于面和边
    if (static_cast<ObjKind>(s.kind) == ObjKind::Body) return;

    try {
        NXOpen::Part* part = WorkPart();
        if (!part) {
            SetNote(s, "无工作部件");
            return;
        }

        NXOpen::TaggedObject* obj =
            NXOpen::NXObjectManager::Get(static_cast<tag_t>(s.tag != 0 ? s.tag : r.tag));
        if (!obj) {
            SetNote(s, "对象已删除");
            return;
        }
        NXOpen::MeasureManager* mm = part->MeasureManager();
        NXOpen::Unit* lengthUnit = FindUnit(part, kLengthUnits);
        NXOpen::Unit* areaUnit = FindUnit(part, kAreaUnits);

        const ObjKind kind = static_cast<ObjKind>(s.kind);
        if (kind == ObjKind::Face) {
            NXOpen::Face* face = dynamic_cast<NXOpen::Face*>(obj);
            if (face) {
                std::vector<NXOpen::IParameterizedSurface*> faces;
                faces.push_back(face);
                NXOpen::MeasureFaces* m = mm->NewFaceProperties(areaUnit, lengthUnit, 0.99, faces);
                if (m) {
                    s.area = m->Area();
                    s.perimeter = m->Perimeter();
                    s.fields |= Field_Area;
                }
            }
        } else if (kind == ObjKind::Edge || kind == ObjKind::Curve) {
            NXOpen::DisplayableObject* d = dynamic_cast<NXOpen::DisplayableObject*>(obj);
            if (d) {
                std::vector<NXOpen::DisplayableObject*> objs;
                objs.push_back(d);
                NXOpen::MeasureLength* m = mm->NewLength(lengthUnit, objs);
                if (m) {
                    s.length = m->Value();
                    s.fields |= Field_Length;
                }
            }
        }
    } catch (const std::exception& e) {
        SetNote(s, e.what());
    } catch (...) {
        SetNote(s, "L1 查询失败");
    }
}

void NxQueryAdapter::FillL2(const ObjRef& r, InfoSnapshot& s) {

    const tag_t handle = static_cast<tag_t>(s.tag != 0 ? s.tag : r.tag);
    const ObjKind kind = static_cast<ObjKind>(s.kind);

    try {
        if (kind == ObjKind::Body) {
            tag_t t = handle;
            double accuracy[11] = { 0 };
            accuracy[0] = 0.99;
            double mp[47] = { 0 };
            double stats[13] = { 0 };
            // type=1 实体, units=4(kg-m) -> 体积单位 m^3，换算成 mm^3
            if (UF_MODL_ask_mass_props_3d(&t, 1, 1, 4, 0.0, 1, accuracy, mp, stats) == 0) {
                // units=4(kg-m)：体积 m^3、面积 m^2、质量 kg
                s.volume = mp[1] * 1.0e9;
                s.surfaceArea = mp[0] * 1.0e6;
                s.mass = mp[2];
                s.fields |= Field_Volume;
                if (s.surfaceArea > 0.0) s.fields |= Field_Surface;
                if (s.mass > 0.0) s.fields |= Field_Mass;
            }
        }

        if (distanceTarget_ != 0 && distanceTarget_ != handle) {
            // 用 UFUN 算最小距离：NXOpen 的 NewDistance 需要先解析对象指针，体/面/边更稳
            double guess[3] = { 0.0, 0.0, 0.0 };
            double pt1[3] = { 0.0, 0.0, 0.0 };
            double pt2[3] = { 0.0, 0.0, 0.0 };
            double minDist = 0.0;
            double accuracy = 0.0;
            if (UF_MODL_ask_minimum_dist_2(handle, static_cast<tag_t>(distanceTarget_),
                                           0, guess, 0, guess,
                                           &minDist, pt1, pt2, &accuracy) == 0) {
                s.distance = minDist;
                s.fields |= Field_Distance;
                std::strncpy(s.distanceTo, "上一个选中对象", sizeof s.distanceTo - 1);
            }
        }
    } catch (const std::exception& e) {
        SetNote(s, e.what());
    } catch (...) {
        SetNote(s, "L2 查询失败");
    }
}

bool NxQueryAdapter::SelfTest(std::string& report) {
    NXOpen::Part* part = WorkPart();
    if (!part) {
        report = "no work part";
        return false;
    }

    NXOpen::MeasureManager* mm = part->MeasureManager();
    const bool ok = (mm != nullptr) && (FindUnit(part, kLengthUnits) != nullptr);
    report = std::string("work part ok, measure manager ") + (mm ? "ok" : "null") +
             ", millimeter " + (FindUnit(part, kLengthUnits) ? "ok" : "missing");
    return ok;
}
