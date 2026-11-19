#pragma once

enum class ObjKind : unsigned char {
    None  = 0,
    Body  = 1,
    Face  = 2,
    Edge  = 3,
    Curve = 4,
    Other = 5
};
