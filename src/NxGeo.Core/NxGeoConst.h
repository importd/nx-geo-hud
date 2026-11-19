#pragma once
#include <string>

namespace NxGeoConst {
    constexpr unsigned        Magic           = 0x4E584745u;   // "NXGE"
    constexpr unsigned short  ProtocolVersion = 5;
    constexpr int             SlotSize        = 512;
    constexpr int             HeaderSize      = 32;

    inline std::string SharedMemoryName(int pid) { return "Local\\NXGEO_" + std::to_string(pid); }
    inline std::string EventName(int pid)        { return "Local\\NXGEO_EVT_" + std::to_string(pid); }
}
