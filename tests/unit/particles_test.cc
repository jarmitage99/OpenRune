#include "core/objects.h"
#include "engine/actor.h"
#include "engine/particles.h"
#include "engine/props_engine.h"
#include "engine/world.h"

#include <catch_amalgamated.hpp>

namespace {
    rune::UObject* makeSystem()
    {
        rune::engine::initialize(RUNE_GAME_DATA);
        rune::UClass* cls = rune::objects::findClass("Engine.ParticleSystem");
        REQUIRE(cls != nullptr);
        rune::UObject* system = rune::objects::newObject(cls, rune::objects::packageObject(rune::Name("Transient")), rune::Name(), 0);
        REQUIRE(system != nullptr);
        return system;
    }

    size_t memberOffset(rune::UStruct* type, const char* name)
    {
        rune::UProperty* property = type->findProperty(rune::Name(name));
        REQUIRE(property != nullptr);
        return property->offset;
    }
}

TEST_CASE("ParticleSystem.Particle script layout matches the native FParticle", "[particles][data]")
{
    rune::engine::initialize(RUNE_GAME_DATA);
    rune::UClass* cls = rune::objects::findClass("Engine.ParticleSystem");
    REQUIRE(cls != nullptr);
    rune::UProperty* array = cls->findProperty(rune::Name("ParticleArray"));
    REQUIRE(array != nullptr);
    REQUIRE(array->structType != nullptr);
    CHECK(array->arrayDim == 64);
    CHECK(array->elementSize() == 128);
    CHECK(memberOffset(array->structType, "Velocity") == 12);
    CHECK(memberOffset(array->structType, "LifeSpan") == 24);
    CHECK(memberOffset(array->structType, "Alpha") == 44);
    CHECK(memberOffset(array->structType, "U0") == 56);
    CHECK(memberOffset(array->structType, "Points") == 72);
    CHECK(memberOffset(array->structType, "Style") == 120);
    CHECK(memberOffset(array->structType, "TextureIndex") == 121);
    CHECK(memberOffset(array->structType, "Valid") == 124);
}

TEST_CASE("emitter particles move, fade and grow over their lifespan", "[particles][data]")
{
    rune::UObject* system = makeSystem();
    rune::World world;
    rune::actor::Location(system) = { 100, 0, 0 };
    rune::particlesystem::ParticleCount(system) = 2;
    rune::particlesystem::ParticleType(system) = rune::particles::PART_Emitter;
    rune::particlesystem::ShapeVector(system) = {};
    rune::particlesystem::VelocityMin(system) = { 10, 0, 0 };
    rune::particlesystem::VelocityMax(system) = { 10, 0, 0 };
    rune::particlesystem::ScaleMin(system) = 2;
    rune::particlesystem::ScaleMax(system) = 2;
    rune::particlesystem::ScaleDeltaX(system) = 3;
    rune::particlesystem::LifeSpanMin(system) = 1;
    rune::particlesystem::LifeSpanMax(system) = 1;
    rune::particlesystem::AlphaStart(system) = 255;
    rune::particlesystem::AlphaEnd(system) = 0;
    rune::particlesystem::bAlphaFade(system) = 1;
    rune::particles::tick(world, system, 0.25f);
    rune::Particle& first = rune::particles::particlesOf(system)[0];
    CHECK(rune::particlesystem::IsLoaded(system) == 1);
    CHECK(first.valid == 1);
    CHECK(first.location.x == Catch::Approx(102.5f));
    CHECK(first.lifeSpan == Catch::Approx(0.75f));
    CHECK(first.alpha.x == Catch::Approx(1.0f));
    CHECK(first.xScale == Catch::Approx(2.0f));
    rune::particles::tick(world, system, 0.25f);
    CHECK(first.location.x == Catch::Approx(105.0f));
    CHECK(first.lifeSpan == Catch::Approx(0.5f));
    CHECK(first.alpha.x == Catch::Approx(0.75f));
    CHECK(first.xScale == Catch::Approx(3.0f));
    CHECK(first.yScale == Catch::Approx(2.0f));
}

TEST_CASE("one-shot system destroys itself the tick after its last particle dies", "[particles][data]")
{
    rune::UObject* system = makeSystem();
    rune::World world;
    rune::particlesystem::ParticleCount(system) = 1;
    rune::particlesystem::ParticleType(system) = rune::particles::PART_Emitter;
    rune::particlesystem::LifeSpanMin(system) = 0.5f;
    rune::particlesystem::LifeSpanMax(system) = 0.5f;
    rune::particlesystem::bOneShot(system) = 1;
    rune::particlesystem::bSystemOneShot(system) = 1;
    rune::particles::tick(world, system, 0.25f);
    rune::particles::tick(world, system, 0.25f);
    CHECK(rune::particles::particlesOf(system)[0].valid == 0);
    CHECK_FALSE(system->destroyed);
    rune::particles::tick(world, system, 0.25f);
    CHECK(system->destroyed);
}

TEST_CASE("beam builds a three-face prism per segment toward its target", "[particles][data]")
{
    rune::UObject* system = makeSystem();
    rune::World world;
    rune::actor::Location(system) = {};
    rune::particlesystem::ParticleType(system) = rune::particles::PART_Beam;
    rune::particlesystem::NumConPts(system) = 3;
    rune::particlesystem::ParticleCount(system) = 6;
    rune::particlesystem::BeamThickness(system) = 4;
    rune::particlesystem::BeamTextureScale(system) = 0.01f;
    rune::particlesystem::bUseTargetLocation(system) = 1;
    rune::particlesystem::TargetLocation(system) = { 200, 0, 0 };
    rune::particlesystem::bTaperEndPoint(system) = 1;
    rune::particles::tick(world, system, 0.1f);
    rune::Particle* list = rune::particles::particlesOf(system);
    CHECK(rune::particlesystem::ConnectionPoint(system, 1).x == Catch::Approx(100.0f));
    CHECK(rune::particlesystem::ConnectionPoint(system, 2).x == Catch::Approx(200.0f));
    CHECK(list[0].points[0].x == Catch::Approx(100.0f));
    CHECK(list[0].points[0].y == Catch::Approx(3.0f));
    CHECK(list[0].points[0].z == Catch::Approx(-2.3094688f));
    CHECK(list[0].points[2].z == Catch::Approx(2.3094688f));
    CHECK(list[1].u0 == Catch::Approx(0.33f));
    CHECK(list[1].u1 == Catch::Approx(0.66f));
    CHECK(list[0].v0 == Catch::Approx(1.0f));
    CHECK(list[0].v1 == Catch::Approx(0.0f));
    CHECK(list[3].v0 == Catch::Approx(2.0f));
    CHECK(list[3].v1 == Catch::Approx(1.0f));
    CHECK(list[3].points[0].x == Catch::Approx(200.0f));
    CHECK(list[3].points[1].y == Catch::Approx(0.0f));
    CHECK(list[5].location.x == Catch::Approx(200.0f));
}
