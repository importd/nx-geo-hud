#pragma once

#include <windows.h>

#include <functional>

// 在 NX 主线程上拿一个定时器：ufsta 本身就跑在主线程，
// 这里新建一个 message-only 窗口并把定时器挂在它上面，
// 它的 WndProc 由 NX 自己的消息循环派发 —— tick 就在主线程执行。
namespace MainThreadTick {

bool Install(UINT intervalMs, std::function<void()> tick);
void Remove();
bool IsInstalled();

}  // namespace MainThreadTick
