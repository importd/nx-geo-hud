#pragma once

#include <windows.h>

#include <string>

#include "InfoViewModel.h"
#include "SnapshotChannel.h"

// 进程外浮窗：只读共享内存快照，不加载任何 NXOpen。
class OverlayWindow {
public:
    bool Create(HINSTANCE instance, unsigned long long nxHwnd, int nxPid);
    int  Run();

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp);

    void Tick();
    void Reposition();
    void Paint(HDC dc);
    int  ContentHeight() const;
    int  ContentWidth();
    bool ShouldStayVisible();
    HWND NxWindow();

    HWND             hwnd_ = nullptr;
    HWND             nxHwnd_ = nullptr;
    DWORD            nxPid_ = 0;
    SnapshotChannel* channel_ = nullptr;
    HANDLE           wakeEvent_ = nullptr;
    InfoViewModel    vm_;
    int              width_ = 360;
    int              height_ = 120;
    bool             clickThrough_ = true;
    bool             enabled_ = true;
    std::string      lastLogged_;
    int              paintedRevision_ = -1;
    int              lastX_ = INT_MIN;
    int              lastY_ = INT_MIN;
};
