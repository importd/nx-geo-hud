#include "PluginEntry.h"

#include <windows.h>

#include <deque>
#include <string>

#include "Log.h"
#include "MainThreadTick.h"
#include "NxGeoConst.h"
#include "NxQueryAdapter.h"
#include "QueryScheduler.h"
#include "SelectionCaptureSource.h"
#include "SnapshotChannel.h"

#include <uf.h>

namespace {

SelectionCaptureSource* g_source = nullptr;
NxQueryAdapter*         g_query = nullptr;
SnapshotChannel*        g_channel = nullptr;
QueryScheduler*         g_scheduler = nullptr;
HANDLE                  g_job = nullptr;
HANDLE                  g_overlayProcess = nullptr;   // 看门狗用
DWORD                   g_lastRelaunch = 0;
bool                    g_running = false;

// L2 任务在主线程 tick 里一个个跑，绝不开后台线程调 NX
std::deque<QueryScheduler::Task> g_tasks;

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
        NxGeoLog::Write("launch overlay FAILED: " + std::to_string(GetLastError()));
        return;
    }
    NxGeoLog::Write("overlay launched, pid=" + std::to_string(pi.dwProcessId));

    if (g_overlayProcess != nullptr) CloseHandle(g_overlayProcess);
    g_overlayProcess = pi.hProcess;   // 留给看门狗检查

    g_job = CreateJobObjectW(nullptr, nullptr);
    if (g_job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(g_job, JobObjectExtendedLimitInformation, &info, sizeof info);
        AssignProcessToJobObject(g_job, pi.hProcess);
    }
    CloseHandle(pi.hThread);
}

// 浮窗进程挂了（崩溃/被结束）就自动重新拉起来，否则快捷键和面板一起消失
void WatchOverlay() {
    if (g_overlayProcess == nullptr) return;
    if (WaitForSingleObject(g_overlayProcess, 0) != WAIT_OBJECT_0) return;   // 还活着

    CloseHandle(g_overlayProcess);
    g_overlayProcess = nullptr;

    const DWORD now = GetTickCount();
    if (now - g_lastRelaunch < 3000) return;   // 连续崩就不停重启
    g_lastRelaunch = now;

    NxGeoLog::Write("watchdog: overlay exited, relaunching");
    LaunchOverlay(static_cast<int>(GetCurrentProcessId()));
}

// 主线程 tick：轮询选择 + 跑一个挂起的 L2
void Tick() {
    WatchOverlay();

    if (g_source != nullptr) g_source->PollOnce();

    if (!g_tasks.empty()) {
        QueryScheduler::Task task = std::move(g_tasks.front());
        g_tasks.pop_front();
        task();
    }
}

}  // namespace

namespace PluginEntry {

void Start() {
    if (g_running) return;

    const int pid = static_cast<int>(GetCurrentProcessId());

    g_channel = new SnapshotChannel(NxGeoConst::SharedMemoryName(pid), true);
    g_channel->SetNxPid(pid);
    g_channel->SetNxHwnd(static_cast<long long>(reinterpret_cast<ULONG_PTR>(GetForegroundWindow())));
    NxGeoLog::Write(std::string("channel ") + (g_channel->IsValid() ? "ok " : "FAILED ") +
                    NxGeoConst::SharedMemoryName(pid));

    g_query = new NxQueryAdapter();
    g_source = new SelectionCaptureSource();
    g_scheduler = new QueryScheduler(g_source, g_query, g_channel,
                                     [](QueryScheduler::Task task) { g_tasks.push_back(std::move(task)); });
    g_scheduler->Start();

    if (!MainThreadTick::Install(50, [] { Tick(); })) {
        NxGeoLog::Write("tick install failed, plugin will not poll");
    }

    LaunchOverlay(pid);
    g_running = true;
    NxGeoLog::Write("PluginEntry::Start ok, pid=" + std::to_string(pid));
}

void Stop() {
    if (!g_running) return;

    MainThreadTick::Remove();

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
    g_tasks.clear();

    if (g_job) {
        CloseHandle(g_job);   // KILL_ON_JOB_CLOSE: 浮窗随 NX 退出
        g_job = nullptr;
    }
    g_running = false;
    NxGeoLog::Write("PluginEntry::Stop");
}

bool IsRunning() { return g_running; }

}  // namespace PluginEntry
