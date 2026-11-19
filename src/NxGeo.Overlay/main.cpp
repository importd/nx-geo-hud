#include <windows.h>

#include <string>

#include "OverlayWindow.h"

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
    int pid = 0;
    unsigned long long nxHwnd = 0;

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        for (int i = 1; i + 1 < argc; i += 2) {
            const std::wstring key = argv[i];
            if (key == L"--pid") {
                pid = std::stoi(argv[i + 1]);
            } else if (key == L"--hwnd") {
                nxHwnd = std::stoull(argv[i + 1]);
            }
        }
        LocalFree(argv);
    }

    if (pid == 0) return 2;

    OverlayWindow window;
    if (!window.Create(instance, nxHwnd, pid)) return 1;
    return window.Run();
}
