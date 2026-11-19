#pragma once

#include <string>
#include <vector>

#include "InfoSnapshot.h"

struct InfoRow {
    std::string title;
    std::string value;
    bool        visible = false;
};

// 字段级 dirty：只重算变化的行，Revision 只在真有变化时递增。
class InfoViewModel {
public:
    InfoViewModel();

    void                     Apply(const InfoSnapshot& s);
    const std::vector<InfoRow>& Rows() const { return rows_; }
    int                      Revision() const { return revision_; }

private:
    std::vector<InfoRow> rows_;
    int                  revision_ = 0;
};
