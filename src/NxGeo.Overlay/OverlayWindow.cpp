#include "OverlayWindow.h"

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
constexpr wchar_t kClassName[] = L"NxGeoOverlayWindow";

std::wstring Utf8ToWide(const std::string& text) {
    if (text.empty()) return std::wstring();
    int len = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (len <= 1) return std::wstring();
    std::wstring out(static_cast<size_t>(len - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, &out[0], len);
    return out;
}

}  // namespace

namespace {

struct FindWindowContext {
    DWORD pid;
    HWND  result;
};

BOOL CALLBACK FindMainWindowProc(HWND hwnd, LPARAM param) {
    auto* ctx = reinterpret_cast<FindWindowContext*>(param);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == ctx->pid && IsWindowVisible(hwnd) && GetWindow(hwnd, GW_OWNER) == nullptr) {
        ctx->result = hwnd;
        return FALSE;
    }
    return TRUE;
}

HWND FindMainWindowForPid(DWORD pid) {
    FindWindowContext ctx{ pid, nullptr };
    EnumWindows(&FindMainWindowProc, reinterpret_cast<LPARAM>(&ctx));
    return ctx.result;
}

}  // namespace

LRESULT CALLBACK OverlayWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* self = reinterpret_cast<OverlayWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    }

    auto* self = reinterpret_cast<OverlayWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self) return self->HandleMessage(msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT OverlayWindow::HandleMessage(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd_, &ps);
            Paint(dc);
            EndPaint(hwnd_, &ps);
            return 0;
        }
        case WM_HOTKEY: {
            if (wp == 1) {
                clickThrough_ = !clickThrough_;
                ApplyNoActivateStyles(hwnd_, clickThrough_);
            } else if (wp == 2) {
                enabled_ = !enabled_;   // Ctrl+Alt+G：临时关掉/恢复
                if (!enabled_) ShowWindow(hwnd_, SW_HIDE);
            }
            return 0;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

bool OverlayWindow::Create(HINSTANCE instance, unsigned long long nxHwnd, int nxPid) {
    nxPid_ = static_cast<DWORD>(nxPid);
    nxHwnd_ = reinterpret_cast<HWND>(nxHwnd);
    if (!nxHwnd_ || !IsWindow(nxHwnd_)) {
        nxHwnd_ = FindMainWindowForPid(nxPid_);
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = &OverlayWindow::WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

    hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT,
                            kClassName, L"", WS_POPUP,
                            0, 0, width_, height_, nullptr, nullptr, instance, this);
    if (!hwnd_) return false;

    ApplyNoActivateStyles(hwnd_, clickThrough_);
    ApplyTranslucency(hwnd_, 120);
    RegisterHotKey(hwnd_, 1, MOD_ALT, VK_OEM_3);                    // Alt+` 切换点穿
    RegisterHotKey(hwnd_, 2, MOD_CONTROL | MOD_ALT, 'G');           // Ctrl+Alt+G 开关浮窗

    if (nxHwnd_ && IsWindow(nxHwnd_)) {
        SetWindowLongPtrW(hwnd_, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(nxHwnd_));   // owner = NX 主窗
    }

    channel_ = new SnapshotChannel(NxGeoConst::SharedMemoryName(nxPid), false);
    wakeEvent_ = OpenEventW(SYNCHRONIZE, FALSE, Utf8ToWide(NxGeoConst::EventName(nxPid)).c_str());

    NxGeoLog::Write("overlay created: nxpid=" + std::to_string(nxPid) +
                    " hwnd=" + std::to_string(reinterpret_cast<unsigned long long>(nxHwnd_)) +
                    " channel=" + (channel_->IsValid() ? "ok" : "MISSING") +
                    " event=" + (wakeEvent_ ? "ok" : "missing"));
    return true;
}

void OverlayWindow::Tick() {
    if (!channel_ || !enabled_) return;

    InfoSnapshot snapshot{};
    const bool readOk = channel_->TryRead(snapshot);
    const bool mismatch = channel_->VersionMismatch();
    const bool isEmpty = readOk && snapshot.kind == static_cast<unsigned short>(ObjKind::None);
    const OverlayState state = DecideOverlayState(readOk, mismatch, isEmpty);

    const bool stay = ShouldStayVisible();
    const std::string desc = "tick: state=" + std::to_string(static_cast<int>(state)) +
                             " readOk=" + (readOk ? "1" : "0") +
                             " mismatch=" + (mismatch ? "1" : "0") +
                             " kind=" + std::to_string(snapshot.kind) +
                             " tag=" + std::to_string(snapshot.tag) +
                             " stay=" + (stay ? "1" : "0");
    if (desc != lastLogged_) {
        NxGeoLog::Write(desc);
        lastLogged_ = desc;
    }

    switch (state) {
        case OverlayState::Hide:
            ShowWindow(hwnd_, SW_HIDE);
            return;
        case OverlayState::ShowMessage:
            vm_.ShowMessage("协议版本不符，请重启 NX");
            break;
        case OverlayState::ShowSnapshot:
            vm_.Apply(snapshot);
            break;
    }

    if (!stay) {
        ShowWindow(hwnd_, SW_HIDE);
        return;
    }

    const int wantedW = ContentWidth();
    const int wantedH = ContentHeight();
    if (wantedW != width_ || wantedH != height_) {
        width_ = wantedW;
        height_ = wantedH;
        SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, width_, height_,
                     SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    }

    if (vm_.Revision() != paintedRevision_) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
    if (!IsWindowVisible(hwnd_)) {
        ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    }
    Reposition();
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
    const HWND nx = NxWindow();
    if (nx == nullptr) return false;

    // 前台是不是 NX：按进程 ID 判断。插件启动时 GetForegroundWindow 可能拿到启动画面，
    // 用窗口指针比较会永远失配。
    DWORD foregroundPid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &foregroundPid);
    if (foregroundPid != nxPid_) return false;

    POINT cursor{};
    GetCursorPos(&cursor);

    RECT nxRect{};
    GetWindowRect(nx, &nxRect);
    return PointInRect(cursor.x, cursor.y, nxRect.left, nxRect.top, nxRect.right, nxRect.bottom);
}

void OverlayWindow::Reposition() {
    const HWND nx = NxWindow();
    if (nx == nullptr) return;

    RECT nxRect{};
    GetWindowRect(nx, &nxRect);

    POINT cursor{};
    GetCursorPos(&cursor);

    const Placement p = ComputePlacement(cursor.x, cursor.y, width_, height_,
                                         nxRect.left, nxRect.top, nxRect.right, nxRect.bottom);

    if (p.x != lastX_ || p.y != lastY_) {
        lastX_ = p.x;
        lastY_ = p.y;
        SetWindowPos(hwnd_, HWND_TOPMOST, p.x, p.y, 0, 0,
                     SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    }
}

// 按当前内容里最宽的一行算宽度（标题列固定 + 值列实测），并夹在合理区间
int OverlayWindow::ContentWidth() {
    HFONT font = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");

    HDC dc = GetDC(hwnd_);
    if (dc == nullptr) {
        DeleteObject(font);
        return width_;
    }

    HGDIOBJ oldFont = SelectObject(dc, font);
    int widestValue = 0;
    for (const auto& row : vm_.Rows()) {
        if (!row.visible) continue;
        const std::wstring value = Utf8ToWide(row.value);
        SIZE size{};
        if (GetTextExtentPoint32W(dc, value.c_str(), static_cast<int>(value.size()), &size)) {
            if (size.cx > widestValue) widestValue = size.cx;
        }
    }
    SelectObject(dc, oldFont);
    ReleaseDC(hwnd_, dc);
    DeleteObject(font);

    int width = kPadding * 2 + kTitleWidth + 6 + widestValue;
    if (width < 220) width = 220;
    if (width > 720) width = 720;
    return width;
}

int OverlayWindow::ContentHeight() const {    int rows = 0;
    for (const auto& row : vm_.Rows()) {
        if (row.visible) ++rows;
    }
    return kPadding * 2 + rows * kRowHeight;
}

void OverlayWindow::Paint(HDC target) {
    RECT client{};
    GetClientRect(hwnd_, &client);

    // 双缓冲：先画到内存位图再一次性贴上去，避免逐行刷新造成闪烁
    HDC dc = CreateCompatibleDC(target);
    HBITMAP bitmap = CreateCompatibleBitmap(target, client.right, client.bottom);
    HGDIOBJ oldBitmap = SelectObject(dc, bitmap);

    const HBRUSH background = CreateSolidBrush(RGB(28, 28, 30));
    FillRect(dc, &client, background);
    DeleteObject(background);

    HFONT font = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
    HGDIOBJ oldFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);

    int y = kPadding;
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

    BitBlt(target, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY);

    SelectObject(dc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);

    paintedRevision_ = vm_.Revision();
}
int OverlayWindow::Run() {
    if (!hwnd_) return 1;

    MSG msg{};
    for (;;) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) return static_cast<int>(msg.wParam);
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        if (wakeEvent_) {
            WaitForSingleObject(wakeEvent_, 16);
        } else {
            Sleep(16);
        }
        Tick();
    }
}
