#include "PluginEntry.h"

#include <windows.h>

#include <string>

#include "NxGeoConst.h"
#include "NxQueryAdapter.h"
#include "QueryScheduler.h"
#include "SelectionCaptureSource.h"
#include "SnapshotChannel.h"

namespace {

SelectionCaptureSource* g_source = nullptr;
NxQueryAdapter*         g_query = nullptr;
SnapshotChannel*        g_channel = nullptr;
QueryScheduler*         g_scheduler = nullptr;
HANDLE                  g_job = nullptr;
bool                    g_running = false;

std::wstring ModuleDir() {
    HMODULE mod = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&ModuleDir), &mod);

    wchar_t buf[MAX_PATH] = { 0 };
    GetModuleFileNameW(mod, buf, MAX_PATH);

    std::wstring path(buf);
    const size_t pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? std::wstring() : path.substr(0, pos);
}

void LaunchOverlay(int pid) {
    const std::wstring exe = ModuleDir() + L"\\nxgeo-overlay.exe";
    const unsigned long long hwnd = static_cast<unsigned long long>(
        reinterpret_cast<ULONG_PTR>(GetForegroundWindow()));

    std::wstring cmd = L"\"" + exe + L"\" --pid " + std::to_wstring(pid) +
                       L" --hwnd " + std::to_wstring(hwnd);

    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        return;
    }

    g_job = CreateJobObjectW(nullptr, nullptr);
    if (g_job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(g_job, JobObjectExtendedLimitInformation, &info, sizeof info);
        AssignProcessToJobObject(g_job, pi.hProcess);
    }
    CloseHandle(pi.hThread);
}

}  // namespace

namespace PluginEntry {

void Start() {
    if (g_running) return;

    const int pid = static_cast<int>(GetCurrentProcessId());

    g_channel = new SnapshotChannel(NxGeoConst::SharedMemoryName(pid), true);
    g_channel->SetNxPid(pid);
    g_channel->SetNxHwnd(static_cast<long long>(reinterpret_cast<ULONG_PTR>(GetForegroundWindow())));

    g_query = new NxQueryAdapter();
    g_source = new SelectionCaptureSource();
    g_scheduler = new QueryScheduler(g_source, g_query, g_channel);
    g_scheduler->Start();

    LaunchOverlay(pid);
    g_running = true;
}

void Stop() {
    if (!g_running) return;

    if (g_scheduler) {
        g_scheduler->Stop();
        delete g_scheduler;
        g_scheduler = nullptr;
    }
    delete g_query;
    g_query = nullptr;
    delete g_source;
    g_source = nullptr;
    delete g_channel;
    g_channel = nullptr;

    if (g_job) {
        CloseHandle(g_job);   // KILL_ON_JOB_CLOSE: 浮窗随 NX 退出
        g_job = nullptr;
    }
    g_running = false;
}

bool IsRunning() { return g_running; }

}  // namespace PluginEntry
