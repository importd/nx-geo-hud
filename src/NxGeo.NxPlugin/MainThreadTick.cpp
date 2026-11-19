#include "MainThreadTick.h"

#include <string>

#include "Log.h"

namespace {

constexpr UINT_PTR kTimerId = 0x4E584731;   // "NXG1"

HWND                  g_window = nullptr;
WNDPROC               g_previous = nullptr;
std::function<void()> g_tick;

LRESULT CALLBACK HookProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_TIMER && wp == kTimerId) {
        try {
            if (g_tick) g_tick();
        } catch (...) {
            NxGeoLog::Write("tick: exception swallowed");
        }
        return 0;
    }
    return CallWindowProcW(g_previous, hwnd, msg, wp, lp);
}

}  // namespace

namespace MainThreadTick {

bool Install(HWND nxWindow, UINT intervalMs, std::function<void()> tick) {
    if (g_window != nullptr) return true;
    if (nxWindow == nullptr || !IsWindow(nxWindow)) {
        NxGeoLog::Write("tick: install failed, invalid window");
        return false;
    }

    g_tick = std::move(tick);
    g_previous = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(nxWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&HookProc)));
    if (g_previous == nullptr) {
        NxGeoLog::Write("tick: subclass failed");
        return false;
    }

    SetTimer(nxWindow, kTimerId, intervalMs, nullptr);
    g_window = nxWindow;
    NxGeoLog::Write("tick: installed on hwnd=" + std::to_string(static_cast<unsigned long long>(
                                              reinterpret_cast<ULONG_PTR>(nxWindow))));
    return true;
}

void Remove() {
    if (g_window == nullptr) return;

    KillTimer(g_window, kTimerId);
    if (g_previous != nullptr) {
        SetWindowLongPtrW(g_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_previous));
    }

    g_window = nullptr;
    g_previous = nullptr;
    g_tick = nullptr;
    NxGeoLog::Write("tick: removed");
}

bool IsInstalled() { return g_window != nullptr; }

}  // namespace MainThreadTick
