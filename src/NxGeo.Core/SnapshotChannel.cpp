#include "SnapshotChannel.h"

#include <windows.h>

#include <cstring>
#include <stdexcept>

#include "NxGeoConst.h"
#include "SnapshotCodec.h"

namespace {

constexpr DWORD kTotalSize = static_cast<DWORD>(NxGeoConst::HeaderSize + 2 * NxGeoConst::SlotSize);

std::wstring Widen(const std::string& s) {
    return std::wstring(s.begin(), s.end());
}

}  // namespace

SnapshotChannel::SnapshotChannel(const std::string& name, bool createNew) {
    const std::wstring wname = Widen(name);

    if (createNew) {
        SetLastError(0);
        map_ = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, kTotalSize, wname.c_str());
        if (!map_) return;
        const bool created = (GetLastError() != ERROR_ALREADY_EXISTS);

        header_ = static_cast<SharedHeader*>(MapViewOfFile(map_, FILE_MAP_ALL_ACCESS, 0, 0, kTotalSize));
        if (!header_) return;

        if (created) {
            std::memset(header_, 0, kTotalSize);
            header_->magic = NxGeoConst::Magic;
            header_->version = NxGeoConst::ProtocolVersion;
        }
    } else {
        map_ = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, wname.c_str());
        if (!map_) return;
        header_ = static_cast<SharedHeader*>(MapViewOfFile(map_, FILE_MAP_ALL_ACCESS, 0, 0, kTotalSize));
        if (!header_) return;
    }

    slots_ = reinterpret_cast<unsigned char*>(header_) + NxGeoConst::HeaderSize;
}

SnapshotChannel::~SnapshotChannel() {
    if (header_) UnmapViewOfFile(header_);
    if (map_) CloseHandle(map_);
}

void SnapshotChannel::SetNxHwnd(long long hwnd) {
    if (header_) header_->nxHwnd = hwnd;
}

void SnapshotChannel::SetNxPid(int pid) {
    if (header_) header_->nxPid = pid;
}

long long SnapshotChannel::NxHwnd() const {
    return header_ ? header_->nxHwnd : 0;
}

unsigned long long SnapshotChannel::Write(const InfoSnapshot& s) {
    if (!header_) return 0;

    const unsigned long long g = ++gen_;
    InfoSnapshot copy = s;
    copy.gen = g;

    const int slot = static_cast<int>(g & 1ull);
    EncodeSnapshot(copy, slots_ + slot * NxGeoConst::SlotSize);

    MemoryBarrier();
    header_->activeGen = g;
    MemoryBarrier();

    if (wakeEvent_ != nullptr) {
        SetEvent(static_cast<HANDLE>(wakeEvent_));   // auto-reset，浮窗立刻醒
    }
    return g;
}

bool SnapshotChannel::TryRead(InfoSnapshot& out) const {
    versionMismatch_ = false;
    if (!header_) return false;

    if (header_->magic != NxGeoConst::Magic || header_->version != NxGeoConst::ProtocolVersion) {
        versionMismatch_ = true;
        return false;
    }

    for (int attempt = 0; attempt < 3; ++attempt) {
        const unsigned long long gen = header_->activeGen;
        if (gen == 0) return false;

        const int slot = static_cast<int>(gen & 1ull);
        InfoSnapshot s;
        try {
            s = DecodeSnapshot(slots_ + slot * NxGeoConst::SlotSize);
        } catch (const std::runtime_error&) {
            return false;
        }

        if (header_->activeGen == gen) {
            out = s;
            return true;
        }
    }
    return false;
}
