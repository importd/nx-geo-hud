#include "PluginEntry.h"

#include <windows.h>

#include <memory>
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
HANDLE                  g_wakeEvent = nullptr;   // 写快照后通知浮窗
HANDLE                  g_overlayProcess = nullptr;   // 看门狗用
DWORD                   g_lastRelaunch = 0;
bool                    g_running = false;

// L2 任务：只保留最新一个，并且延迟执行（快速连点时根本不重算）
std::unique_ptr<QueryScheduler::Task> g_pendingTask;
DWORD g_pendingDue = 0;
constexpr DWORD kL2DebounceMs = 120;

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

    if (g_job != nullptr) {
        CloseHandle(g_job);
        g_job = nullptr;
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

// 主线程 tick：轮询选择（L0+L1，必须快）+ 一个去抖后的 L2
// 查询性能计数器，毫秒（浮点）：GetTickCount 只有 15.6ms 精度，会把 1ms 报成 16ms
double NowMs() {
    static LARGE_INTEGER frequency = { 0 };
    if (frequency.QuadPart == 0) QueryPerformanceFrequency(&frequency);
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return static_cast<double>(counter.QuadPart) * 1000.0 / static_cast<double>(frequency.QuadPart);
}

std::string Ms(double value) {
    char buf[32];
    snprintf(buf, sizeof buf, "%.2f", value);
    return std::string(buf);
}

DWORD g_nextPollAt = 0;   // 轮询自适应退避：贵的时候降低频率

void Tick() {
    WatchOverlay();

    double pollms = 0.0;
    double t1 = NowMs();
    const DWORD now = GetTickCount();
    if (g_source != nullptr && now >= g_nextPollAt) {
        const double t0 = NowMs();
        g_source->PollOnce();
        t1 = NowMs();
        pollms = t1 - t0;
        // 一次轮询超过 4ms（通常意味着选中了几十个对象），就把节奏降到 100ms；
        // 否则维持 50ms。人眼对 100ms 的选择延迟基本无感，但主线程占用减半。
        g_nextPollAt = GetTickCount() + (pollms > 4.0 ? 100 : 50);
    }

    // 去抖：选择稳定 120ms 后才算质量属性/距离，快速连点时不重算
    double l2ms = 0.0;
    if (g_pendingTask && static_cast<long>(GetTickCount() - g_pendingDue) >= 0) {
        std::unique_ptr<QueryScheduler::Task> task = std::move(g_pendingTask);
        (*task)();
        l2ms = NowMs() - t1;
    }

    if (pollms + l2ms > 8.0) {   // 只在偏慢时留痕，正常不写日志
        NxGeoLog::Write("slow: poll=" + Ms(pollms) + "ms l2=" + Ms(l2ms) + "ms");
    }
}

}  // namespace

namespace PluginEntry {

void Start() {
    if (g_running) return;

    const int pid = static_cast<int>(GetCurrentProcessId());

    g_channel = new SnapshotChannel(NxGeoConst::SharedMemoryName(pid), true);

    // 自动复位事件：每次写入快照就唤醒浮窗，浮窗不用空转轮询
    {
        const std::string name = NxGeoConst::EventName(pid);
        const std::wstring wideName(name.begin(), name.end());
        g_wakeEvent = CreateEventW(nullptr, FALSE, FALSE, wideName.c_str());
        g_channel->SetWakeEvent(g_wakeEvent);
    }
    g_channel->SetNxPid(pid);
    g_channel->SetNxHwnd(static_cast<long long>(reinterpret_cast<ULONG_PTR>(GetForegroundWindow())));
    NxGeoLog::Write(std::string("channel ") + (g_channel->IsValid() ? "ok " : "FAILED ") +
                    NxGeoConst::SharedMemoryName(pid));

    g_query = new NxQueryAdapter();
    g_source = new SelectionCaptureSource();
    g_scheduler = new QueryScheduler(g_source, g_query, g_channel,
                                     [](QueryScheduler::Task task) {
                                         g_pendingTask.reset(new QueryScheduler::Task(std::move(task)));
                                         g_pendingDue = GetTickCount() + kL2DebounceMs;
                                     });
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
    g_pendingTask.reset();

    if (g_job) {
        CloseHandle(g_job);   // KILL_ON_JOB_CLOSE: 浮窗随 NX 退出
        g_job = nullptr;
    }
    g_running = false;
    NxGeoLog::Write("PluginEntry::Stop");
}

bool IsRunning() { return g_running; }

}  // namespace PluginEntry
