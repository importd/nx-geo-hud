// 独立悬停探针 v3：
//  1) filter 回调接收悬停对象（已验证：悬停即触发）
//  2) 后台线程找 NX 弹出的「名称」对话框并隐藏（定时器在模态循环里不派发，只能用线程）
//  3) 按 Esc 退出

#include <windows.h>

#include <atomic>
#include <string>
#include <thread>

#include <uf.h>
#include <uf_ui.h>

#include "Log.h"

namespace {

std::atomic<bool> g_running{ false };
std::thread       g_watcher;

int SelFilter(tag_t object, int type[3], void* /*user_data*/, UF_UI_selection_p_t /*select_*/) {
    NxGeoLog::Write("probe hover: tag=" + std::to_string(static_cast<unsigned>(object)) +
                    " type=" + std::to_string(type[0]) + "/" + std::to_string(type[1]));
    return UF_UI_SEL_REJECT;
}

int SelInit(UF_UI_selection_p_t select_, void* user_data) {
    return UF_UI_set_sel_procs(select_, SelFilter, nullptr, user_data) == 0
               ? UF_UI_SEL_SUCCESS
               : UF_UI_SEL_FAILURE;
}

std::string WideToUtf8(const wchar_t* text) {
    if (text == nullptr) return std::string();
    const int len = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 1) return std::string();
    std::string out(static_cast<size_t>(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, &out[0], len, nullptr, nullptr);
    return out;
}

struct ScanContext {
    DWORD       pid;
    std::string summary;
};

BOOL CALLBACK ScanProc(HWND hwnd, LPARAM param) {
    auto* ctx = reinterpret_cast<ScanContext*>(param);

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != ctx->pid || !IsWindowVisible(hwnd)) return TRUE;

    wchar_t title[256] = { 0 };
    GetWindowTextW(hwnd, title, 255);
    wchar_t cls[256] = { 0 };
    GetClassNameW(hwnd, cls, 255);

    const std::string titleUtf8 = WideToUtf8(title);
    ctx->summary += "[" + WideToUtf8(cls) + "|" + titleUtf8 + "]";

    // NX 的单选对话框：标题「名称」，类名 UGS::...；隐藏它，用户就能直接在图形区悬停
    if (titleUtf8 == "名称" || titleUtf8 == "Name") {
        ShowWindow(hwnd, SW_HIDE);
        NxGeoLog::Write("probe: hid dialog " + WideToUtf8(cls) + " | " + titleUtf8);
    }
    return TRUE;
}

void WatchDialogs() {
    std::string last;
    while (g_running.load()) {
        ScanContext ctx{ GetCurrentProcessId(), std::string() };
        EnumWindows(&ScanProc, reinterpret_cast<LPARAM>(&ctx));
        if (ctx.summary != last) {
            NxGeoLog::Write("probe windows: " + ctx.summary);
            last = ctx.summary;
        }
        Sleep(150);
    }
}

}  // namespace

extern "C" __declspec(dllexport) void ufsta(char* /*param*/, int* retCode, int /*rlen*/) {
    if (retCode) *retCode = 0;
}

extern "C" __declspec(dllexport) void ufusr(char* /*param*/, int* retCode, int /*paramLen*/) {
    if (retCode) *retCode = 0;

    UF_initialize();

    g_running = true;
    g_watcher = std::thread(WatchDialogs);
    NxGeoLog::Write("probe v3: entering pick mode");

    int response = 0;
    tag_t object = NULL_TAG;
    tag_t view = NULL_TAG;
    double cursor[3] = { 0.0, 0.0, 0.0 };

    const int rc = UF_UI_select_with_single_dialog(
        const_cast<char*>("悬停验证：把鼠标移到对象上，按 Esc 退出"),
        nullptr, UF_UI_SEL_SCOPE_WORK_PART, SelInit, nullptr, &response, &object, cursor, &view);

    NxGeoLog::Write("probe v3: exited rc=" + std::to_string(rc));

    g_running = false;
    if (g_watcher.joinable()) g_watcher.join();
    UF_terminate();
}

extern "C" __declspec(dllexport) int ufusr_ask_unload(void) {
    return UF_UNLOAD_IMMEDIATELY;
}
