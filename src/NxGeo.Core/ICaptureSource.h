#pragma once

#include <functional>

#include "ObjRef.h"

// 捕获源：把「当前该显示什么」变成一串 ObjRef。
// 实现可以是选择轮询、官方 hover 回调，或自建拾取——上层不关心。
struct ICaptureSource {
    virtual ~ICaptureSource() {}
    virtual void Start(std::function<void(const ObjRef&)> sink) = 0;
    virtual void Stop() = 0;
};
