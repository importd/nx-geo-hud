#include "SelectionCaptureSource.h"

#include <windows.h>
#undef CreateDialog   // NX UI.hxx 的方法名与 Win32 宏冲突

#include <vector>

#include <NXOpen/Selection.hxx>
#include <NXOpen/TaggedObject.hxx>
#include <NXOpen/UI.hxx>
#include <uf.h>
#include <uf_obj.h>
#include <uf_part.h>
#include <uf_ui.h>

#include "Log.h"
#include "SelectionDiff.h"

void SelectionCaptureSource::Start(std::function<void(const ObjRef&)> sink) {
    sink_ = std::move(sink);
}

void SelectionCaptureSource::Stop() {
    sink_ = nullptr;
    previous_.clear();
    lastTag_ = 0xFFFFFFFFu;
}

void SelectionCaptureSource::PollOnce() {
    if (!sink_) return;

    std::vector<unsigned> current;

    try {
        NXOpen::Selection* sel = NXOpen::UI::GetUI()->SelectionManager();
        if (sel != nullptr) {
            const int count = sel->GetNumSelectedObjects();
            for (int i = 0; i < count; ++i) {
                NXOpen::TaggedObject* obj = sel->GetSelectedTaggedObject(i);
                if (obj != nullptr) current.push_back(obj->Tag());
            }
        }
    } catch (...) {
        NxGeoLog::Write("poll: SelectionManager threw");
    }

    if (current.empty()) {
        int num = 0;
        tag_t* tags = nullptr;
        if (UF_UI_ask_global_sel_object_list(&num, &tags) == 0) {
            for (int i = 0; i < num && tags != nullptr; ++i) current.push_back(tags[i]);
            if (tags != nullptr) UF_free(tags);
        }
    }

    // 多选时取「最新选中」的那个，而不是列表里第一个
    const unsigned tag = NewestSelected(previous_, current);
    previous_ = current;

    if (tag == lastTag_) return;
    lastTag_ = tag;

    NxGeoLog::Write("poll: count=" + std::to_string(current.size()) +
                    " newest=" + std::to_string(tag));

    ObjRef ref;
    ref.tag = tag;
    ref.kind = ObjKind::Other;   // 真实类型由 L0 查出来
    sink_(ref);
}
