#include "SelectionDiff.h"

#include <algorithm>

unsigned NewestSelected(const std::vector<unsigned>& previous,
                        const std::vector<unsigned>& current) {
    if (current.empty()) return 0;

    for (auto it = current.rbegin(); it != current.rend(); ++it) {
        if (std::find(previous.begin(), previous.end(), *it) == previous.end()) {
            return *it;
        }
    }
    return current.back();
}
