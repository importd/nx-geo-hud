#include <windows.h>

#include <uf.h>

#include "PluginEntry.h"

extern "C" __declspec(dllexport) void ufsta(char* param, int* retCode, int rlen) {
    (void)param;
    (void)rlen;
    if (retCode) *retCode = 0;
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
}
