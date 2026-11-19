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
    void RenderContent(HDC dc);
    void ShowNotice();
    static int RowsForKind(unsigned kind);
    void ReleaseCache();
    void DrawToolbar(HDC dc);
    int  ToolbarStartX() const;
    int  ToolbarHit(int x, int y) const;
    void PerformToolbarAction(int index);
    bool ShouldStayVisible();
    HWND NxWindow();
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
    int              autoX_ = INT_MIN;       // 上次自动出现的位置（复位回这里）
    int              autoY_ = INT_MIN;
    int              anchorX_ = INT_MIN;
    int              anchorY_ = INT_MIN;
    bool             autoPlace_ = true;      // 新对象贴在点击位置
    bool             enabled_ = true;        // Ctrl+Alt+G 总开关
    bool             dismissed_ = false;     // 点了关闭，等下一个对象
    unsigned         lastTag_ = 0;
    int              hoverButton_ = -1;      // 悬停的按钮下标
    int              pressedButton_ = -1;    // 按下的按钮下标
    bool             trackingLeave_ = false;
    bool             dragging_ = false;
    bool             dragMoved_ = false;
    POINT            dragStartCursor_{};
    RECT             dragStartRect_{};

    int              lastX_ = INT_MIN;
    int              lastY_ = INT_MIN;
    int              paintedRevision_ = -1;
    DWORD            hideSince_ = 0;         // 连续不可见计时，避免点击瞬间闪一下
    bool             noticeActive_ = false;  // 启动提示：显示几秒
    DWORD            noticeUntil_ = 0;
    bool             showingNotice_ = false;

    // 画面缓存：内容或尺寸没变就直接贴位图，拖动时不再逐帧重绘
    HBITMAP          cache_ = nullptr;
    HDC              cacheDc_ = nullptr;
    HGDIOBJ          cacheOldBitmap_ = nullptr;
    int              cacheW_ = 0;
    int              cacheH_ = 0;
    int              cacheRevision_ = -1;
    std::string      lastLogged_;
};
