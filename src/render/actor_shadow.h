#pragma once

#include "core/math.h"
#include "engine/resources.h"
#include "render/lighting.h"

#include <cstdint>
#include <vector>

namespace rune {
    namespace shadow {
        constexpr int32_t TextureSize = 128;

        struct Projection {
            Vector origin;
            Vector xAxis;
            Vector yAxis;
            Vector zAxis;
            float uScale = 1;
            float vScale = 1;
        };

        struct Polygon {
            std::vector<Vector> points;
            std::vector<float> u;
            std::vector<float> v;
        };

        void updateVector(UObject* actor, const std::vector<PreparedLight>& lights);
        UTexture* texture(UObject* actor);
        Projection projection(UObject* actor, UTexture* texture);
        void clear(UTexture* texture);
        void drawTriangle(UTexture* texture, const Projection& projection, const Vector* triangle);
        void receivers(const UModel& model, const Projection& projection, const Vector& eye, std::vector<Polygon>& out);
    }
}
