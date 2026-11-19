#pragma once

#include <string>

#include "INxQuery.h"

// NXOpen / UFUN 的真实查询实现。所有 NX 调用都必须在 NX 主线程（或持 UG access 锁）执行。
class NxQueryAdapter : public INxQuery {
public:
    InfoSnapshot QueryL0(const ObjRef& r) override;
    void         FillL1(const ObjRef& r, InfoSnapshot& s) override;
    void         FillL2(const ObjRef& r, InfoSnapshot& s) override;

    // D3：距离默认量到「上一次选中的对象」
    void SetDistanceTarget(unsigned tag) { distanceTarget_ = tag; }



private:
    unsigned distanceTarget_ = 0;
};
