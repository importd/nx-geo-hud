#include "Check.h"
#include "NxGeoConst.h"

int main() {
    CHECK_EQ(NxGeoConst::SlotSize, 512);
    CHECK_EQ(std::string(NxGeoConst::SharedMemoryName(42)), std::string("Local\\NXGEO_42"));

    std::cout << (Check::Failures() == 0 ? "ALL PASS" : "FAILED") << "\n";
    return Check::Failures() == 0 ? 0 : 1;
}
