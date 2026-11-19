#pragma once

struct Placement {
    int x;
    int y;
};

// 浮窗左上角：优先光标右下 (16,20)，越界翻到左上，再不行夹进 NX 窗口矩形。
Placement ComputePlacement(int cursorX, int cursorY, int width, int height,
                           int nxLeft, int nxTop, int nxRight, int nxBottom);

inline bool PointInRect(int x, int y, int left, int top, int right, int bottom) {
    return x >= left && x <= right && y >= top && y <= bottom;
}
