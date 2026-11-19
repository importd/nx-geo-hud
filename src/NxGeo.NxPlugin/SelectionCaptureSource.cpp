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
        unsigned tag = 0;

        if (UF_UI_lock_ug_access(UF_UI_FROM_CUSTOM) == UF_UI_LOCK_SET) {
            lockFailures = 0;
            try {
                NXOpen::Selection* sel = NXOpen::UI::GetUI()->SelectionManager();
                if (sel != nullptr && sel->GetNumSelectedObjects() > 0) {
                    NXOpen::TaggedObject* obj = sel->GetSelectedTaggedObject(0);
                    if (obj != nullptr) tag = obj->Tag();
                }
            } catch (...) {
                tag = 0;   // 选择列表读失败时按"没选"处理，浮窗隐藏
            }
            UF_UI_unlock_ug_access(UF_UI_FROM_CUSTOM);
        } else {
            ++lockFailures;
            if (lockFailures == 1 || lockFailures % 200 == 0) {
                NxGeoLog::Write("poll: UF_UI_lock_ug_access failed x" + std::to_string(lockFailures));
            }
        }

        if (tag != lastReported) {
            NxGeoLog::Write("poll: selection tag=" + std::to_string(tag));
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
