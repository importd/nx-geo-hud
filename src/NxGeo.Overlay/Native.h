#pragma once

#include <windows.h>

// 让窗口不抢焦点、不出现在任务栏、可点穿。
inline void ApplyNoActivateStyles(HWND hwnd, bool clickThrough) {
    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    style |= (WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW);
    if (clickThrough) {
        style |= WS_EX_TRANSPARENT;
    } else {
        style &= ~static_cast<LONG_PTR>(WS_EX_TRANSPARENT);
    }
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, style);
}

// 整窗统一透明度（LWA_ALPHA）：文字和底板一起半透明，够用且不需要逐像素预乘 alpha。
inline void ApplyTranslucency(HWND hwnd, BYTE alpha) {
    SetLayeredWindowAttributes(hwnd, 0, alpha, LWA_ALPHA);
}
