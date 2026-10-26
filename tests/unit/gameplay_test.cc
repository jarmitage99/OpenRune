#include "core/objects.h"
#include "core/vm.h"
#include "engine/actor.h"
#include "engine/console.h"
#include "engine/input.h"
#include "engine/props_engine.h"
#include "engine/world.h"

#include <catch_amalgamated.hpp>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace {
    std::unique_ptr<rune::World> freshWorld(const std::string& map)
    {
        rune::engine::initialize(RUNE_GAME_DATA);
        rune::objects::unloadPackage(rune::Name(map));
        auto world = std::make_unique<rune::World>();
        world->inputHook = [](rune::UObject* player, float delta) {
            rune::console::readInput(player, delta);
        };
        REQUIRE(world->loadMap(map));
        return world;
    }

    void releaseWorld(std::unique_ptr<rune::World>& world, const std::string& map)
    {
        rune::console::resetInput(world->player());
        rune::objects::unloadPackage(rune::Name(map));
        world.reset();
    }

    rune::UObject* findActor(rune::World& world, const char* name)
    {
        for (rune::UObject* a : world.actors) {
            if (a && !a->destroyed && a->name == rune::Name(name)) {
                return a;
            }
        }
        return nullptr;
    }

    void tick(rune::World& world, int frames)
    {
        for (int i = 0; i < frames; i++) {
            world.tick(1.0f / 30.0f);
        }
    }

    std::string at(const rune::Vector& v)
    {
        return std::to_string(v.x) + " " + std::to_string(v.y) + " " + std::to_string(v.z);
    }

    int countLiving(rune::World& world, const char* className)
    {
        int count = 0;
        for (rune::UObject* a : world.actors) {
            if (a && !a->destroyed && !rune::actor::bDeleteMe(a) && a->isA(rune::Name(className))) {
                count++;
            }
        }
        return count;
    }
}

TEST_CASE("player grabs a climbable chain by jumping into it and climbs it", "[data]")
{
    std::unique_ptr<rune::World> world = freshWorld("Hel1");
    rune::UObject* player = world->player();
    REQUIRE(player != nullptr);
    rune::UObject* chain = findActor(*world, "ClimbableChain0");
    REQUIRE(chain != nullptr);
    rune::Vector rope = rune::actor::Location(chain);
    rune::console::execute(player, "teleport " + at(rope + rune::Vector { 60, 0, 40 }) + " 32768");
    rune::actor::Physics(player) = rune::PHYS_Falling;
    rune::actor::Velocity(player) = { -300.0f, 0.0f, 0.0f };
    tick(*world, 10);
    CHECK(rune::vm::stateName(player) == rune::Name("PlayerRopeClimbing"));
    CHECK(rune::actor::Physics(player) == rune::PHYS_Flying);
    CHECK(rune::actor::Location(player).x == Catch::Approx(rope.x).margin(0.01));
    CHECK(rune::actor::Location(player).y == Catch::Approx(rope.y).margin(0.01));
    float startZ = rune::actor::Location(player).z;
    uint8_t forward = rune::console::keyFromName("W");
    rune::console::key(forward, rune::IST_Press, 0);
    tick(*world, 30);
    rune::console::key(forward, rune::IST_Release, 0);
    float climbed = rune::actor::Location(player).z - startZ;
    CHECK(climbed == Catch::Approx(84.524f).margin(0.01));
    CHECK(rune::vm::stateName(player) == rune::Name("PlayerRopeClimbing"));
    float held = rune::actor::Location(player).z;
    tick(*world, 30);
    CHECK(rune::actor::Location(player).z == Catch::Approx(held).margin(0.01));
    releaseWorld(world, "Hel1");
}

TEST_CASE("traces do not report actors behind level geometry", "[data]")
{
    std::unique_ptr<rune::World> world = freshWorld("Hel1");
    rune::UObject* keg = findActor(*world, "Keg8");
    rune::UObject* chain = findActor(*world, "ClimbableChain0");
    REQUIRE(keg != nullptr);
    REQUIRE(chain != nullptr);
    rune::Vector start = rune::actor::Location(chain);
    rune::Vector end = rune::actor::Location(keg);
    std::vector<rune::CheckResult> hits = world->multiLineCheck(start, end, {}, nullptr, rune::TRACE_AllColliding);
    REQUIRE_FALSE(hits.empty());
    CHECK(hits.front().actor == world->levelInfo);
    bool kegHit = std::any_of(hits.begin(), hits.end(), [&](const rune::CheckResult& hit) {
        return hit.actor == keg;
    });
    CHECK_FALSE(kegHit);
    std::vector<rune::CheckResult> actorsOnly = world->multiLineCheck(start, end, {}, nullptr, rune::TRACE_AllColliding & ~(rune::TRACE_Level | rune::TRACE_LevelGeometry));
    bool kegWithoutLevel = std::any_of(actorsOnly.begin(), actorsOnly.end(), [&](const rune::CheckResult& hit) {
        return hit.actor == keg;
    });
    CHECK(kegWithoutLevel);
    releaseWorld(world, "Hel1");
}

TEST_CASE("level traces report the texture of the surface that was hit", "[data]")
{
    std::unique_ptr<rune::World> world = freshWorld("Hel1");
    rune::Vector start = rune::actor::Location(world->player());
    rune::CheckResult floor;
    REQUIRE(world->lineCheck(floor, start, start - rune::Vector { 0, 0, 3000 }, {}, nullptr, rune::TRACE_World));
    CHECK(floor.normal.z > 0.7f);
    REQUIRE(floor.material != nullptr);
    CHECK(floor.material->name == rune::Name("PANEL_03LICHEN"));
    REQUIRE(floor.material->outer != nullptr);
    CHECK(floor.material->outer->name == rune::Name("UNDERANCIENT"));
    rune::CheckResult wall;
    REQUIRE(world->lineCheck(wall, start, start + rune::Vector { 3000, 0, 0 }, {}, nullptr, rune::TRACE_World));
    CHECK(wall.normal.x < -0.5f);
    REQUIRE(wall.material != nullptr);
    CHECK(wall.material->name == rune::Name("Rock29_d"));
    REQUIRE(wall.material->outer != nullptr);
    CHECK(wall.material->outer->name == rune::Name("rock"));
    releaseWorld(world, "Hel1");
}

TEST_CASE("player falling into a water zone starts swimming", "[data]")
{
    std::unique_ptr<rune::World> world = freshWorld("Hel1");
    rune::UObject* player = world->player();
    REQUIRE(player != nullptr);
    rune::console::execute(player, "teleport 960 -5087 -600 0");
    tick(*world, 2);
    REQUIRE(rune::zoneOf(player) != nullptr);
    CHECK_FALSE(rune::zoneinfo::bWaterZone(rune::zoneOf(player)));
    CHECK(rune::actor::Physics(player) == rune::PHYS_Falling);
    tick(*world, 35);
    REQUIRE(rune::zoneOf(player) != nullptr);
    CHECK(rune::zoneOf(player)->name == rune::Name("WaterZone0"));
    CHECK(rune::zoneinfo::bWaterZone(rune::zoneOf(player)));
    CHECK(rune::actor::Physics(player) == rune::PHYS_Swimming);
    CHECK(rune::vm::stateName(player) == rune::Name("PlayerSwimming"));
    releaseWorld(world, "Hel1");
}

TEST_CASE("using a lever opens the door it is linked to", "[data]")
{
    std::unique_ptr<rune::World> world = freshWorld("Hel1");
    rune::UObject* player = world->player();
    rune::UObject* lever = findActor(*world, "lever7");
    rune::UObject* door = findActor(*world, "Mover4");
    REQUIRE(lever != nullptr);
    REQUIRE(door != nullptr);
    REQUIRE(rune::actor::Event(lever) == rune::actor::Tag(door));
    rune::Vector closed = rune::actor::Location(door);
    rune::Vector opened = rune::mover::BasePos(door) + rune::mover::KeyPos(door, 1);
    REQUIRE(rune::mover::KeyNum(door) == 0);
    REQUIRE(rune::mover::MoveTime(door, 0) == 10.0f);
    REQUIRE(rune::mover::DelayTime(door) == 2.0f);
    rune::vm::Call use(lever, rune::Name("UseTrigger"));
    REQUIRE(use.valid());
    use.obj(player).run();
    tick(*world, 45);
    CHECK(rune::actor::Location(door).z == Catch::Approx(closed.z).margin(0.01));
    tick(*world, 165);
    CHECK(rune::actor::Location(door).z == Catch::Approx((closed.z + opened.z) * 0.5f).margin(0.5));
    tick(*world, 210);
    CHECK(rune::mover::KeyNum(door) == 1);
    CHECK(rune::actor::Location(door).x == Catch::Approx(opened.x).margin(0.01));
    CHECK(rune::actor::Location(door).y == Catch::Approx(opened.y).margin(0.01));
    CHECK(rune::actor::Location(door).z == Catch::Approx(opened.z).margin(0.01));
    releaseWorld(world, "Hel1");
}

TEST_CASE("smashing a keg destroys it and scatters wood debris", "[data]")
{
    std::unique_ptr<rune::World> world = freshWorld("Hel1");
    rune::UObject* player = world->player();
    rune::UObject* chain = findActor(*world, "ClimbableChain0");
    REQUIRE(chain != nullptr);
    rune::UClass* kegClass = rune::objects::findClass("RuneI.Keg");
    rune::objects::flushLoads();
    REQUIRE(kegClass != nullptr);
    rune::UObject* keg = world->spawn(kegClass, nullptr, rune::Name(), rune::actor::Location(chain) + rune::Vector { 80, 0, 0 }, {}, nullptr, false);
    REQUIRE(keg != nullptr);
    REQUIRE(rune::actor::Mass(keg) == 100.0f);
    *reinterpret_cast<float*>(world->gameInfo->propertyAddress(rune::Name("DebrisPercentage"))) = 1.0f;
    int debrisBefore = countLiving(*world, "DebrisWood");
    rune::vm::Call damage(keg, rune::Name("JointDamaged"));
    REQUIRE(damage.valid());
    damage.i(50).obj(player).v(rune::actor::Location(keg)).v({}).n(rune::Name("blunt")).i(0).run();
    tick(*world, 1);
    CHECK((keg->destroyed || rune::actor::bDeleteMe(keg)));
    CHECK(countLiving(*world, "DebrisWood") - debrisBefore == 10);
    releaseWorld(world, "Hel1");
}

TEST_CASE("killing a goblin leaves a carcass and drops a weapon the player can pick up", "[data]")
{
    std::unique_ptr<rune::World> world = freshWorld("Goblin2");
    rune::UObject* player = world->player();
    rune::UObject* goblin = findActor(*world, "GoblinCrazy0");
    REQUIRE(goblin != nullptr);
    tick(*world, 30);
    rune::UObject* club = rune::pawn::Weapon(goblin);
    REQUIRE(club != nullptr);
    REQUIRE(club->isA(rune::Name("boneclub")));
    REQUIRE(rune::actor::Owner(club) == goblin);
    REQUIRE(rune::pawn::Health(goblin) == 50);
    REQUIRE(countLiving(*world, "Carcass") == 0);
    rune::vm::Call damage(goblin, rune::Name("JointDamaged"));
    REQUIRE(damage.valid());
    damage.i(60).obj(player).v(rune::actor::Location(goblin)).v({}).n(rune::Name("blunt")).i(0).run();
    tick(*world, 150);
    CHECK(goblin->destroyed);
    CHECK(countLiving(*world, "RuneCarcass") == 1);
    CHECK(rune::actor::Owner(club) == nullptr);
    CHECK(rune::vm::stateName(club) == rune::Name("Pickup"));
    rune::Vector spot = rune::actor::Location(club);
    rune::console::execute(player, "teleport " + at(spot + rune::Vector { 50, 0, 30 }) + " 32768");
    tick(*world, 30);
    REQUIRE(rune::actor::Physics(player) == rune::PHYS_Walking);
    rune::console::execute(player, "Use");
    tick(*world, 30);
    CHECK(rune::pawn::Weapon(player) == club);
    CHECK(rune::actor::Owner(club) == player);
    releaseWorld(world, "Goblin2");
}

TEST_CASE("player picks up a weapon and throws it where he is looking", "[data]")
{
    std::unique_ptr<rune::World> world = freshWorld("Goblin2");
    rune::UObject* player = world->player();
    tick(*world, 30);
    REQUIRE(rune::actor::Physics(player) == rune::PHYS_Walking);
    rune::UClass* axeClass = rune::objects::findClass("RuneI.handaxe");
    rune::objects::flushLoads();
    REQUIRE(axeClass != nullptr);
    rune::Vector start = rune::actor::Location(player);
    rune::UObject* axe = world->spawn(axeClass, nullptr, rune::Name(), start + rune::Vector { 50, 0, 0 }, {}, nullptr, false);
    REQUIRE(axe != nullptr);
    REQUIRE(rune::actor::Mass(axe) == 10.0f);
    tick(*world, 10);
    rune::console::execute(player, "Use");
    tick(*world, 30);
    REQUIRE(rune::pawn::Weapon(player) == axe);
    rune::pawn::ViewRotation(player) = { 0, 0, 0 };
    rune::actor::Rotation(player) = { 0, 0, 0 };
    rune::console::execute(player, "Throw");
    bool released = false;
    for (int i = 0; i < 60 && !released; i++) {
        tick(*world, 1);
        released = rune::vm::stateName(axe) == rune::Name("Throw");
    }
    REQUIRE(released);
    CHECK(rune::pawn::Weapon(player) == nullptr);
    CHECK(rune::actor::Physics(axe) == rune::PHYS_Falling);
    CHECK(rune::actor::Velocity(axe).x == Catch::Approx(750.0f).margin(0.5));
    CHECK(rune::actor::Velocity(axe).y == Catch::Approx(0.0f).margin(0.5));
    float releaseX = rune::actor::Location(axe).x;
    tick(*world, 6);
    CHECK(rune::actor::Location(axe).x - releaseX == Catch::Approx(150.0f).margin(0.5));
    releaseWorld(world, "Goblin2");
}
