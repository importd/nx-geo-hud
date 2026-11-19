#include "OverlayPlacement.h"

Placement ComputePlacement(int cursorX, int cursorY, int width, int height,
                           int nxLeft, int nxTop, int nxRight, int nxBottom) {
    const int margin = 4;

    int x = cursorX + 16;
    int y = cursorY + 20;

    if (x + width > nxRight) x = cursorX - width - 16;
    if (y + height > nxBottom) y = cursorY - height - 20;

    if (x < nxLeft + margin) x = nxLeft + margin;
    if (y < nxTop + margin) y = nxTop + margin;
    if (x + width > nxRight - margin) x = nxRight - width - margin;
    if (y + height > nxBottom - margin) y = nxBottom - height - margin;

    Placement p;
    p.x = x;
    p.y = y;
    return p;
}
