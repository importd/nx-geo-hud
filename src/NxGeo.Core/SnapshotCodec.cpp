#include "SnapshotCodec.h"

#include <cstring>
#include <stdexcept>

#include "NxGeoConst.h"

void EncodeSnapshot(const InfoSnapshot& s, unsigned char* out512) {
    std::memset(out512, 0, NxGeoConst::SlotSize);

    InfoSnapshot copy = s;
    copy.magic = NxGeoConst::Magic;
    copy.version = NxGeoConst::ProtocolVersion;
    std::memcpy(out512, &copy, sizeof(InfoSnapshot));
}

InfoSnapshot DecodeSnapshot(const unsigned char* in512) {
    InfoSnapshot s;
    std::memcpy(&s, in512, sizeof(InfoSnapshot));

    if (s.magic != NxGeoConst::Magic || s.version != NxGeoConst::ProtocolVersion) {
        throw std::runtime_error("snapshot protocol mismatch");
    }
    return s;
}
