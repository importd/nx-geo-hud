// 独立悬停探针：不参与插件启动，只作为「文件 → 执行 → NX Open」的一次性命令。
// 目的：验证 UF_UI_select_with_single_dialog 的 filter 回调是否在鼠标悬停时触发。

#include <windows.h>

#include <string>

#include <uf.h>
#include <uf_ui.h>

#include "Log.h"

namespace {

int SelFilter(tag_t object, int type[3], void* /*user_data*/, UF_UI_selection_p_t /*select_*/) {
    NxGeoLog::Write("probe hover: tag=" + std::to_string(static_cast<unsigned>(object)) +
                    " type=" + std::to_string(type[0]) + "/" + std::to_string(type[1]));
    return UF_UI_SEL_REJECT;   // 只观察，不选中
}

int SelInit(UF_UI_selection_p_t select_, void* user_data) {
    return UF_UI_set_sel_procs(select_, SelFilter, nullptr, user_data) == 0
               ? UF_UI_SEL_SUCCESS
               : UF_UI_SEL_FAILURE;
}

}  // namespace

extern "C" __declspec(dllexport) void ufsta(char* /*param*/, int* retCode, int /*rlen*/) {
    if (retCode) *retCode = 0;   // 被 startup 目录加载时什么都不做
}

extern "C" __declspec(dllexport) void ufusr(char* /*param*/, int* retCode, int /*paramLen*/) {
    if (retCode) *retCode = 0;

    UF_initialize();
    NxGeoLog::Write("probe: entering pick mode");

    int response = 0;
    tag_t object = NULL_TAG;
    tag_t view = NULL_TAG;
    double cursor[3] = { 0.0, 0.0, 0.0 };

    const int rc = UF_UI_select_with_single_dialog(
        const_cast<char*>("悬停验证：把鼠标移到对象上，按 Esc 退出"),
        nullptr, UF_UI_SEL_SCOPE_WORK_PART, SelInit, nullptr, &response, &object, cursor, &view);

    NxGeoLog::Write("probe: exited rc=" + std::to_string(rc));
    UF_terminate();
}

extern "C" __declspec(dllexport) int ufusr_ask_unload(void) {
    return UF_UNLOAD_IMMEDIATELY;
}
