#include "OverlayPlacement.h"

Placement ComputePlacement(int cursorX, int cursorY, int width, int height,
                           int nxLeft, int nxTop, int nxRight, int nxBottom) {
    const int margin = 4;

    int x = cursorX + 16;
    int y = cursorY + 20;

    // 右下放不下就翻到光标左上
    if (x + width > nxRight) x = cursorX - width - 16;
    if (y + height > nxBottom) y = cursorY - height - 20;

    // 再不行夹进 NX 窗口
    if (x < nxLeft + margin) x = nxLeft + margin;
    if (y < nxTop + margin) y = nxTop + margin;
    if (x + width > nxRight - margin) x = nxRight - width - margin;
    if (y + height > nxBottom - margin) y = nxBottom - height - margin;

    // 面板比 NX 窗口还大时，上面的夹取只会算出窗口外的坐标，直接对齐左上角
    if (width >= nxRight - nxLeft) x = nxLeft;
    if (height >= nxBottom - nxTop) y = nxTop;

    Placement p;
    p.x = x;
    p.y = y;
    return p;
}
