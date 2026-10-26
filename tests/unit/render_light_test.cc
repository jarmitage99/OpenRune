#include "core/objects.h"
#include "engine/props_engine.h"
#include "engine/world.h"
#include "render/lighting.h"

#include <catch_amalgamated.hpp>

namespace {
    rune::UObject* makeSpotlight()
    {
        rune::engine::initialize(RUNE_GAME_DATA);
        rune::UClass* lightClass = rune::objects::findClass("Engine.Light");
        REQUIRE(lightClass != nullptr);
        rune::UObject* light = rune::objects::newObject(lightClass, rune::objects::packageObject(rune::Name("Transient")), rune::Name(), 0);
        rune::actor::LightRadius(light) = 3;
        rune::actor::LightEffect(light) = 12;
        rune::actor::LightCone(light) = 128;
        rune::actor::Rotation(light) = { -16384, 0, 0 };
        return light;
    }
}

TEST_CASE("prepared spotlight illumination matches hand computed values", "[data]")
{
    rune::UObject* light = makeSpotlight();
    rune::Vector location { 0, 0, 100 };
    rune::Vector up { 0, 0, 1 };
    rune::PreparedLight prepared = rune::prepareLight(light, location);
    CHECK(prepared.radius == 100.0f);
    CHECK(prepared.spotDirection.z == Catch::Approx(1.0f).margin(0.0001));
    CHECK(prepared.outer == Catch::Approx(0.5f).margin(0.00001));
    CHECK(rune::lightIllumination(prepared, { 0, 0, 50 }, up, 0) == Catch::Approx(0.5f).margin(0.00005));
    CHECK(rune::lightIllumination(prepared, { 30, 0, 50 }, up, 0) == Catch::Approx(0.16499f).margin(0.00005));
    CHECK(rune::lightIllumination(prepared, { 60, 0, 90 }, up, 0) == 0.0f);
    CHECK(rune::lightIllumination(light, location, { 30, 0, 50 }, up, 0) == Catch::Approx(0.16499f).margin(0.00005));
}
