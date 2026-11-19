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

std::string WithUnit(double v, const char* unit, int decimals) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f %s", decimals, v, unit);
    return std::string(buf);
}

std::string MassText(double kg) {
    char buf[64];
    if (kg < 1.0) {
        std::snprintf(buf, sizeof buf, "%.2f g", kg * 1000.0);
    } else {
        std::snprintf(buf, sizeof buf, "%.3f kg", kg);
    }
    return std::string(buf);
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
    rows_.resize(19);
    const char* titles[19] = {
        "名称", "类型", "tag", "颜色", "图层", "包围盒", "面积", "周长", "长度",
        "半径", "法向", "拔模角", "体积", "表面积", "质量", "距离", "特征", "零件", "备注"
    };
    for (int i = 0; i < 19; ++i) rows_[i].title = titles[i];
}
void InfoViewModel::ShowMessage(const std::string& text) {
    std::vector<InfoRow> next = rows_;
    for (auto& row : next) {
        row.value.clear();
        row.visible = false;
    }
    if (!next.empty()) {
        next[18].value = text;
        next[18].visible = true;
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
        next[2].value = std::to_string(s.tag);
        next[2].visible = true;
        next[3].value = std::to_string(s.color);
        next[3].visible = true;
        next[4].value = std::to_string(s.layer);
        next[4].visible = true;
        if (s.featureName[0] != '\0') {
            next[16].value = s.featureName;
            next[16].visible = true;
        }
        if (s.componentName[0] != '\0') {
            next[17].value = s.componentName;
            next[17].visible = true;
        }
    }

    if (HasField(s.fields, Field_BBox)) {
        const std::string line = Compact(s.bboxMin[0]) + "," + Compact(s.bboxMin[1]) + "," + Compact(s.bboxMin[2]) +
                                 " → " + Compact(s.bboxMax[0]) + "," + Compact(s.bboxMax[1]) + "," + Compact(s.bboxMax[2]);
        next[5].value = line + " mm";
        next[5].visible = true;
    }

    if (HasField(s.fields, Field_Area)) {
        next[6].value = WithUnit(s.area, "mm²", 3);
        next[6].visible = true;
        next[7].value = WithUnit(s.perimeter, "mm", 3);
        next[7].visible = true;
    }

    if (HasField(s.fields, Field_Length)) {
        next[8].value = WithUnit(s.length, "mm", 3);
        next[8].visible = true;
    }

    if (HasField(s.fields, Field_Radius)) {
        next[9].value = WithUnit(s.radius, "mm", 3);
        next[9].visible = true;
    }

    if (HasField(s.fields, Field_Normal)) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "%.3f,%.3f,%.3f", s.normal[0], s.normal[1], s.normal[2]);
        next[10].value = buf;
        next[10].visible = true;
        std::snprintf(buf, sizeof buf, "%.2f°", s.draftAngle);
        next[11].value = buf;
        next[11].visible = true;
    }

    if (HasField(s.fields, Field_Volume)) {
        next[12].value = WithUnit(s.volume, "mm³", 3);
        next[12].visible = true;
    }

    if (HasField(s.fields, Field_Surface)) {
        next[13].value = WithUnit(s.surfaceArea, "mm²", 3);
        next[13].visible = true;
    }

    if (HasField(s.fields, Field_Mass)) {
        next[14].value = MassText(s.mass);
        next[14].visible = true;
    }

    if (HasField(s.fields, Field_Distance)) {
        std::string distanceText = WithUnit(s.distance, "mm", 4);
        if (s.distanceTo[0] != '\0') {
            distanceText += "（";
            distanceText += s.distanceTo;
            distanceText += "）";
        }
        next[15].value = distanceText;
        next[15].visible = true;
    }

    if (s.note[0] != '\0') {
        next[18].value = s.note;
        next[18].visible = true;
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