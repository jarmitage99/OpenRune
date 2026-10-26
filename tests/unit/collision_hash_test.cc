#include "core/objects.h"
#include "engine/actor.h"
#include "engine/console.h"
#include "engine/props_engine.h"
#include "engine/world.h"

#include <catch_amalgamated.hpp>

#include <algorithm>
#include <memory>
#include <random>
#include <vector>

namespace {
    std::unique_ptr<rune::World> loadWorld(const std::string& map)
    {
        rune::engine::initialize(RUNE_GAME_DATA);
        rune::objects::unloadPackage(rune::Name(map));
        auto world = std::make_unique<rune::World>();
        REQUIRE(world->loadMap(map));
        return world;
    }

    void unloadWorld(std::unique_ptr<rune::World>& world, const std::string& map)
    {
        rune::console::resetInput(world->player());
        rune::objects::unloadPackage(rune::Name(map));
        world.reset();
    }

    std::vector<rune::UObject*> overlappingCylinders(rune::World& world, const rune::Vector& boxMin, const rune::Vector& boxMax)
    {
        std::vector<rune::UObject*> expected;
        for (rune::UObject* a : world.actors) {
            if (!a || a->destroyed || !rune::actor::bCollideActors(a) || rune::isBrush(a)) {
                continue;
            }
            rune::Vector location = rune::actor::Location(a);
            float radius = rune::actor::CollisionRadius(a);
            float height = rune::actor::CollisionHeight(a);
            if (location.x + radius < boxMin.x || location.x - radius > boxMax.x || location.y + radius < boxMin.y || location.y - radius > boxMax.y || location.z + height < boxMin.z || location.z - height > boxMax.z) {
                continue;
            }
            expected.push_back(a);
        }
        return expected;
    }

    std::vector<rune::UObject*> withoutBrushes(const std::vector<rune::UObject*>& candidates)
    {
        std::vector<rune::UObject*> result;
        for (rune::UObject* a : candidates) {
            if (!rune::isBrush(a)) {
                result.push_back(a);
            }
        }
        return result;
    }
}

TEST_CASE("collision hash returns exactly the overlapping cylinders in actor order", "[data]")
{
    std::unique_ptr<rune::World> world = loadWorld("RagnarVillage2");
    std::mt19937 random(12345);
    std::uniform_real_distribution<float> coordinate(-6000.0f, 6000.0f);
    std::uniform_real_distribution<float> height(-1500.0f, 1500.0f);
    std::uniform_real_distribution<float> size(1.0f, 3000.0f);
    size_t nonEmpty = 0;
    for (int i = 0; i < 2000; i++) {
        rune::Vector center { coordinate(random), coordinate(random), height(random) };
        rune::Vector half { size(random) * 0.5f, size(random) * 0.5f, size(random) * 0.25f };
        rune::Vector boxMin = center - half;
        rune::Vector boxMax = center + half;
        std::vector<rune::UObject*> expected = overlappingCylinders(*world, boxMin, boxMax);
        std::vector<rune::UObject*> actual = withoutBrushes(world->collisionHash.query(boxMin, boxMax));
        INFO("query " << i);
        REQUIRE(actual == expected);
        if (!expected.empty()) {
            nonEmpty++;
        }
    }
    CHECK(nonEmpty > 200);

    rune::UObject* player = world->player();
    REQUIRE(player != nullptr);
    rune::Vector start = rune::actor::Location(player);
    rune::Vector destination { 129, -1760, -276 };
    rune::console::execute(player, "teleport 129 -1760 -276 32768");
    rune::Vector moved = rune::actor::Location(player);
    CHECK((moved - destination).size() < 8.0f);
    std::vector<rune::UObject*> near = world->collisionHash.query(moved - rune::Vector { 4, 4, 4 }, moved + rune::Vector { 4, 4, 4 });
    CHECK(std::find(near.begin(), near.end(), player) != near.end());
    std::vector<rune::UObject*> old = world->collisionHash.query(start - rune::Vector { 4, 4, 4 }, start + rune::Vector { 4, 4, 4 });
    CHECK(std::find(old.begin(), old.end(), player) == old.end());

    for (int i = 0; i < 300; i++) {
        world->tick(1.0f / 30.0f);
    }
    for (int i = 0; i < 500; i++) {
        rune::Vector center { coordinate(random), coordinate(random), height(random) };
        rune::Vector half { size(random) * 0.5f, size(random) * 0.5f, size(random) * 0.25f };
        std::vector<rune::UObject*> expected = overlappingCylinders(*world, center - half, center + half);
        std::vector<rune::UObject*> actual = withoutBrushes(world->collisionHash.query(center - half, center + half));
        INFO("query after ticking " << i);
        REQUIRE(actual == expected);
    }
    unloadWorld(world, "RagnarVillage2");
}

TEST_CASE("collision hash finds movers where traces hit them", "[data]")
{
    std::unique_ptr<rune::World> world = loadWorld("Hel1");
    const rune::Vector directions[6] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
    size_t hits = 0;
    for (rune::UObject* a : world->actors) {
        if (!a || a->destroyed || !rune::isMover(a) || !rune::actor::bCollideActors(a)) {
            continue;
        }
        rune::Vector target = rune::actor::Location(a);
        for (const rune::Vector& direction : directions) {
            rune::CheckResult hit;
            if (!world->lineCheck(hit, target + direction * 400.0f, target, {}, nullptr, rune::TRACE_Movers)) {
                continue;
            }
            if (hit.actor != a) {
                continue;
            }
            hits++;
            std::vector<rune::UObject*> found = world->collisionHash.query(hit.location - rune::Vector { 1, 1, 1 }, hit.location + rune::Vector { 1, 1, 1 });
            INFO(a->name.c_str());
            CHECK(std::find(found.begin(), found.end(), a) != found.end());
        }
    }
    CHECK(hits >= 10);
    unloadWorld(world, "Hel1");
}

TEST_CASE("live actor snapshot drops destroyed actors and picks up spawned ones", "[data]")
{
    std::unique_ptr<rune::World> world = loadWorld("RagnarVillage2");
    std::shared_ptr<const std::vector<rune::UObject*>> before = world->liveActors();
    rune::UObject* victim = nullptr;
    for (rune::UObject* a : *before) {
        if (a->name == rune::Name("Sigurd0")) {
            victim = a;
        }
    }
    REQUIRE(victim != nullptr);
    REQUIRE(world->destroy(victim));
    std::shared_ptr<const std::vector<rune::UObject*>> after = world->liveActors();
    CHECK(after->size() == before->size() - 1);
    CHECK(std::find(after->begin(), after->end(), victim) == after->end());
    CHECK(std::find(before->begin(), before->end(), victim) != before->end());
    rune::UObject* spawned = world->spawn(rune::objects::findClass("RuneI.Keg"), nullptr, rune::Name(), rune::actor::Location(world->player()) + rune::Vector { 200, 0, 0 }, {}, nullptr, false);
    REQUIRE(spawned != nullptr);
    std::shared_ptr<const std::vector<rune::UObject*>> later = world->liveActors();
    CHECK(later->size() == after->size() + 1);
    CHECK(later->back() == spawned);
    unloadWorld(world, "RagnarVillage2");
}
