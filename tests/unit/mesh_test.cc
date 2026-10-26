#include "core/objects.h"
#include "engine/world.h"
#include "render/lighting.h"
#include "render/lightmap.h"

#include <catch_amalgamated.hpp>

namespace {
    void initializeEngine()
    {
        static bool initialized = false;
        if (!initialized) {
            rune::engine::initialize(RUNE_GAME_DATA);
            initialized = true;
        }
    }
}

TEST_CASE("Unreal hue saturation brightness conversion")
{
    rune::Vector white = rune::fGetHSV(0, 255, 255);
    CHECK(white.x == Catch::Approx(0.82128f).margin(0.0005));
    CHECK(white.y == Catch::Approx(0.82128f).margin(0.0005));
    CHECK(white.z == Catch::Approx(0.82128f).margin(0.0005));
    rune::Vector red = rune::fGetHSV(0, 0, 255);
    CHECK(red.x == Catch::Approx(0.82128f).margin(0.0005));
    CHECK(red.y == Catch::Approx(0.0f).margin(0.0005));
    CHECK(red.z == Catch::Approx(0.0f).margin(0.0005));
    rune::Vector black = rune::fGetHSV(40, 0, 0);
    CHECK(black.x == 0.0f);
}

TEST_CASE("light distance falloff")
{
    CHECK(rune::lightDistanceFalloff(0.25f) == Catch::Approx(0.5f).margin(0.0005));
    CHECK(rune::lightDistanceFalloff(0.81f) == Catch::Approx(0.0281f).margin(0.0005));
    CHECK(rune::lightDistanceFalloff(0.0001f) == Catch::Approx(0.99927f).margin(0.0001));
    CHECK(rune::lightDistanceFalloff(1.0f) == Catch::Approx(0.0f).margin(0.000001));
    CHECK(rune::lightDistanceFalloff(1.001f) == 0.0f);
}

TEST_CASE("lightmap texels lie on their surface plane", "[data]")
{
    initializeEngine();
    rune::Linker* linker = rune::objects::loadPackage(rune::Name("DM-Hel"));
    REQUIRE(linker != nullptr);
    rune::ULevel* level = rune::cast<rune::ULevel>(linker->object(linker->findExport(rune::Name("MyLevel"), rune::Name("Level"), "*")));
    REQUIRE(level != nullptr);
    rune::objects::ensureLoaded(level);
    rune::UModel* model = level->model;
    rune::objects::ensureLoaded(model);
    REQUIRE(model != nullptr);
    size_t checked = 0;
    for (const rune::BspSurf& surf : model->surfs) {
        if (surf.lightMap < 0 || checked >= 50) {
            continue;
        }
        const rune::LightMapIndex& index = model->lightMaps[static_cast<size_t>(surf.lightMap)];
        rune::Vector normal = model->vectors[static_cast<size_t>(surf.vNormal)];
        rune::Vector base = model->points[static_cast<size_t>(surf.pBase)];
        rune::Vector corner = rune::lightmapTexelPoint(model, surf, index.uClamp - 1, index.vClamp - 1);
        CHECK(normal.dot(corner - base) == Catch::Approx(0.0f).margin(0.05));
        checked++;
    }
    CHECK(checked == 50);
}
