#pragma once

#include "core/object.h"

#include <cstdint>
#include <vector>

namespace rune {
    struct AnimInfo {
        Name group;
        int32_t numFrames = 0;
        float rate = 0;
        std::vector<std::pair<float, Name>> notifys;
    };

    bool findAnim(UObject* actor, Name sequence, AnimInfo& info);
}
