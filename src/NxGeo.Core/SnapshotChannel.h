#pragma once

#include <string>

#include "InfoSnapshot.h"

// 共享内存头：32 字节，紧跟两个 SlotSize 的槽。
struct SharedHeader {
    unsigned           magic;
    unsigned short     version;
    unsigned short     reserved;
    unsigned long long activeGen;
    long long          nxHwnd;
    int                nxPid;
    unsigned           reserved2;
};
static_assert(sizeof(SharedHeader) == 32, "SharedHeader must stay 32 bytes");

// seqlock 双槽通道：写方永远写 (gen & 1) 槽，读方比对 activeGen 保证不撕裂。
class SnapshotChannel {
public:
    SnapshotChannel(const std::string& name, bool createNew);
    ~SnapshotChannel();

    SnapshotChannel(const SnapshotChannel&) = delete;
    SnapshotChannel& operator=(const SnapshotChannel&) = delete;

    void      SetNxHwnd(long long hwnd);
    void      SetNxPid(int pid);
    long long NxHwnd() const;

    unsigned long long Write(const InfoSnapshot& s);
    bool               TryRead(InfoSnapshot& out) const;
    bool               VersionMismatch() const { return versionMismatch_; }

private:
    void*               map_ = nullptr;
    SharedHeader*       header_ = nullptr;
    unsigned char*      slots_ = nullptr;
    unsigned long long  gen_ = 0;
    mutable bool        versionMismatch_ = false;
};
