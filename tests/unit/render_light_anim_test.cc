#include "core/objects.h"
#include "engine/csg.h"
#include "engine/lightmap_builder.h"
#include "engine/props_engine.h"
#include "engine/resources.h"
#include "engine/world.h"
#include "render/lighting.h"
#include "render/lightmap.h"

#include <catch_amalgamated.hpp>

#include <cmath>

namespace {
    rune::UObject* makeLight(uint8_t type, uint8_t period, uint8_t phase)
    {
        rune::engine::initialize(RUNE_GAME_DATA);
        rune::UObject* light = rune::objects::newObject(rune::objects::findClass("Engine.Light"), rune::objects::packageObject(rune::Name("LightAnimTest")), rune::Name(), 0);
        light->loaded = true;
        rune::actor::LightType(light) = type;
        rune::actor::LightBrightness(light) = 128;
        rune::actor::LightHue(light) = 0;
        rune::actor::LightSaturation(light) = 255;
        rune::actor::LightPeriod(light) = period;
        rune::actor::LightPhase(light) = phase;
        rune::actor::LightRadius(light) = 3;
        return light;
    }

    float brightnessRatio(rune::UObject* light, float time)
    {
        uint8_t type = rune::actor::LightType(light);
        rune::Vector animated = rune::lightColor(light, time);
        rune::actor::LightType(light) = 1;
        rune::Vector steady = rune::lightColor(light, time);
        rune::actor::LightType(light) = type;
        return animated.x / steady.x;
    }

    rune::UObject* makeEffectLight(uint8_t effect)
    {
        rune::UObject* light = makeLight(1, 0, 0);
        rune::actor::LightEffect(light) = effect;
        return light;
    }

    template <typename T>
    T* make(const char* className, rune::UObject* outer)
    {
        rune::UObject* object = rune::objects::newObject(rune::csg::nativeClass(className), outer, rune::Name(), 0);
        object->loaded = true;
        return rune::cast<T>(object);
    }

    struct Room {
        rune::ULevel* level = nullptr;
        rune::UObject* light = nullptr;
    };

    Room makeLitRoom(const char* packageName)
    {
        rune::engine::initialize(RUNE_GAME_DATA);
        rune::UObject* package = rune::objects::packageObject(rune::Name(packageName));
        Room room;
        room.level = make<rune::ULevel>("Level", package);
        room.level->model = make<rune::UModel>("Model", package);
        rune::UObject* builder = make<rune::UObject>("Brush", package);
        rune::UModel* builderModel = make<rune::UModel>("Model", package);
        builderModel->polys = make<rune::UPolys>("Polys", package);
        rune::actor::Brush(builder) = builderModel;
        room.level->actors.push_back(nullptr);
        room.level->actors.push_back(builder);
        rune::csg::buildCube(*builderModel->polys, 512.0f, 512.0f, 256.0f, nullptr);
        rune::csg::addBrush(room.level, builder, rune::csg::CsgSubtract);
        room.light = rune::objects::newObject(rune::objects::findClass("Engine.Light"), package, rune::Name(), 0);
        room.light->loaded = true;
        rune::actor::Location(room.light) = { 0.0f, 0.0f, 0.0f };
        rune::actor::LightType(room.light) = 1;
        rune::actor::LightBrightness(room.light) = 64;
        rune::actor::LightRadius(room.light) = 32;
        rune::actor::LightPeriod(room.light) = 35;
        rune::actor::bStatic(room.light) = 1;
        room.level->actors.push_back(room.light);
        rune::csg::rebuild(room.level);
        rune::lightbuild::rebuild(room.level);
        return room;
    }

    const rune::BspSurf* floorSurf(const rune::UModel& model)
    {
        for (const rune::BspSurf& surf : model.surfs) {
            if (model.vectors[static_cast<size_t>(surf.vNormal)].z > 0.99f) {
                return &surf;
            }
        }
        return nullptr;
    }
}

TEST_CASE("pulse and subtle pulse follow the table sine of 35 T over period plus phase", "[data]")
{
    rune::UObject* pulse = makeLight(2, 35, 0);
    CHECK(brightnessRatio(pulse, 0.25f) == Catch::Approx(0.99f).margin(0.00001));
    CHECK(brightnessRatio(pulse, 0.75f) == Catch::Approx(0.21f).margin(0.00001));
    rune::actor::LightPhase(pulse) = 64;
    CHECK(brightnessRatio(pulse, 0.5f) == Catch::Approx(0.21f).margin(0.00001));
    rune::UObject* subtle = makeLight(7, 35, 0);
    CHECK(brightnessRatio(subtle, 0.25f) == Catch::Approx(0.99f).margin(0.00001));
    CHECK(brightnessRatio(subtle, 0.75f) == Catch::Approx(0.81f).margin(0.00001));
}

TEST_CASE("blink keeps the low bit of the phase counter at a large level time", "[data]")
{
    rune::UObject* blink = makeLight(3, 0, 0);
    float even = 200.0f;
    float odd = 200.0f + 1.0f / 65536.0f;
    REQUIRE(odd != even);
    CHECK(rune::lightColor(blink, even).x == Catch::Approx(0.412265f).margin(0.000001));
    CHECK(rune::lightColor(blink, odd).x == 0.0f);
    CHECK(rune::truncateToInt(3000000000.0) == -1294967296);
}

TEST_CASE("fast wave shifts the integer distance before applying the time phase", "[data]")
{
    rune::UObject* wave = makeEffectLight(6);
    rune::UObject* plain = makeEffectLight(0);
    rune::Vector location { 0, 0, 22 };
    rune::Vector point { 0, 0, 0 };
    rune::Vector up { 0, 0, 1 };
    CHECK(rune::sqrtApprox(484.0f) == 22.0f);
    float base = rune::lightIllumination(plain, location, point, up, 0);
    REQUIRE(base > 0.0f);
    CHECK(rune::lightIllumination(wave, location, point, up, 0) / base == Catch::Approx(0.9494409f).margin(0.00001));
    CHECK(rune::lightIllumination(wave, location, point, up, 1.0f) / base == Catch::Approx(0.8148050f).margin(0.00001));
}

TEST_CASE("auto panning advances 35 texels per second of zone speed and wraps at 1024 texels")
{
    CHECK(rune::autoPanTexels(2.0f, 1.5f) == 105.0f);
    CHECK(rune::autoPanTexels(100.0f, 2.0f) == 856.0f);
    CHECK(rune::autoPanTexels(1.0f, -1.0f) == 989.0f);
}

TEST_CASE("continuous randoms move at most a sixteenth per tick while bases are redrawn")
{
    rune::tickLightRandoms(10.0f);
    float bases[256];
    float randoms[256];
    for (int32_t i = 0; i < 256; i++) {
        bases[i] = rune::lightRandomBase(i);
        randoms[i] = rune::lightRandom(i);
    }
    rune::tickLightRandoms(10.0f + 1.0f / 35.0f + 0.001f);
    int32_t basesChanged = 0;
    int32_t randomsChanged = 0;
    float largestStep = 0;
    for (int32_t i = 0; i < 256; i++) {
        if (rune::lightRandomBase(i) != bases[i]) {
            basesChanged++;
        }
        if (rune::lightRandom(i) != randoms[i]) {
            randomsChanged++;
        }
        largestStep = std::max(largestStep, std::abs(rune::lightRandom(i) - randoms[i]));
    }
    CHECK(basesChanged > 250);
    CHECK(randomsChanged > 200);
    CHECK(largestStep <= 1.0f / 16.0f + 0.00001f);
}

TEST_CASE("a pulsing static light relights its surface over time while a steady one stays baked", "[data]")
{
    Room room = makeLitRoom("LightAnimRoomTest");
    rune::UModel& model = *room.level->model;
    const rune::BspSurf* floor = floorSurf(model);
    REQUIRE(floor != nullptr);
    REQUIRE(floor->lightMap >= 0);
    rune::Vector ambient {};
    size_t texel = 8 * 16 + 8;

    rune::SurfaceLighting steady = rune::prepareSurfaceLighting(&model, *floor, ambient);
    CHECK(steady.lights.empty());
    rune::LightmapTexels steadyEarly = rune::computeLightmap(&model, *floor, ambient, 0.25f);
    rune::LightmapTexels steadyLate = rune::computeLightmap(&model, *floor, ambient, 0.75f);
    REQUIRE(steadyEarly.colors.size() > texel);
    CHECK(steadyEarly.colors[texel].x > 0.05f);
    CHECK(steadyEarly.colors[texel].x == steadyLate.colors[texel].x);

    rune::actor::LightType(room.light) = 2;
    rune::SurfaceLighting pulsing = rune::prepareSurfaceLighting(&model, *floor, ambient);
    CHECK(pulsing.lights.size() == 1);
    rune::LightmapTexels bright = rune::computeLightmap(&model, *floor, ambient, 0.25f);
    rune::LightmapTexels dim = rune::computeLightmap(&model, *floor, ambient, 0.75f);
    CHECK(bright.colors[texel].x == Catch::Approx(steadyEarly.colors[texel].x * 0.99f).margin(2.0f / 127.5f));
    CHECK(dim.colors[texel].x == Catch::Approx(steadyEarly.colors[texel].x * 0.21f).margin(2.0f / 127.5f));
    CHECK(dim.colors[texel].x < bright.colors[texel].x - 0.05f);
}

TEST_CASE("a negative light darkens the lightmap instead of brightening it", "[data]")
{
    Room room = makeLitRoom("NegativeLightRoomTest");
    rune::UModel& model = *room.level->model;
    const rune::BspSurf* floor = floorSurf(model);
    REQUIRE(floor != nullptr);
    REQUIRE(floor->lightMap >= 0);
    size_t texel = 8 * 16 + 8;
    rune::LightmapTexels lit = rune::computeLightmap(&model, *floor, {}, 0.25f);
    REQUIRE(lit.colors.size() > texel);
    REQUIRE(lit.colors[texel].x > 0.05f);
    rune::actor::bNegativeLight(room.light) = 1;
    rune::LightmapTexels darkened = rune::computeLightmap(&model, *floor, {}, 0.25f);
    CHECK(darkened.colors[texel].x == 0.0f);
    CHECK(darkened.colors[texel].y == 0.0f);
    CHECK(darkened.colors[texel].z == 0.0f);
}
