#pragma once

#include <windows.h>

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
    bool ShouldStayVisible() const;

    HWND             hwnd_ = nullptr;
    HWND             nxHwnd_ = nullptr;
    SnapshotChannel* channel_ = nullptr;
    HANDLE           wakeEvent_ = nullptr;
    InfoViewModel    vm_;
    int              width_ = 340;
    int              height_ = 120;
    bool             clickThrough_ = true;
    bool             enabled_ = true;
};
