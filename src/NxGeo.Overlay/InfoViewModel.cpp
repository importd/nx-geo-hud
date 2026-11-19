#include "InfoViewModel.h"

#include <cstdio>

#include "FieldFlags.h"

namespace {

std::string Compact(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.3f", v);
    std::string s(buf);
    const size_t dot = s.find('.');
    if (dot != std::string::npos) {
        s.erase(s.find_last_not_of('0') + 1);
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    return s;
}

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
    rows_.resize(12);
    const char* titles[12] = {
        "名称", "类型", "tag", "颜色", "图层", "包围盒", "面积", "周长", "长度", "体积", "距离", "备注"
    };
    for (int i = 0; i < 12; ++i) rows_[i].title = titles[i];
}

void InfoViewModel::ShowMessage(const std::string& text) {
    std::vector<InfoRow> next = rows_;
    for (auto& row : next) {
        row.value.clear();
        row.visible = false;
    }
    if (!next.empty()) {
        next[11].value = text;
        next[11].visible = true;
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
        if (s.objectName[0] != '\0') {
            next[0].value = s.objectName;
            next[0].visible = true;
        }
        next[1].value = s.typeName;
        next[1].visible = true;
        next[2].value = std::to_string(s.tag);          // 十进制 tag
        next[2].visible = true;
        next[3].value = std::to_string(s.color);
        next[3].visible = true;
        next[4].value = std::to_string(s.layer);        // 图层
        next[4].visible = true;
    }

    if (HasField(s.fields, Field_BBox)) {
        char buf[160];
        const std::string line = Compact(s.bboxMin[0]) + "," + Compact(s.bboxMin[1]) + "," + Compact(s.bboxMin[2]) +
                             " → " + Compact(s.bboxMax[0]) + "," + Compact(s.bboxMax[1]) + "," + Compact(s.bboxMax[2]);
        std::snprintf(buf, sizeof buf, "%s", line.c_str());
        next[5].value = buf;
        next[5].visible = true;
    }

    if (HasField(s.fields, Field_Area)) {
        next[6].value = Fixed3(s.area);
        next[6].visible = true;
        next[7].value = Fixed3(s.perimeter);
        next[7].visible = true;
    }

    if (HasField(s.fields, Field_Length)) {
        next[8].value = Fixed3(s.length);
        next[8].visible = true;
    }

    if (HasField(s.fields, Field_Volume)) {
        next[9].value = Fixed3(s.volume);
        next[9].visible = true;
    }

    if (HasField(s.fields, Field_Distance)) {
        next[10].value = Fixed4(s.distance);
        next[10].visible = true;
    }

    if (s.note[0] != '\0') {
        next[11].value = s.note;
        next[11].visible = true;
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
