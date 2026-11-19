#pragma once

// 浮窗当前该做什么：隐藏 / 显示一条消息 / 显示快照。
enum class OverlayState {
    Hide,
    ShowMessage,
    ShowSnapshot
};

inline OverlayState DecideOverlayState(bool readOk, bool versionMismatch, bool isEmpty) {
    if (versionMismatch) return OverlayState::ShowMessage;
    if (!readOk) return OverlayState::Hide;
    if (isEmpty) return OverlayState::Hide;
    return OverlayState::ShowSnapshot;
}
