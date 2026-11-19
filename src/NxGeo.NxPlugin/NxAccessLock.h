#pragma once

#include <uf.h>
#include <uf_ui.h>

// 非 NX 主线程调用 NXOpen/UFUN 时必须持有的 UG access 锁（RAII）。
class NxAccessLock {
public:
    NxAccessLock() : locked_(UF_UI_lock_ug_access(UF_UI_FROM_CUSTOM) == UF_UI_LOCK_SET) {}
    ~NxAccessLock() {
        if (locked_) UF_UI_unlock_ug_access(UF_UI_FROM_CUSTOM);
    }

    NxAccessLock(const NxAccessLock&) = delete;
    NxAccessLock& operator=(const NxAccessLock&) = delete;

    bool locked() const { return locked_; }

private:
    bool locked_;
};
