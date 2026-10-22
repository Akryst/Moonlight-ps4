#pragma once
#include <algorithm>

struct LibraryState {
    int selected = 0;
    bool details = false;
    int page() const { return selected / 4; }
    int slot() const { return selected % 4; }

    void move(int dx, int dy, int count) {
        if (details)
            return;
        selected = std::clamp(selected, 0, std::max(0, count - 1));
        if (count == 0)
            return;
        int next = selected + dx + dy * 4;
        if (next >= 0 && next < count)
            selected = next;
    }
};
