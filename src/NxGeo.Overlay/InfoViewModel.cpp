#include "InfoViewModel.h"

#include <cstdio>

#include "FieldFlags.h"

namespace {

std::string Fixed4(double v) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.4f", v);
    return std::string(buf);
}

std::string Fixed3(double v) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.3f", v);
    return std::string(buf);
}

}  // namespace

InfoViewModel::InfoViewModel() {
    rows_.resize(11);
    const char* titles[11] = {
        "类型", "tag", "颜色", "图层", "包围盒", "面积", "周长", "长度", "体积", "距离", "备注"
    };
    for (int i = 0; i < 11; ++i) rows_[i].title = titles[i];
}

void InfoViewModel::ShowMessage(const std::string& text) {
    std::vector<InfoRow> next = rows_;
    for (auto& row : next) {
        row.value.clear();
        row.visible = false;
    }
    if (!next.empty()) {
        next[0].value = text;
        next[0].visible = true;
    }

    bool changed = false;
    for (size_t i = 0; i < rows_.size(); ++i) {
        if (rows_[i].value != next[i].value || rows_[i].visible != next[i].visible) {
            changed = true;
            break;
        }
    }
    if (changed) {
        rows_ = next;
        ++revision_;
    }
}

void InfoViewModel::Apply(const InfoSnapshot& s) {
    std::vector<InfoRow> next = rows_;
    for (auto& row : next) {
        row.value.clear();
        row.visible = false;
    }

    if (HasField(s.fields, Field_L0)) {
        next[0].value = s.typeName;
        next[0].visible = true;
        next[1].value = std::to_string(s.tag);          // 十进制 tag
        next[1].visible = true;
        next[2].value = std::to_string(s.color);
        next[2].visible = true;
        next[3].value = std::to_string(s.layer);        // 图层
        next[3].visible = true;
    }

    if (HasField(s.fields, Field_BBox)) {
        char buf[160];
        std::snprintf(buf, sizeof buf, "%.1f,%.1f,%.1f → %.1f,%.1f,%.1f",
                      s.bboxMin[0], s.bboxMin[1], s.bboxMin[2],
                      s.bboxMax[0], s.bboxMax[1], s.bboxMax[2]);
        next[4].value = buf;
        next[4].visible = true;
    }

    if (HasField(s.fields, Field_Area)) {
        next[5].value = Fixed3(s.area);
        next[5].visible = true;
        next[6].value = Fixed3(s.perimeter);
        next[6].visible = true;
    }

    if (HasField(s.fields, Field_Length)) {
        next[7].value = Fixed3(s.length);
        next[7].visible = true;
    }

    if (HasField(s.fields, Field_Volume)) {
        next[8].value = Fixed3(s.volume);
        next[8].visible = true;
    }

    if (HasField(s.fields, Field_Distance)) {
        next[9].value = Fixed4(s.distance);
        next[9].visible = true;
    }

    if (s.note[0] != '\0') {
        next[10].value = s.note;
        next[10].visible = true;
    }

    bool changed = false;
    for (size_t i = 0; i < rows_.size(); ++i) {
        if (rows_[i].value != next[i].value || rows_[i].visible != next[i].visible) {
            changed = true;
            break;
        }
    }

    if (changed) {
        rows_ = next;
        ++revision_;
    }
}
