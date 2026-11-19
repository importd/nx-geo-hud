#include "SelectionCaptureSource.h"

#include <windows.h>
#undef CreateDialog   // NX UI.hxx 的方法名与 Win32 宏冲突

#include <NXOpen/Selection.hxx>
#include <NXOpen/TaggedObject.hxx>
#include <NXOpen/UI.hxx>
#include <uf.h>
#include <uf_ui.h>

#include "Log.h"

void SelectionCaptureSource::Start(std::function<void(const ObjRef&)> sink) {
    sink_ = std::move(sink);
}

void SelectionCaptureSource::Stop() {
    sink_ = nullptr;
}

void SelectionCaptureSource::PollOnce() {
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
