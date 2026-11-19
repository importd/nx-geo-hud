#include "MainThreadTick.h"

#include <string>

#include "Log.h"

namespace {

constexpr UINT_PTR kTimerId = 0x4E584731;   // "NXG1"
const wchar_t    kClassName[] = L"NxGeoMainThreadTick";

HWND                  g_window = nullptr;
std::function<void()> g_tick;

LRESULT CALLBACK TickProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_TIMER && wp == kTimerId) {
        try {
            if (g_tick) g_tick();
        } catch (...) {
            NxGeoLog::Write("tick: exception swallowed");
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

namespace MainThreadTick {

bool Install(UINT intervalMs, std::function<void()> tick) {
    if (g_window != nullptr) return true;

    HINSTANCE instance = GetModuleHandleW(nullptr);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = &TickProc;
    wc.hInstance = instance;
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        NxGeoLog::Write("tick: RegisterClass failed " + std::to_string(GetLastError()));
        return false;
    }

    // message-only 窗口：不显示、不占任务栏，但能收到本线程派发的 WM_TIMER
    g_window = CreateWindowExW(0, kClassName, L"", 0, 0, 0, 0, 0,
                               HWND_MESSAGE, nullptr, instance, nullptr);
    if (g_window == nullptr) {
        NxGeoLog::Write("tick: CreateWindow failed " + std::to_string(GetLastError()));
        return false;
    }

    g_tick = std::move(tick);
    SetTimer(g_window, kTimerId, intervalMs, nullptr);
    NxGeoLog::Write("tick: installed on message window, interval=" + std::to_string(intervalMs) + "ms");
    return true;
}

void Remove() {
    if (g_window == nullptr) return;

    KillTimer(g_window, kTimerId);
    DestroyWindow(g_window);
    g_window = nullptr;
    g_tick = nullptr;
    NxGeoLog::Write("tick: removed");
}

bool IsInstalled() { return g_window != nullptr; }

}  // namespace MainThreadTick
