#pragma once

#include <windows.h>

#include <string>

#include "InfoViewModel.h"
#include "SnapshotChannel.h"

// 进程外浮窗：只读共享内存快照，不加载任何 NXOpen。
//
// 交互模型（尽量简单）：
//   * 默认：新对象的面板出现在「点击位置」右下方
//   * 拖动：面板跟着手走，松手后记住位置；之后新对象继续出现在这个位置
//   * 复位：回到「新对象出现在点击位置」的默认行为
//   * 关闭：隐藏，直到选中下一个对象
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
    void DrawToolbar(HDC dc);
    int  ToolbarStartX() const;
    int  ToolbarHit(int x, int y) const;
    void PerformToolbarAction(int index);
    int  ContentWidth();
    int  ContentHeight() const;
    bool ShouldStayVisible();
    HWND NxWindow();
    void SendEscapeToNx();
    void PlaceAtCursor();
    void LoadLayout();
    void SaveLayout();

    HWND             hwnd_ = nullptr;
    HWND             nxHwnd_ = nullptr;
    DWORD            nxPid_ = 0;
    SnapshotChannel* channel_ = nullptr;
    HANDLE           wakeEvent_ = nullptr;
    InfoViewModel    vm_;

    int              width_ = 320;
    int              height_ = 120;
    int              anchorX_ = INT_MIN;
    int              anchorY_ = INT_MIN;
    bool             autoPlace_ = true;      // 新对象贴在点击位置
    bool             enabled_ = true;        // Ctrl+Alt+G 总开关
    bool             dismissed_ = false;     // 点了关闭，等下一个对象
    unsigned         dismissedTag_ = 0;
    unsigned         lastTag_ = 0;
    bool             dragging_ = false;
    POINT            dragStartCursor_{};
    RECT             dragStartRect_{};

    int              lastX_ = INT_MIN;
    int              lastY_ = INT_MIN;
    int              paintedRevision_ = -1;
    std::string      lastLogged_;
};
