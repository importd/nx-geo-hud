#pragma once

#include <atomic>
#include <functional>
#include <thread>

#include "ICaptureSource.h"

// 每 50ms 读一次 NX 当前选择列表；只取 tag，不做几何查询。
// NXOpen 调用必须在 NX 主线程或持 UG access 锁时执行，这里用后者。
class SelectionCaptureSource : public ICaptureSource {
public:
    void Start(std::function<void(const ObjRef&)> sink) override;
    void Stop() override;

private:
    void Loop();

    std::function<void(const ObjRef&)> sink_;
    std::thread                        thread_;
    std::atomic<bool>                  running_{ false };
};
