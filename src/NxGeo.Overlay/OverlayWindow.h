#pragma once

#include <windows.h>

#include <string>

#include "InfoViewModel.h"
#include "SnapshotChannel.h"

// 进程外浮窗：只读共享内存快照，不加载任何 NXOpen。
class OverlayWindow {
public:
    bool Create(HINSTANCE instance, unsigned long long nxHwnd, int nxPid);
    // 钉住的副本：内容冻结，不读共享内存，双击关闭
    bool CreateFrozen(HINSTANCE instance, int x, int y, const InfoSnapshot& snapshot);
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
    void CopyToClipboard();
    void ShowTrayMenu();
    void AddTrayIcon();
    void RemoveTrayIcon();
    void LoadLayout();
    void SaveLayout();
    void ResetToDefaultPosition();
    void Close();
    void DrawToolbar(HDC dc);
    int  ToolbarStartX() const;
    int  ToolbarHit(int x, int y) const;
    void PerformToolbarAction(int index);
    void TogglePin();
    void SendEscapeToNx();
    bool IsClosed() const { return closed_; }

    HWND             hwnd_ = nullptr;
    HWND             nxHwnd_ = nullptr;
    DWORD            nxPid_ = 0;
    SnapshotChannel* channel_ = nullptr;
    HANDLE           wakeEvent_ = nullptr;
    InfoViewModel    vm_;
    int              width_ = 360;
    int              height_ = 120;
    bool             clickThrough_ = false;   // 默认接收鼠标，右键才能出菜单
    bool             enabled_ = true;
    bool             pinned_ = false;
    bool             followCursor_ = false;   // 默认固定位置，不追鼠标
    bool             dragging_ = false;
    POINT            dragStartCursor_{};
    RECT             dragStartRect_{};
    int              anchorX_ = INT_MIN;      // 固定模式的左上角
    int              anchorY_ = INT_MIN;
    bool             autoPlace_ = true;       // 新对象时自动贴到点击位置；手动拖动后不再自动
    unsigned         lastTag_ = 0;

    bool             frozen_ = false;         // 钉住的副本
    bool             closed_ = false;
    InfoSnapshot     frozenSnapshot_{};
    InfoSnapshot     lastSnapshot_{};
    std::string      lastLogged_;
    int              paintedRevision_ = -1;
    int              lastX_ = INT_MIN;
    int              lastY_ = INT_MIN;
    NOTIFYICONDATAW  tray_{};
};
