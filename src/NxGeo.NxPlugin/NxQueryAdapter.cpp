#include "NxQueryAdapter.h"

#include <cstring>
#include <exception>
#include <vector>

#include <uf.h>
#include <uf_modl.h>
#include <uf_obj.h>
#include <uf_object_types.h>

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

NXOpen::Unit* FindUnit(NXOpen::Part* part, const char* name) {
    if (!part) return nullptr;
    return part->UnitCollection()->FindObject(name);
}

}  // namespace

InfoSnapshot NxQueryAdapter::QueryL0(const ObjRef& r) {

    InfoSnapshot s{};
    s.tag = r.tag;
    s.kind = static_cast<unsigned short>(r.kind);

    const tag_t t0 = static_cast<tag_t>(r.tag);
    tag_t t = t0;
    try {
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
                }
                s.kind = static_cast<unsigned short>(ObjKind::Body);
                std::strncpy(s.typeName, "实体(来自特征)", sizeof s.typeName - 1);
            } else {
                s.kind = static_cast<unsigned short>(TypeNameMap::KindOf(type, subtype));
                const std::string name = TypeNameMap::NameOf(type, subtype);
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
        NXOpen::Unit* lengthUnit = FindUnit(part, "Millimeter");
        NXOpen::Unit* areaUnit = FindUnit(part, "SquareMillimeter");

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
                s.volume = mp[1] * 1.0e9;
                s.fields |= Field_Volume;
            }
        }

        if (distanceTarget_ != 0 && distanceTarget_ != handle) {
            NXOpen::Part* part = WorkPart();
            if (!part) return;
            NXOpen::TaggedObject* a = NXOpen::NXObjectManager::Get(handle);
            NXOpen::TaggedObject* b = NXOpen::NXObjectManager::Get(static_cast<tag_t>(distanceTarget_));
            NXOpen::DisplayableObject* da = dynamic_cast<NXOpen::DisplayableObject*>(a);
            NXOpen::DisplayableObject* db = dynamic_cast<NXOpen::DisplayableObject*>(b);
            if (da && db) {
                NXOpen::MeasureDistance* m = part->MeasureManager()->NewDistance(
                    FindUnit(part, "Millimeter"), NXOpen::MeasureManager::MeasureTypeMinimum, da, db);
                if (m) {
                    s.distance = m->Value();
                    s.fields |= Field_Distance;
                }
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
    const bool ok = (mm != nullptr) && (FindUnit(part, "Millimeter") != nullptr);
    report = std::string("work part ok, measure manager ") + (mm ? "ok" : "null") +
             ", millimeter " + (FindUnit(part, "Millimeter") ? "ok" : "missing");
    return ok;
}
