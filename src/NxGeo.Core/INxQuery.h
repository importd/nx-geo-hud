#pragma once

#include "InfoSnapshot.h"
#include "ObjRef.h"

// 查询抽象：插件用 NXOpen 实现，测试用假实现。
struct INxQuery {
    virtual ~INxQuery() {}

    // L0：类型 / tag / 颜色 / 图层，必须快（微秒~毫秒级）
    virtual InfoSnapshot QueryL0(const ObjRef& r) = 0;

    // L1：包围盒 / 面积 / 周长 / 长度
    virtual void FillL1(const ObjRef& r, InfoSnapshot& s) = 0;

    // L2：体积 / 距离，可能慢，由调度器放到后台
    virtual void FillL2(const ObjRef& r, InfoSnapshot& s) = 0;

    // D3：距离量到「上一个选中的对象」；默认不关心
    virtual void SetDistanceTarget(unsigned /*tag*/) {}
};
