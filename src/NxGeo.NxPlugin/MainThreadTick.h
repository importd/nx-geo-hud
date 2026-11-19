#pragma once

#include <windows.h>

#include <functional>

// 在 NX 主窗口上挂一个定时器并子类化它的窗口过程：
// 这样 tick 跑在 NX 自己的消息循环里（NX 主线程），NXOpen/UF_UI 调用才安全。
namespace MainThreadTick {

bool Install(HWND nxWindow, UINT intervalMs, std::function<void()> tick);
void Remove();
bool IsInstalled();

}  // namespace MainThreadTick
