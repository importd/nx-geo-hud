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
constexpr UINT WM_TRAYICON = WM_APP + 1;

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
        case WM_TRAYICON:
            if (LOWORD(lp) == WM_RBUTTONUP) {
                ShowTrayMenu();
            } else if (LOWORD(lp) == WM_LBUTTONDBLCLK) {
                enabled_ = !enabled_;
                if (!enabled_) ShowWindow(hwnd_, SW_HIDE);
            }
            return 0;
        case WM_RBUTTONUP:
            NxGeoLog::Write("menu: WM_RBUTTONUP received");
            ShowTrayMenu();          // 面板上右键也出同一个菜单
            return 0;
        case WM_LBUTTONDOWN:
            dragging_ = true;
            autoPlace_ = false;      // 手动挪过之后就别再自动贴光标
            GetCursorPos(&dragStartCursor_);
            GetWindowRect(hwnd_, &dragStartRect_);
            SetCapture(hwnd_);
            return 0;
        case WM_MOUSEMOVE:
            if (dragging_) {
                POINT now{};
                GetCursorPos(&now);
                const int nx = dragStartRect_.left + (now.x - dragStartCursor_.x);
                const int ny = dragStartRect_.top + (now.y - dragStartCursor_.y);
                followCursor_ = false;
                anchorX_ = nx;
                anchorY_ = ny;
                lastX_ = nx;
                lastY_ = ny;
                SetWindowPos(hwnd_, HWND_TOPMOST, nx, ny, 0, 0,
                             SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
            }
            return 0;
        case WM_LBUTTONUP:
            if (dragging_) {
                dragging_ = false;
                ReleaseCapture();
                SaveLayout();
            }
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd_, &ps);
            Paint(dc);
            EndPaint(hwnd_, &ps);
            return 0;
        }
        case WM_HOTKEY: {
            if (wp == 2) {              // Ctrl+Alt+G：开关浮窗
                enabled_ = !enabled_;
                if (!enabled_) ShowWindow(hwnd_, SW_HIDE);
            }
            return 0;
        }
        case WM_DESTROY:
            RemoveTrayIcon();
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
    ApplyTranslucency(hwnd_, 170);

    // 只保留一个快捷键：Ctrl+Alt+G 开关浮窗。
    // 复制/钉住/点穿走托盘右键菜单，避免和 NX 抢组合键。
    if (RegisterHotKey(hwnd_, 2, MOD_CONTROL | MOD_ALT, 'G')) {
        NxGeoLog::Write("hotkey toggle = Ctrl+Alt+G");
    } else {
        NxGeoLog::Write("hotkey toggle FAILED (Ctrl+Alt+G 被占用)，请用托盘图标");
    }
    {
        const LONG_PTR ex = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
        NxGeoLog::Write(std::string("overlay style: TRANSPARENT=") +
                        ((ex & WS_EX_TRANSPARENT) ? "1" : "0") + " NOACTIVATE=" +
                        ((ex & WS_EX_NOACTIVATE) ? "1" : "0"));
    }
    LoadLayout();
    AddTrayIcon();

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
            // 新对象：面板落到「点它的位置」右下方，之后保持不动（缩放也不跟随）
            if (snapshot.tag != lastTag_) {
                lastTag_ = snapshot.tag;
                if (autoPlace_) {
                    POINT cursor{};
                    GetCursorPos(&cursor);
                    anchorX_ = cursor.x + 20;
                    anchorY_ = cursor.y + 24;
                    followCursor_ = false;
                }
            }
            vm_.Apply(snapshot);
            break;
    }

    if (!stay) {
        ShowWindow(hwnd_, SW_HIDE);
        return;
    }

    const int wantedW = ContentWidth();
    const int wantedH = ContentHeight();
    const bool resized = (wantedW != width_ || wantedH != height_);
    if (resized) {
        width_ = wantedW;
        height_ = wantedH;
        SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, width_, height_,
                     SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        lastX_ = INT_MIN;   // 尺寸变了，定位要重算
        lastY_ = INT_MIN;
    }

    // 顺序很重要：先把尺寸和位置定好，再显示并立刻画完，否则会「先出现在旧位置再跳」
    const bool wasVisible = IsWindowVisible(hwnd_) != FALSE;
    Reposition();

    if (!wasVisible) {
        ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
        InvalidateRect(hwnd_, nullptr, FALSE);
        UpdateWindow(hwnd_);
    } else if (vm_.Revision() != paintedRevision_) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}


enum TrayCommand {
    kTrayCopy = 1,
    kTrayPin = 2,
    kTrayClickThrough = 3,
    kTrayHide = 4,
    kTrayQuit = 5,
    kTrayFollow = 6,
    kTrayReset = 7
};

void OverlayWindow::AddTrayIcon() {
    tray_.cbSize = sizeof(NOTIFYICONDATAW);
    tray_.hWnd = hwnd_;
    tray_.uID = 1;
    tray_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    tray_.uCallbackMessage = WM_TRAYICON;
    tray_.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(tray_.szTip, L"NX 几何信息浮窗（右键菜单）");
    Shell_NotifyIconW(NIM_ADD, &tray_);
}

void OverlayWindow::RemoveTrayIcon() {
    if (tray_.hWnd != nullptr) {
        Shell_NotifyIconW(NIM_DELETE, &tray_);
        tray_.hWnd = nullptr;
    }
}

// 系统图标转成菜单能用的位图（底色用菜单色，避免黑框）
HBITMAP IconToMenuBitmap(HICON icon) {
    if (icon == nullptr) return nullptr;
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, 16, 16);
    HGDIOBJ old = SelectObject(mem, bitmap);

    RECT box{ 0, 0, 16, 16 };
    HBRUSH background = CreateSolidBrush(GetSysColor(COLOR_MENU));
    FillRect(mem, &box, background);
    DeleteObject(background);

    DrawIconEx(mem, 0, 0, icon, 16, 16, 0, nullptr, DI_NORMAL);

    SelectObject(mem, old);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    return bitmap;
}

void OverlayWindow::ShowTrayMenu() {
    POINT cursor{};
    GetCursorPos(&cursor);

    HMENU menu = CreatePopupMenu();

    AppendMenuW(menu, MF_STRING, kTrayCopy, L"复制信息\tCtrl+C");
    AppendMenuW(menu, MF_STRING | (pinned_ ? MF_CHECKED : 0), kTrayPin, L"钉住面板\tCtrl+P");
    AppendMenuW(menu, MF_STRING | (followCursor_ ? MF_CHECKED : 0), kTrayFollow, L"跟随鼠标");
    AppendMenuW(menu, MF_STRING, kTrayReset, L"恢复默认位置");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kTrayHide, L"隐藏面板\tCtrl+Alt+G");

    // 菜单项图标
    HBITMAP copyIcon = IconToMenuBitmap(static_cast<HICON>(LoadImageW(nullptr, IDI_INFORMATION, IMAGE_ICON, 16, 16, LR_SHARED)));
    HBITMAP pinIcon = IconToMenuBitmap(static_cast<HICON>(LoadImageW(nullptr, IDI_APPLICATION, IMAGE_ICON, 16, 16, LR_SHARED)));
    HBITMAP hideIcon = IconToMenuBitmap(static_cast<HICON>(LoadImageW(nullptr, IDI_WARNING, IMAGE_ICON, 16, 16, LR_SHARED)));
    HBITMAP quitIcon = IconToMenuBitmap(static_cast<HICON>(LoadImageW(nullptr, IDI_ERROR, IMAGE_ICON, 16, 16, LR_SHARED)));
    SetMenuItemBitmaps(menu, kTrayCopy, MF_BYCOMMAND, copyIcon, copyIcon);
    SetMenuItemBitmaps(menu, kTrayPin, MF_BYCOMMAND, pinIcon, pinIcon);
    SetMenuItemBitmaps(menu, kTrayHide, MF_BYCOMMAND, hideIcon, hideIcon);
    SetMenuItemBitmaps(menu, kTrayQuit, MF_BYCOMMAND, quitIcon, quitIcon);

    SetForegroundWindow(hwnd_);
    NxGeoLog::Write("menu: TrackPopupMenu at " + std::to_string(cursor.x) + "," + std::to_string(cursor.y));
    const int command = TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD,
                                       cursor.x, cursor.y, 0, hwnd_, nullptr);
    PostMessageW(hwnd_, WM_NULL, 0, 0);   // NOACTIVATE 窗口的经典修正：否则菜单会立刻消失
    DestroyMenu(menu);
    NxGeoLog::Write("menu: command=" + std::to_string(command));

    DeleteObject(copyIcon);
    DeleteObject(pinIcon);
    DeleteObject(hideIcon);
    DeleteObject(quitIcon);

    switch (command) {
        case kTrayCopy:
            CopyToClipboard();
            break;
        case kTrayPin:
            pinned_ = !pinned_;
            if (!pinned_) ShowWindow(hwnd_, SW_HIDE);
            break;
        case kTrayClickThrough:
            clickThrough_ = !clickThrough_;
            ApplyNoActivateStyles(hwnd_, clickThrough_);
            break;
        case kTrayHide:
            enabled_ = false;
            ShowWindow(hwnd_, SW_HIDE);
            break;
        case kTrayFollow:
            followCursor_ = !followCursor_;
            if (!followCursor_) {
                autoPlace_ = true;
                ResetToDefaultPosition();
            }
            SaveLayout();
            break;
        case kTrayReset:
            followCursor_ = false;
            autoPlace_ = true;
            ResetToDefaultPosition();
            SaveLayout();
            break;
        case kTrayQuit:
            PostQuitMessage(0);
            break;
        default:
            break;
    }
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

    int x = 0;
    int y = 0;

    if (followCursor_) {
        POINT cursor{};
        GetCursorPos(&cursor);

        // 跟随模式下，鼠标进到面板里就停住，不然右键落不上
        RECT self{};
        if (GetWindowRect(hwnd_, &self) &&
            PointInRect(cursor.x, cursor.y, self.left, self.top, self.right, self.bottom)) {
            return;
        }

        const Placement p = ComputePlacement(cursor.x, cursor.y, width_, height_,
                                             nxRect.left, nxRect.top, nxRect.right, nxRect.bottom);
        x = p.x;
        y = p.y;
    } else {
        if (anchorX_ == INT_MIN || anchorY_ == INT_MIN) {
            ResetToDefaultPosition();
        }
        x = anchorX_;
        y = anchorY_;

        // 固定在 NX 窗口内（窗口挪动/缩放后也不会跑丢）
        if (x < nxRect.left + 4) x = nxRect.left + 4;
        if (y < nxRect.top + 4) y = nxRect.top + 4;
        if (x + width_ > nxRect.right - 4) x = nxRect.right - width_ - 4;
        if (y + height_ > nxRect.bottom - 4) y = nxRect.bottom - height_ - 4;
        anchorX_ = x;
        anchorY_ = y;
    }

    if (x != lastX_ || y != lastY_) {
        lastX_ = x;
        lastY_ = y;
        SetWindowPos(hwnd_, HWND_TOPMOST, x, y, 0, 0,
                     SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    }
}

void OverlayWindow::ResetToDefaultPosition() {
    const HWND nx = NxWindow();
    if (nx == nullptr) return;

    RECT nxRect{};
    GetWindowRect(nx, &nxRect);

    anchorX_ = nxRect.right - width_ - 20;   // 右上角
    anchorY_ = nxRect.top + 20;
    lastX_ = INT_MIN;
    lastY_ = INT_MIN;
}

void OverlayWindow::LoadLayout() {
    std::ifstream file(ExeDir() + L"\\nxgeo.layout");
    int x = 0;
    int y = 0;
    int follow = 0;
    if (file >> x >> y >> follow) {
        anchorX_ = x;
        anchorY_ = y;
        followCursor_ = (follow != 0);
    }
}

void OverlayWindow::SaveLayout() {
    std::ofstream file(ExeDir() + L"\\nxgeo.layout");
    file << anchorX_ << " " << anchorY_ << " " << (followCursor_ ? 1 : 0) << "\n";
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
