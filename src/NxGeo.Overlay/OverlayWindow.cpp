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

constexpr int kFontHeight = 15;   // 字号（负数传给 CreateFontW）
constexpr int kRowHeight = 21;
constexpr int kPadding = 6;
constexpr int kTitleWidth = 64;
constexpr int kToolbarHeight = 20;
constexpr int kButton = 16;
constexpr int kButtonGap = 2;
constexpr int kButtonCount = 2;          // 复位 / 关闭
// 固定窗口尺寸：不做内容自适应，避免内容分 L0/L1/L2 三步到达时窗口「一点点长大」
constexpr int kPanelWidth = 320;

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

// 每类对象高度固定：面 12 行、体 11 行、边 10 行。
// 这样 L0→L1→L2 三步到达时窗口不会一点点长高，而不同对象之间高度仍然不同。
int OverlayWindow::RowsForKind(unsigned kind) {
    switch (kind) {
        case 1:  return 11;   // 体 / 有体的特征
        case 2:  return 12;   // 面
        case 3:  return 10;   // 边
        default: return 6;    // 其它（无几何特征、曲线）
    }
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

    // 关键：NX 点击新对象时会先清空选择、再选中，中间会出现一个「空快照」。
    // 立刻隐藏就会看到面板整块闪一下，所以空状态也给宽限期。
    const bool wantVisible = (state != OverlayState::Hide) && ShouldStayVisible();
    if (!wantVisible) {
        if (hideSince_ == 0) hideSince_ = GetTickCount();
        if (GetTickCount() - hideSince_ < 300) return;   // 宽限期内保持现状
        ShowWindow(hwnd_, SW_HIDE);
        return;
    }
    hideSince_ = 0;

    switch (state) {
        case OverlayState::Hide:
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


    // 尺寸和位置一次算好、一次下发：分两次 SetWindowPos 会看到「先缩放再跳过去」
    const int wantedWidth = kPanelWidth;
    const int wantedHeight = kToolbarHeight + kPadding * 2 + RowsForKind(snapshot.kind) * kRowHeight;

    const HWND nx = NxWindow();
    RECT nxRect{ 0, 0, 0, 0 };
    if (nx != nullptr) GetWindowRect(nx, &nxRect);
    if (anchorX_ == INT_MIN || anchorY_ == INT_MIN) PlaceAtCursor();

    int x = anchorX_;
    int y = anchorY_;
    if (x < nxRect.left + 4) x = nxRect.left + 4;
    if (y < nxRect.top + 4) y = nxRect.top + 4;
    if (x + wantedWidth > nxRect.right - 4) x = nxRect.right - wantedWidth - 4;
    if (y + wantedHeight > nxRect.bottom - 4) y = nxRect.bottom - wantedHeight - 4;
    anchorX_ = x;
    anchorY_ = y;

    const bool wasVisible = IsWindowVisible(hwnd_) != FALSE;
    const bool geometryChanged = (x != lastX_) || (y != lastY_) ||
                                 (wantedWidth != appliedW_) || (wantedHeight != appliedH_);

    if (geometryChanged) {
        // 关键顺序：先接受新尺寸（工具条定位依赖 width_）→ 按新尺寸渲染 → 再改窗口尺寸，
        // 否则扩张出来的那一帧是空白，看起来就是闪一下
        width_ = wantedWidth;
        height_ = wantedHeight;
        EnsureCache(wantedWidth, wantedHeight, vm_.Revision());
        lastX_ = x;
        lastY_ = y;
        appliedW_ = wantedWidth;
        appliedH_ = wantedHeight;
        cacheRevision_ = -1;   // 尺寸变了，缓存作废
        SetWindowPos(hwnd_, HWND_TOPMOST, x, y, wantedWidth, wantedHeight,
                     SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    }

    if (!wasVisible) {
        ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
        InvalidateRect(hwnd_, nullptr, FALSE);
        UpdateWindow(hwnd_);
    } else if (geometryChanged || vm_.Revision() != paintedRevision_) {
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
        if (cacheOldBitmap_ != nullptr) {
            SelectObject(cacheDc_, cacheOldBitmap_);
            cacheOldBitmap_ = nullptr;
        }
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
void OverlayWindow::RenderContent(HDC dc, int width, int height) {
    RECT client{ 0, 0, width, height };

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
        HFONT noticeFont = CreateFontW(-kFontHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
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

    HFONT font = CreateFontW(-kFontHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
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

// 按指定尺寸把画面渲染到位图里（可以在改窗口尺寸之前先调用）
void OverlayWindow::EnsureCache(int width, int height, int revision) {
    if (cache_ != nullptr && cacheW_ == width && cacheH_ == height && cacheRevision_ == revision) return;

    ReleaseCache();
    if (width <= 0 || height <= 0) return;

    HDC screen = GetDC(hwnd_);
    if (screen == nullptr) return;
    cacheDc_ = CreateCompatibleDC(screen);
    cache_ = CreateCompatibleBitmap(screen, width, height);
    ReleaseDC(hwnd_, screen);

    if (cacheDc_ == nullptr || cache_ == nullptr) {
        ReleaseCache();
        return;
    }

    // 位图必须一直选在 cacheDc_ 里，否则 BitBlt 读到的是 1x1 默认位图（全黑）
    cacheOldBitmap_ = SelectObject(cacheDc_, cache_);
    RenderContent(cacheDc_, width, height);
    cacheW_ = width;
    cacheH_ = height;
    cacheRevision_ = revision;
}

void OverlayWindow::Paint(HDC target) {
    RECT client{};
    GetClientRect(hwnd_, &client);
    if (client.right <= 0 || client.bottom <= 0) return;

    const int revision = vm_.Revision();
    EnsureCache(client.right, client.bottom, revision);

    if (cacheDc_ != nullptr) {
        BitBlt(target, 0, 0, client.right, client.bottom, cacheDc_, 0, 0, SRCCOPY);
    } else {
        RenderContent(target, client.right, client.bottom);   // 缓存建不出来就直接画
    }
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

        // 点击反馈：悬停浅一点，按下更深
        if (i == pressedButton_ || i == hoverButton_) {
            RECT box{ x, y, x + kButton, y + kButton };
            HBRUSH hover = CreateSolidBrush((i == pressedButton_) ? RGB(84, 84, 94) : RGB(52, 52, 60));
            FillRect(dc, &box, hover);
            DeleteObject(hover);
        }

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
                pressedButton_ = hit;      // 先给按下反馈，抬起时再执行
                cacheRevision_ = -1;
                InvalidateRect(hwnd_, nullptr, FALSE);
                UpdateWindow(hwnd_);
                return 0;
            }
            dragging_ = true;
            dragMoved_ = false;
            GetCursorPos(&dragStartCursor_);
            GetWindowRect(hwnd_, &dragStartRect_);
            SetCapture(hwnd_);
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (!trackingLeave_) {
                TRACKMOUSEEVENT track{ sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd_, 0 };
                TrackMouseEvent(&track);
                trackingLeave_ = true;
            }
            const int over = ToolbarHit(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            if (over != hoverButton_) {
                hoverButton_ = over;
                cacheRevision_ = -1;
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
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
        }
        case WM_MOUSELEAVE:
            trackingLeave_ = false;
            if (hoverButton_ != -1) {
                hoverButton_ = -1;
                cacheRevision_ = -1;
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            return 0;
        case WM_LBUTTONUP:
            if (pressedButton_ >= 0) {
                const int hit = ToolbarHit(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
                const int pressed = pressedButton_;
                pressedButton_ = -1;
                cacheRevision_ = -1;
                InvalidateRect(hwnd_, nullptr, FALSE);
                if (hit == pressed) PerformToolbarAction(pressed);
                return 0;
            }
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

        // 关键：等待时把窗口输入也算进来，否则拖动时鼠标消息会攒到 100ms 才被处理（看起来掉帧）
        if (wakeEvent_ != nullptr) {
            HANDLE handles[1] = { wakeEvent_ };
            MsgWaitForMultipleObjectsEx(1, handles, 100, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        } else {
            MsgWaitForMultipleObjectsEx(0, nullptr, 100, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        }
        Tick();
    }
}
