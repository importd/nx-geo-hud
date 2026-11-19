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

    // 先用 UFUN 一次性拿全局选择列表：多选十几个对象时，
    // NXOpen 的逐对象虚调用要 16ms，而这是一次调用 + 一次释放。
    int num = 0;
    tag_t* tags = nullptr;
    if (UF_UI_ask_global_sel_object_list(&num, &tags) == 0) {
        if (num > 0 && tags != nullptr) {
            current.reserve(static_cast<size_t>(num));
            for (int i = 0; i < num; ++i) current.push_back(tags[i]);
        }
        if (tags != nullptr) UF_free(tags);
    }

    if (current.empty()) {   // 兜底：某些上下文下 UFUN 拿不到
        NxGeoLog::Write("poll: UFUN returned nothing, falling back to NXOpen");
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
