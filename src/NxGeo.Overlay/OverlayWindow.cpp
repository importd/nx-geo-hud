#include "OverlayWindow.h"

#include <cstdlib>
#include <cstring>

#include <windowsx.h>
#include <fstream>
#include <string>

#include "Log.h"
#include "Native.h"
#include "NxGeoConst.h"
#include "ObjKind.h"
#include "OverlayPlacement.h"
#include "OverlayState.h"

namespace {

constexpr int kRowHeight = 18;
constexpr int kPadding = 6;
constexpr int kTitleWidth = 64;
constexpr int kToolbarHeight = 20;
constexpr int kButton = 16;
constexpr int kButtonGap = 2;
constexpr int kButtonCount = 2;          // 复位 / 关闭
// 固定窗口尺寸：不做内容自适应，避免内容分 L0/L1/L2 三步到达时窗口「一点点长大」
constexpr int kPanelWidth = 340;
constexpr int kPanelHeight = 20 + 6 * 2 + 12 * 18;   // 工具条 + 上边距 + 最多 12 行
constexpr wchar_t kClassName[] = L"NxGeoOverlayWindow";
constexpr BYTE kAlpha = 170;

std::wstring Utf8ToWide(const std::string& text) {
    if (text.empty()) return std::wstring();
    const int len = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (len <= 1) return std::wstring();
    std::wstring out(static_cast<size_t>(len - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, &out[0], len);
    return out;
}

std::wstring ExeDir() {
    wchar_t buffer[MAX_PATH] = { 0 };
    GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    std::wstring path(buffer);
    const size_t pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? std::wstring() : path.substr(0, pos);
}

struct FindWindowContext {
    DWORD pid;
    HWND  result;
};

BOOL CALLBACK FindMainWindowProc(HWND hwnd, LPARAM param) {
    auto* context = reinterpret_cast<FindWindowContext*>(param);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == context->pid && IsWindowVisible(hwnd) && GetWindow(hwnd, GW_OWNER) == nullptr) {
        context->result = hwnd;
        return FALSE;
    }
    return TRUE;
}

HWND FindMainWindowForPid(DWORD pid) {
    FindWindowContext context{ pid, nullptr };
    EnumWindows(&FindMainWindowProc, reinterpret_cast<LPARAM>(&context));
    return context.result;
}


}  // namespace

bool OverlayWindow::Create(HINSTANCE instance, unsigned long long nxHwnd, int nxPid) {
    nxPid_ = static_cast<DWORD>(nxPid);
    nxHwnd_ = reinterpret_cast<HWND>(nxHwnd);
    if (nxHwnd_ == nullptr || !IsWindow(nxHwnd_)) {
        nxHwnd_ = FindMainWindowForPid(nxPid_);
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = &OverlayWindow::WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

    hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
                            kClassName, L"", WS_POPUP,
                            0, 0, width_, height_, nullptr, nullptr, instance, this);
    if (hwnd_ == nullptr) return false;

    ApplyNoActivateStyles(hwnd_, false);
    ApplyTranslucency(hwnd_, kAlpha);

    if (nxHwnd_ != nullptr && IsWindow(nxHwnd_)) {
        SetWindowLongPtrW(hwnd_, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(nxHwnd_));
    }

    if (!RegisterHotKey(hwnd_, 1, MOD_CONTROL | MOD_ALT, 'G')) {
        NxGeoLog::Write("hotkey toggle FAILED (Ctrl+Alt+G 被占用)");
    }

    LoadLayout();

    noticeActive_ = true;
    noticeUntil_ = GetTickCount() + 3500;   // 显示 3.5 秒

    channel_ = new SnapshotChannel(NxGeoConst::SharedMemoryName(static_cast<int>(nxPid_)), false);
    wakeEvent_ = OpenEventW(SYNCHRONIZE, FALSE, Utf8ToWide(NxGeoConst::EventName(static_cast<int>(nxPid_))).c_str());

    NxGeoLog::Write("overlay created: nxpid=" + std::to_string(nxPid_) +
                    " channel=" + (channel_->IsValid() ? "ok" : "MISSING"));
    return true;
}

void OverlayWindow::ShowNotice() {
    const int width = 300;
    const int height = 36;

    const HWND nx = NxWindow();
    RECT nxRect{ 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
    if (nx != nullptr) GetWindowRect(nx, &nxRect);

    const int x = nxRect.left + ((nxRect.right - nxRect.left) - width) / 2;
    const int y = nxRect.top + ((nxRect.bottom - nxRect.top) - height) / 2;

    showingNotice_ = true;
    if (width != width_ || height != height_) cacheRevision_ = -1;
    width_ = width;
    height_ = height;
    lastX_ = x;
    lastY_ = y;

    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, width_, height_,
                 SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_SHOWWINDOW);
    InvalidateRect(hwnd_, nullptr, FALSE);
    UpdateWindow(hwnd_);
}

void OverlayWindow::PlaceAtCursor() {
    POINT cursor{};
    GetCursorPos(&cursor);

    const HWND nx = NxWindow();
    RECT nxRect{ 0, 0, 0, 0 };
    if (nx != nullptr) GetWindowRect(nx, &nxRect);

    const Placement p = ComputePlacement(cursor.x, cursor.y, width_, height_,
                                         nxRect.left, nxRect.top, nxRect.right, nxRect.bottom);
    anchorX_ = p.x;
    anchorY_ = p.y;
    autoX_ = p.x;      // 记住这个位置，复位就回这里
    autoY_ = p.y;
    lastX_ = INT_MIN;
    lastY_ = INT_MIN;
}

void OverlayWindow::Reposition() {
    const HWND nx = NxWindow();
    if (nx == nullptr) return;

    RECT nxRect{};
    GetWindowRect(nx, &nxRect);

    if (anchorX_ == INT_MIN || anchorY_ == INT_MIN) {
        const Placement fallback = ComputePlacement(0, 0, width_, height_,
                                                    nxRect.left, nxRect.top, nxRect.right, nxRect.bottom);
        anchorX_ = fallback.x;
        anchorY_ = fallback.y;
    }

    int x = anchorX_;
    int y = anchorY_;
    if (x < nxRect.left + 4) x = nxRect.left + 4;
    if (y < nxRect.top + 4) y = nxRect.top + 4;
    if (x + width_ > nxRect.right - 4) x = nxRect.right - width_ - 4;
    if (y + height_ > nxRect.bottom - 4) y = nxRect.bottom - height_ - 4;
    anchorX_ = x;
    anchorY_ = y;

    if (x != lastX_ || y != lastY_) {
        lastX_ = x;
        lastY_ = y;
        SetWindowPos(hwnd_, HWND_TOPMOST, x, y, 0, 0,
                     SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    }
}

void OverlayWindow::Tick() {
    if (channel_ == nullptr || !enabled_) return;

    InfoSnapshot snapshot{};
    const bool readOk = channel_->TryRead(snapshot);
    const bool mismatch = channel_->VersionMismatch();
    const bool isEmpty = readOk && snapshot.kind == static_cast<unsigned short>(ObjKind::None);
    const OverlayState state = DecideOverlayState(readOk, mismatch, isEmpty);

    const std::string desc = "tick: state=" + std::to_string(static_cast<int>(state)) +
                             " kind=" + std::to_string(snapshot.kind) +
                             " tag=" + std::to_string(snapshot.tag);
    if (desc != lastLogged_) {
        NxGeoLog::Write(desc);
        lastLogged_ = desc;
    }

    // 启动提示：几秒后自动消失，或者用户一开始选东西就消失
    if (noticeActive_) {
        const bool expired = GetTickCount() > noticeUntil_;
        const bool used = (state == OverlayState::ShowSnapshot) && snapshot.tag != 0;
        if (expired || used) {
            noticeActive_ = false;
            showingNotice_ = false;
            cacheRevision_ = -1;
        } else {
            ShowNotice();
            return;
        }
    }

    switch (state) {
        case OverlayState::Hide:
            ShowWindow(hwnd_, SW_HIDE);
            return;
        case OverlayState::ShowMessage:
            vm_.ShowMessage("协议版本不符，请重启 NX");
            break;
        case OverlayState::ShowSnapshot: {
            if (snapshot.tag != lastTag_) {
                lastTag_ = snapshot.tag;
                dismissed_ = false;
                if (autoPlace_) PlaceAtCursor();
            }
            if (dismissed_) {
                ShowWindow(hwnd_, SW_HIDE);
                return;
            }
            vm_.Apply(snapshot);
            break;
        }
    }

    if (!ShouldStayVisible()) {
        // NX 点击的瞬间前台会短暂抖动，连续不可见 250ms 才真的隐藏，否则会闪一下
        if (hideSince_ == 0) hideSince_ = GetTickCount();
        if (GetTickCount() - hideSince_ < 250) return;
        ShowWindow(hwnd_, SW_HIDE);
        return;
    }
    hideSince_ = 0;

    // 固定尺寸：只有从「启动提示」切回信息面板时才需要改一次
    if (width_ != kPanelWidth || height_ != kPanelHeight) {
        width_ = kPanelWidth;
        height_ = kPanelHeight;
        SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, width_, height_,
                     SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        lastX_ = INT_MIN;
        lastY_ = INT_MIN;
        cacheRevision_ = -1;
    }

    // 先定尺寸和位置，再显示并立刻画完，避免「先出现在旧位置再跳」
    const bool wasVisible = IsWindowVisible(hwnd_) != FALSE;
    Reposition();

    if (!wasVisible) {
        ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
        InvalidateRect(hwnd_, nullptr, FALSE);
        UpdateWindow(hwnd_);
    } else if (vm_.Revision() != paintedRevision_) {
        // 尺寸可能刚变过：立刻同步重画，否则会露出一帧空白
        InvalidateRect(hwnd_, nullptr, FALSE);
        UpdateWindow(hwnd_);
    }
}

HWND OverlayWindow::NxWindow() {
    if (nxHwnd_ != nullptr && IsWindow(nxHwnd_)) {
        DWORD pid = 0;
        GetWindowThreadProcessId(nxHwnd_, &pid);
        if (pid == nxPid_) return nxHwnd_;
    }
    nxHwnd_ = FindMainWindowForPid(nxPid_);
    return nxHwnd_;
}

bool OverlayWindow::ShouldStayVisible() {
    if (dragging_) return true;      // 拖动时不要因为光标跑出 NX 就藏起来

    const HWND nx = NxWindow();
    if (nx == nullptr) return false;

    DWORD foregroundPid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &foregroundPid);
    if (foregroundPid != nxPid_) return false;

    RECT nxRect{};
    GetWindowRect(nx, &nxRect);
    POINT cursor{};
    GetCursorPos(&cursor);
    return PointInRect(cursor.x, cursor.y, nxRect.left, nxRect.top, nxRect.right, nxRect.bottom);
}

// 给 NX 的焦点窗口投递 Esc：NX 自己清空选择与高亮，等价于它的「取消选择」

void OverlayWindow::LoadLayout() {
    std::ifstream file(ExeDir() + L"\\nxgeo.layout");
    int x = 0;
    int y = 0;
    int autoPlace = 1;
    if (file >> x >> y >> autoPlace) {
        anchorX_ = x;
        anchorY_ = y;
        autoPlace_ = (autoPlace != 0);
    }
}

void OverlayWindow::SaveLayout() {
    std::ofstream file(ExeDir() + L"\\nxgeo.layout");
    file << anchorX_ << " " << anchorY_ << " " << (autoPlace_ ? 1 : 0) << "\n";
}



void OverlayWindow::ReleaseCache() {
    if (cacheDc_ != nullptr) {
        if (cache_ != nullptr) DeleteObject(cache_);
        DeleteDC(cacheDc_);
        cacheDc_ = nullptr;
        cache_ = nullptr;
    }
    cacheW_ = 0;
    cacheH_ = 0;
    cacheRevision_ = -1;
}

// 真正画内容：只在内容/尺寸变化时跑
void OverlayWindow::RenderContent(HDC dc) {
    RECT client{};
    GetClientRect(hwnd_, &client);

    HBRUSH background = CreateSolidBrush(RGB(28, 28, 30));
    FillRect(dc, &client, background);
    DeleteObject(background);

    {
        int visible = 0;
        for (const auto& row : vm_.Rows()) { if (row.visible) ++visible; }
        NxGeoLog::Write("render: " + std::to_string(client.right) + "x" + std::to_string(client.bottom) +
                        " notice=" + (showingNotice_ ? "1" : "0") +
                        " rev=" + std::to_string(vm_.Revision()) +
                        " rows=" + std::to_string(visible));
    }

    if (showingNotice_) {   // 启动提示：只有一行居中文字
        const std::wstring text = L"几何预览开启（Ctrl+Alt+G 关闭/开启）";
        HFONT noticeFont = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
        HGDIOBJ oldFont = SelectObject(dc, noticeFont);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(232, 232, 232));
        RECT box = client;
        DrawTextW(dc, text.c_str(), -1, &box, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        SelectObject(dc, oldFont);
        DeleteObject(noticeFont);
        return;
    }

    DrawToolbar(dc);

    HFONT font = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
    HGDIOBJ oldFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);

    int y = kToolbarHeight + kPadding;
    for (const auto& row : vm_.Rows()) {
        if (!row.visible) continue;

        SetTextColor(dc, RGB(150, 200, 255));
        RECT titleRect{ kPadding, y, kPadding + kTitleWidth, y + kRowHeight };
        const std::wstring title = Utf8ToWide(row.title);
        DrawTextW(dc, title.c_str(), -1, &titleRect, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

        SetTextColor(dc, RGB(232, 232, 232));
        RECT valueRect{ kPadding + kTitleWidth, y, client.right - kPadding, y + kRowHeight };
        const std::wstring value = Utf8ToWide(row.value);
        DrawTextW(dc, value.c_str(), -1, &valueRect, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);

        y += kRowHeight;
    }

    SelectObject(dc, oldFont);
    DeleteObject(font);
}

// 拖动时窗口会被系统反复要求重绘：缓存命中就只贴位图，避免逐帧建字体+画文字
void OverlayWindow::Paint(HDC target) {
    RECT client{};
    GetClientRect(hwnd_, &client);
    if (client.right <= 0 || client.bottom <= 0) return;

    const int revision = vm_.Revision();
    const bool cacheValid = (cache_ != nullptr) && (cacheW_ == client.right) &&
                            (cacheH_ == client.bottom) && (cacheRevision_ == revision);

    if (!cacheValid) {
        ReleaseCache();

        cacheDc_ = CreateCompatibleDC(target);
        cache_ = CreateCompatibleBitmap(target, client.right, client.bottom);
        if (cacheDc_ == nullptr || cache_ == nullptr) {
            ReleaseCache();
            return;
        }
        const HGDIOBJ oldBitmap = SelectObject(cacheDc_, cache_);
        RenderContent(cacheDc_);
        SelectObject(cacheDc_, oldBitmap);

        cacheW_ = client.right;
        cacheH_ = client.bottom;
        cacheRevision_ = revision;
    }

    BitBlt(target, 0, 0, client.right, client.bottom, cacheDc_, 0, 0, SRCCOPY);
    paintedRevision_ = revision;
}

int OverlayWindow::ToolbarStartX() const {
    return width_ - kPadding - kButtonCount * (kButton + kButtonGap) + kButtonGap;
}

void OverlayWindow::DrawToolbar(HDC dc) {
    RECT bar{ 0, 0, width_, kToolbarHeight };
    HBRUSH barBrush = CreateSolidBrush(RGB(18, 18, 20));
    FillRect(dc, &bar, barBrush);
    DeleteObject(barBrush);

    const int startX = ToolbarStartX();
    const int y = (kToolbarHeight - kButton) / 2;

    HPEN pen = CreatePen(PS_SOLID, 1, RGB(210, 210, 210));
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));

    for (int i = 0; i < kButtonCount; ++i) {
        const int x = startX + i * (kButton + kButtonGap);
        const int cx = x + kButton / 2;      // 按钮中心，所有图元都按中心定位
        const int cy = y + kButton / 2;

        switch (i) {
            case 0: {  // 复位：开口圆环 + 箭头
                const int r = 5;
                MoveToEx(dc, cx + r, cy, nullptr);
                AngleArc(dc, cx, cy, r, 0.0f, 300.0f);
                const int ex = cx + r - 2;
                const int ey = cy - 3;
                MoveToEx(dc, ex, ey + 3, nullptr);
                LineTo(dc, ex + 1, ey);
                LineTo(dc, ex + 4, ey + 1);
                break;
            }
            case 1: {  // 关闭：红叉
                HPEN red = CreatePen(PS_SOLID, 2, RGB(230, 90, 80));
                HGDIOBJ previous = SelectObject(dc, red);
                MoveToEx(dc, cx - 4, cy - 4, nullptr);  LineTo(dc, cx + 4, cy + 4);
                MoveToEx(dc, cx + 4, cy - 4, nullptr);  LineTo(dc, cx - 4, cy + 4);
                SelectObject(dc, previous);
                DeleteObject(red);
                break;
            }
            default:
                break;
        }
    }

    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

int OverlayWindow::ToolbarHit(int x, int y) const {
    if (y < 0 || y > kToolbarHeight) return -1;
    const int startX = ToolbarStartX();
    for (int i = 0; i < kButtonCount; ++i) {
        const int bx = startX + i * (kButton + kButtonGap);
        if (x >= bx && x <= bx + kButton) return i;
    }
    return -1;
}

void OverlayWindow::PerformToolbarAction(int index) {
    switch (index) {
        case 0:   // 复位：回到上次自动出现的位置，并恢复"新对象贴点击位置"
            autoPlace_ = true;
            if (autoX_ != INT_MIN && autoY_ != INT_MIN) {
                anchorX_ = autoX_;
                anchorY_ = autoY_;
            } else {
                PlaceAtCursor();
            }
            lastX_ = INT_MIN;
            lastY_ = INT_MIN;
            Reposition();
            SaveLayout();
            break;
        case 1:   // 关闭：隐藏到下一个对象
            dismissed_ = true;
            ShowWindow(hwnd_, SW_HIDE);
            break;
        default:
            break;
    }
}

LRESULT CALLBACK OverlayWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    OverlayWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = reinterpret_cast<OverlayWindow*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<OverlayWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self == nullptr) return DefWindowProcW(hwnd, msg, wp, lp);
    return self->HandleMessage(msg, wp, lp);
}

LRESULT OverlayWindow::HandleMessage(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd_, &ps);
            Paint(dc);
            EndPaint(hwnd_, &ps);
            return 0;
        }
        case WM_HOTKEY:
            if (wp == 1) {              // Ctrl+Alt+G：总开关
                enabled_ = !enabled_;
                if (!enabled_) {
                    ShowWindow(hwnd_, SW_HIDE);
                } else {
                    dismissed_ = false;
                }
            }
            return 0;
        case WM_LBUTTONDOWN: {
            const int hit = ToolbarHit(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            if (hit >= 0) {
                PerformToolbarAction(hit);
                return 0;
            }
            dragging_ = true;
            dragMoved_ = false;
            GetCursorPos(&dragStartCursor_);
            GetWindowRect(hwnd_, &dragStartRect_);
            SetCapture(hwnd_);
            return 0;
        }
        case WM_MOUSEMOVE:
            if (dragging_) {
                POINT now{};
                GetCursorPos(&now);
                dragMoved_ = true;
                autoPlace_ = false;      // 真正拖过，之后新对象就在这个位置出现
                const int x = dragStartRect_.left + (now.x - dragStartCursor_.x);
                const int y = dragStartRect_.top + (now.y - dragStartCursor_.y);
                anchorX_ = x;
                anchorY_ = y;
                lastX_ = x;
                lastY_ = y;
                SetWindowPos(hwnd_, HWND_TOPMOST, x, y, 0, 0,
                             SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
            }
            return 0;
        case WM_LBUTTONUP:
            if (dragging_) {
                dragging_ = false;
                ReleaseCapture();
                if (dragMoved_) SaveLayout();
            }
            return 0;
        case WM_DESTROY:
            ReleaseCache();
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

int OverlayWindow::Run() {
    if (hwnd_ == nullptr) return 1;

    MSG msg{};
    for (;;) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) return static_cast<int>(msg.wParam);
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        if (wakeEvent_) {
            WaitForSingleObject(wakeEvent_, 100);   // 有事件立刻醒，没事件 100ms 兜底
        } else {
            Sleep(16);
        }
        Tick();
    }
}
