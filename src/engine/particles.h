#pragma once

#include "core/math.h"
#include "core/object.h"

#include <cstddef>
#include <cstdint>

namespace rune {
    class World;

    struct Particle {
        Vector location;
        Vector velocity;
        float lifeSpan = 0;
        float xScale = 0;
        float yScale = 0;
        float scaleStartX = 0;
        float scaleStartY = 0;
        Vector alpha;
        float u0 = 0;
        float v0 = 0;
        float u1 = 0;
        float v1 = 0;
        Vector points[4];
        uint8_t style = 0;
        uint8_t textureIndex = 0;
        uint32_t valid = 0;
    };

    static_assert(sizeof(Particle) == 128);
    static_assert(offsetof(Particle, lifeSpan) == 24);
    static_assert(offsetof(Particle, alpha) == 44);
    static_assert(offsetof(Particle, u0) == 56);
    static_assert(offsetof(Particle, points) == 72);
    static_assert(offsetof(Particle, style) == 120);
    static_assert(offsetof(Particle, valid) == 124);

    namespace particles {
        constexpr int32_t MaxParticles = 64;
        constexpr int32_t MaxRenderParticles = 63;

        constexpr uint8_t PART_Emitter = 0;
        constexpr uint8_t PART_SwipeEffect = 1;
        constexpr uint8_t PART_Generic = 2;
        constexpr uint8_t PART_Beam = 3;

        constexpr uint8_t PSPRITE_Normal = 0;
        constexpr uint8_t PSPRITE_Vertical = 1;
        constexpr uint8_t PSPRITE_Flat = 2;
        constexpr uint8_t PSPRITE_QuadUV = 3;

        constexpr uint8_t PSHAPE_Ellipsoid = 0;
        constexpr uint8_t PSHAPE_Line = 1;

        bool isParticleSystem(UObject* actor);
        Particle* particlesOf(UObject* system);
        void tick(World& world, UObject* system, float deltaSeconds);
        float boundsRadius(UObject* system);
    }
}
