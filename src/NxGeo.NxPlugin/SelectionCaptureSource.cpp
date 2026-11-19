#include "SelectionCaptureSource.h"

#include <windows.h>
#undef CreateDialog   // NX UI.hxx 的方法名与 Win32 宏冲突（NX 头文件里明确要求）

#include <NXOpen/Selection.hxx>
#include <NXOpen/TaggedObject.hxx>
#include <NXOpen/UI.hxx>
#include <uf.h>
#include <uf_ui.h>

#include "Log.h"

void SelectionCaptureSource::Start(std::function<void(const ObjRef&)> sink) {
    sink_ = std::move(sink);
    running_ = true;
    thread_ = std::thread([this] { Loop(); });
}

void SelectionCaptureSource::Stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
}

void SelectionCaptureSource::Loop() {
    UF_initialize();   // 每个线程都要自己初始化 UFUN

    unsigned lastReported = 0xFFFFFFFFu;
    int lockFailures = 0;

    while (running_) {
        unsigned tagFromSelection = 0;
        unsigned tagFromGlobal = 0;
        int selectionCount = -1;
        int globalCount = -1;

        if (UF_UI_lock_ug_access(UF_UI_FROM_CUSTOM) == UF_UI_LOCK_SET) {
            lockFailures = 0;
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
                // 选择列表读失败时按"没选"处理
            }

            int num = 0;
            tag_t* tags = nullptr;
            if (UF_UI_ask_global_sel_object_list(&num, &tags) == 0) {
                globalCount = num;
                if (num > 0 && tags != nullptr) tagFromGlobal = tags[0];
                if (tags != nullptr) UF_free(tags);
            }

            UF_UI_unlock_ug_access(UF_UI_FROM_CUSTOM);
        } else {
            ++lockFailures;
            if (lockFailures == 1 || lockFailures % 200 == 0) {
                NxGeoLog::Write("poll: UF_UI_lock_ug_access failed x" + std::to_string(lockFailures));
            }
        }

        const unsigned tag = tagFromSelection != 0 ? tagFromSelection : tagFromGlobal;

        if (tag != lastReported) {
            NxGeoLog::Write("poll: selCount=" + std::to_string(selectionCount) +
                            " globalCount=" + std::to_string(globalCount) +
                            " selTag=" + std::to_string(tagFromSelection) +
                            " globalTag=" + std::to_string(tagFromGlobal));
            lastReported = tag;
        }

        ObjRef ref;
        ref.tag = tag;
        ref.kind = ObjKind::Other;   // 真实类型由 L0 查出来
        if (sink_) sink_(ref);

        Sleep(50);
    }

    UF_terminate();
}
