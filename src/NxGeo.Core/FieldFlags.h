#pragma once

enum FieldFlags : unsigned {
    Field_None     = 0,
    Field_L0       = 1u,    // 类型 / tag / 颜色 / 图层
    Field_BBox     = 2u,
    Field_Area     = 4u,    // 面面积 + 周长
    Field_Length   = 8u,    // 边/曲线长度
    Field_Volume   = 16u,   // 体体积（L2）
    Field_Distance = 32u    // 距离（L2）
};

inline bool HasField(unsigned fields, unsigned f) { return (fields & f) != 0; }
