#include "core/random.h"

#include <ctime>

namespace rune {
    namespace random {
        namespace {
            uint32_t& state()
            {
                static uint32_t holdrand = static_cast<uint32_t>(std::time(nullptr));
                return holdrand;
            }
        }

        void seed(uint32_t value)
        {
            state() = value;
        }

        int32_t rand()
        {
            uint32_t& holdrand = state();
            holdrand = holdrand * 214013u + 2531011u;
            return static_cast<int32_t>((holdrand >> 16) & 0x7fff);
        }

        float frand()
        {
            return static_cast<float>(static_cast<double>(rand()) / 32767.0);
        }
    }
}
