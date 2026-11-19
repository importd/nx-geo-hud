#include "SelectionCaptureSource.h"

#include <windows.h>
#undef CreateDialog   // NX UI.hxx 的方法名与 Win32 宏冲突

#include <NXOpen/Selection.hxx>
#include <NXOpen/TaggedObject.hxx>
#include <NXOpen/UI.hxx>
#include <uf.h>
#include <uf_obj.h>
#include <uf_part.h>
#include <uf_ui.h>
#include <uf_object_types.h>

#include <string>

#include "Log.h"

void SelectionCaptureSource::Start(std::function<void(const ObjRef&)> sink) {
    sink_ = std::move(sink);
}

void SelectionCaptureSource::Stop() {
    sink_ = nullptr;
}

// 实验：NX 预选（悬停高亮）会不会把对象的 highlight_status 置真。
// 会的话就能用"每拍扫一遍"实现悬停，不需要射线求交。
void ProbeHighlighted() {
    static std::string last;

    const tag_t part = UF_PART_ask_display_part();
    if (part == NULL_TAG) return;

    std::string bodies;
    std::string features;

    // 体
    tag_t object = NULL_TAG;
    while (UF_OBJ_cycle_objs_in_part(part, UF_solid_type, &object) == 0 && object != NULL_TAG) {
        int type = 0;
        int subtype = 0;
        if (UF_OBJ_ask_type_and_subtype(object, &type, &subtype) != 0) continue;
        if (subtype != 0) continue;

        UF_OBJ_disp_props_t props{};
        if (UF_OBJ_ask_display_properties(object, &props) != 0) continue;
        if (props.highlight_status) bodies += std::to_string(static_cast<unsigned>(object)) + " ";
    }

    // 特征：NX10 默认点选命中的就是它，预选很可能也标在它身上
    object = NULL_TAG;
    while (UF_OBJ_cycle_objs_in_part(part, UF_feature_type, &object) == 0 && object != NULL_TAG) {
        UF_OBJ_disp_props_t props{};
        if (UF_OBJ_ask_display_properties(object, &props) != 0) continue;
        if (props.highlight_status) features += std::to_string(static_cast<unsigned>(object)) + " ";
    }

    const std::string current = "bodies=[" + bodies + "] features=[" + features + "]";
    if (current != last) {
        NxGeoLog::Write("highlighted " + current);
        last = current;
    }
}

void SelectionCaptureSource::PollOnce() {
    ProbeHighlighted();

    if (!sink_) return;

    unsigned tagFromSelection = 0;
    unsigned tagFromGlobal = 0;
    int selectionCount = -1;
    int globalCount = -1;

    try {
        NXOpen::Selection* sel = NXOpen::UI::GetUI()->SelectionManager();
        if (sel != nullptr) {
            selectionCount = sel->GetNumSelectedObjects();
            if (selectionCount > 0) {
                NXOpen::TaggedObject* obj = sel->GetSelectedTaggedObject(0);
                if (obj != nullptr) tagFromSelection = obj->Tag();
            }
        }
    } catch (...) {
        NxGeoLog::Write("poll: SelectionManager threw");
    }

    // 每次启动只打一次"即将调全局接口"，崩溃时日志会停在这一行
    static bool globalProbeLogged = false;
    if (!globalProbeLogged) {
        NxGeoLog::Write("poll: calling UF_UI_ask_global_sel_object_list");
        globalProbeLogged = true;
    }

    int num = 0;
    tag_t* tags = nullptr;
    if (UF_UI_ask_global_sel_object_list(&num, &tags) == 0) {
        globalCount = num;
        if (num > 0 && tags != nullptr) tagFromGlobal = tags[0];
        if (tags != nullptr) UF_free(tags);
    }

    const unsigned tag = tagFromSelection != 0 ? tagFromSelection : tagFromGlobal;
    if (tag == lastTag_) return;
    lastTag_ = tag;

    NxGeoLog::Write("poll: selCount=" + std::to_string(selectionCount) +
                    " globalCount=" + std::to_string(globalCount) +
                    " selTag=" + std::to_string(tagFromSelection) +
                    " globalTag=" + std::to_string(tagFromGlobal));

    ObjRef ref;
    ref.tag = tag;
    ref.kind = ObjKind::Other;   // 真实类型由 L0 查出来
    sink_(ref);
}
