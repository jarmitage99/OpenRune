#pragma once

#include <cstdint>

namespace rune {
    namespace random {
        void seed(uint32_t value);
        int32_t rand();
        float frand();
    }
}
