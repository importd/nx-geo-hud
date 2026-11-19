#include "OverlayWindow.h"

#include <cstdlib>
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
constexpr wchar_t kClassName[] = L"NxGeoOverlayWindow";

// 快捷键默认值（可在 exe 同目录的 nxgeo.ini 里改，写成 "copy=Ctrl+Shift+C"）
struct HotkeySpec {
    UINT mods = 0;
    UINT vk = 0;
};

bool ParseHotkey(const std::string& text, HotkeySpec& out) {
    out.mods = 0;
    out.vk = 0;
    if (text.empty() || text == "none") return false;

    std::string token;
    std::string rest = text;
    while (!rest.empty()) {
        const size_t plus = rest.find('+');
        token = rest.substr(0, plus);
        rest = (plus == std::string::npos) ? std::string() : rest.substr(plus + 1);

        if (token == "Ctrl" || token == "CTRL" || token == "ctrl") out.mods |= MOD_CONTROL;
        else if (token == "Shift" || token == "SHIFT" || token == "shift") out.mods |= MOD_SHIFT;
        else if (token == "Alt" || token == "ALT" || token == "alt") out.mods |= MOD_ALT;
        else if (token.size() == 1) {
            char c = token[0];
            out.vk = (c >= 'a' && c <= 'z') ? static_cast<UINT>(c - 'a' + 'A') : static_cast<UINT>(c);
        } else if (token == "`" ) {
            out.vk = VK_OEM_3;
        } else if (token.size() >= 2 && (token[0] == 'F' || token[0] == 'f')) {
            const int n = std::atoi(token.c_str() + 1);
            if (n >= 1 && n <= 24) out.vk = VK_F1 + n - 1;
        }
        if (plus == std::string::npos) break;
    }
    return out.vk != 0;
}

std::wstring ExeDir() {
    wchar_t buffer[MAX_PATH] = { 0 };
    GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    std::wstring path(buffer);
    const size_t pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? std::wstring() : path.substr(0, pos);
}

std::string ReadIniValue(const std::wstring& iniPath, const std::string& key) {
    std::ifstream file(iniPath);
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#' || line[0] == '[') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string name = line.substr(0, eq);
        std::string value = line.substr(eq + 1);
        while (!name.empty() && (name.back() == ' ' || name.back() == '\t')) name.pop_back();
        while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.erase(0, 1);
        if (name == key) return value;
    }
    return std::string();
}

void WriteDefaultIni(const std::wstring& iniPath) {
    if (GetFileAttributesW(iniPath.c_str()) != INVALID_FILE_ATTRIBUTES) return;
    std::ofstream file(iniPath);
    file << "# NX geometry overlay hotkeys. Restart NX after editing.\n";
    file << "# Format: Ctrl/Shift/Alt + A-Z, F1-F12 or ` ; use 'none' to disable\n";
    file << "clickthrough=Alt+`\n";
    file << "toggle=Ctrl+Shift+G\n";
    file << "copy=Ctrl+Shift+C\n";
    file << "pin=Ctrl+Shift+P\n";
}
}  // namespace

std::wstring Utf8ToWide(const std::string& text) {
    if (text.empty()) return std::wstring();
    int len = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (len <= 1) return std::wstring();
    std::wstring out(static_cast<size_t>(len - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, &out[0], len);
    return out;
}

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
            } else if (wp == 3) {
                CopyToClipboard();      // Ctrl+Alt+C：复制当前信息
            } else if (wp == 4) {
                pinned_ = !pinned_;     // Ctrl+Alt+P：钉住/取消钉住
                if (!pinned_) ShowWindow(hwnd_, SW_HIDE);
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
    ApplyTranslucency(hwnd_, 100);
    RegisterHotKey(hwnd_, 1, MOD_ALT, VK_OEM_3);                    // Alt+` 切换点穿
    RegisterHotKey(hwnd_, 2, MOD_CONTROL | MOD_ALT, 'G');           // Ctrl+Alt+G 开关浮窗
    RegisterHotKey(hwnd_, 3, MOD_CONTROL | MOD_ALT, 'C');           // Ctrl+Alt+C 复制
    RegisterHotKey(hwnd_, 4, MOD_CONTROL | MOD_ALT, 'P');           // Ctrl+Alt+P 钉住

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

void OverlayWindow::CopyToClipboard() {
    std::string text;
    for (const auto& row : vm_.Rows()) {
        if (!row.visible) continue;
        text += row.title + ": " + row.value + "\r\n";
    }
    if (text.empty()) return;

    const std::wstring wide = Utf8ToWide(text);
    if (!OpenClipboard(hwnd_)) return;
    EmptyClipboard();

    const size_t bytes = (wide.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory != nullptr) {
        void* dst = GlobalLock(memory);
        if (dst != nullptr) {
            std::memcpy(dst, wide.c_str(), bytes);
            GlobalUnlock(memory);
            SetClipboardData(CF_UNICODETEXT, memory);
        }
    }
    CloseClipboard();
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
    if (pinned_) return true;   // 钉住后不受前台/光标影响

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
