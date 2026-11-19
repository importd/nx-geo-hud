#pragma once

namespace PluginEntry {
    void Start();
    void Stop();
    bool IsRunning();

    // 临时：在命令上下文里启动悬停验证
    void RunHoverProbe();
}
