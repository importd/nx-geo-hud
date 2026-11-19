#pragma once

#include <functional>
#include <vector>

#include "ICaptureSource.h"

// 选择轮询：不再自带线程，由 MainThreadTick 在 NX 主线程上驱动 PollOnce()。
class SelectionCaptureSource : public ICaptureSource {
public:
    void Start(std::function<void(const ObjRef&)> sink) override;
    void Stop() override;

    // 必须在 NX 主线程调用
    void PollOnce();

private:
    std::function<void(const ObjRef&)> sink_;
    unsigned                           lastTag_ = 0xFFFFFFFFu;
    std::vector<unsigned>              previous_;
};
