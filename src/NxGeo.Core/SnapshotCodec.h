#pragma once

#include "InfoSnapshot.h"

// 把快照编码进一个 SlotSize 字节的槽（先清零再覆盖，足够安全地整块 memcpy）。
void EncodeSnapshot(const InfoSnapshot& s, unsigned char* out512);

// 从槽解码；魔数或版本不符时抛 std::runtime_error。
InfoSnapshot DecodeSnapshot(const unsigned char* in512);
