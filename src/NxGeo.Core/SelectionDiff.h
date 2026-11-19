#pragma once

#include <vector>

// 从当前选择列表里挑出「最新选中」的对象：
//  - 优先返回本次新出现（上一次没有）的标签，多个时取最后一个
//  - 没有新标签时退回列表末尾（单选或重选同一批）
//  - 空列表返回 0
unsigned NewestSelected(const std::vector<unsigned>& previous,
                        const std::vector<unsigned>& current);
