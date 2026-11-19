#include <windows.h>

#include <uf.h>

#include "Log.h"
#include "PluginEntry.h"

extern "C" __declspec(dllexport) void ufsta(char* param, int* retCode, int rlen) {
    (void)param;
    (void)rlen;
    if (retCode) *retCode = 0;

    const int status = UF_initialize();
    NxGeoLog::Write("ufsta: UF_initialize status=" + std::to_string(status));

    PluginEntry::Start();
}

extern "C" __declspec(dllexport) void ufusr(char* param, int* retCode, int paramLen) {
    ufsta(param, retCode, paramLen);
}

extern "C" __declspec(dllexport) int ufusr_ask_unload(void) {
    return UF_UNLOAD_UG_TERMINATE;
}

extern "C" __declspec(dllexport) void ufusr_cleanup(void) {
    PluginEntry::Stop();

    const int status = UF_terminate();
    NxGeoLog::Write("ufusr_cleanup: UF_terminate status=" + std::to_string(status));
}
