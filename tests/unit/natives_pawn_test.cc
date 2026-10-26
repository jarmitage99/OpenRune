#include "core/objects.h"
#include "core/vm.h"
#include "engine/actor.h"
#include "engine/latent.h"
#include "engine/path_builder.h"
#include "engine/props_engine.h"
#include "engine/resources.h"
#include "engine/world.h"

#include <catch_amalgamated.hpp>

#include <cmath>
#include <memory>

namespace {
    struct DwarfMap {
        std::unique_ptr<rune::World> world;

        DwarfMap()
        {
            rune::engine::initialize(RUNE_GAME_DATA);
            rune::objects::unloadPackage(rune::Name("DwarfMap2"));
            world = std::make_unique<rune::World>();
            REQUIRE(world->loadMap("DwarfMap2"));
        }

        ~DwarfMap()
        {
            world.reset();
            rune::objects::unloadPackage(rune::Name("DwarfMap2"));
        }

        rune::UObject* actor(const char* name)
        {
            for (rune::UObject* a : world->actors) {
                if (a && !a->destroyed && a->name == rune::Name(name)) {
                    return a;
                }
            }
            return nullptr;
        }

        rune::UObject* player()
        {
            rune::UObject* start = actor("PlayerStart0");
            rune::UObject* p = world->player();
            REQUIRE(start != nullptr);
            REQUIRE(p != nullptr);
            rune::actor::Location(p) = rune::actor::Location(start) + rune::Vector { 0, 0, rune::actor::CollisionHeight(p) - rune::actor::CollisionHeight(start) };
            rune::actor::Physics(p) = rune::PHYS_Walking;
            world->collisionHash.update(p);
            return p;
        }
    };

    rune::UObject* walker(DwarfMap& map)
    {
        rune::UObject* goblin = map.actor("GoblinWarrior0");
        REQUIRE(goblin != nullptr);
        rune::actor::Physics(goblin) = rune::PHYS_Walking;
        rune::actor::Velocity(goblin) = {};
        rune::actor::Acceleration(goblin) = {};
        rune::vm::stateFrame(goblin).latentAction = 0;
        return goblin;
    }

    void place(DwarfMap& map, rune::UObject* a, const rune::Vector& location)
    {
        rune::actor::Location(a) = location;
        map.world->collisionHash.update(a);
    }

    void moveTo(rune::UObject* self, const rune::Vector& destination, float speed)
    {
        rune::vm::Call call(self, rune::Name("MoveTo"));
        REQUIRE(call.valid());
        call.v(destination).f(speed).run();
    }

    rune::UObject* objectResult(rune::vm::Call& call)
    {
        return *reinterpret_cast<rune::UObject**>(call.returnValue());
    }

    bool boolResult(rune::vm::Call& call)
    {
        return *reinterpret_cast<uint32_t*>(call.returnValue()) != 0;
    }

    rune::UObject* pathNode(DwarfMap& map)
    {
        for (rune::UObject* a : map.world->actors) {
            if (a && !a->destroyed && a->isA(rune::Name("PathNode")) && rune::navigationpoint::Paths(a, 0) >= 0) {
                return a;
            }
        }
        FAIL("no connected PathNode");
        return nullptr;
    }
}

TEST_CASE("MoveTo caps the speed at MaxDesiredSpeed and times the move by 1.3 times the distance over the ground speed", "[data]")
{
    DwarfMap map;
    rune::UObject* goblin = walker(map);
    rune::pawn::GroundSpeed(goblin) = 300.0f;
    rune::pawn::MaxDesiredSpeed(goblin) = 0.5f;
    rune::Vector start = rune::actor::Location(goblin);
    moveTo(goblin, start + rune::Vector { 390, 0, 0 }, 0.8f);
    CHECK(rune::pawn::DesiredSpeed(goblin) == 0.5f);
    CHECK(rune::pawn::MoveTimer(goblin) == Catch::Approx(4.38).margin(1e-4));
    moveTo(goblin, start + rune::Vector { 390, 0, 0 }, 0.3f);
    CHECK(rune::pawn::DesiredSpeed(goblin) == 0.3f);
    CHECK(rune::pawn::MoveTimer(goblin) == Catch::Approx(6.633333).margin(1e-4));
    moveTo(goblin, start + rune::Vector { 390, 0, 0 }, 0.0f);
    CHECK(rune::pawn::MoveTimer(goblin) == 0.5f);
    CHECK(rune::vm::stateFrame(goblin).latentAction == 501);
}

TEST_CASE("MoveToward a pawn allows 1.2 seconds and walking toward a pawn keeps the pitch", "[data]")
{
    DwarfMap map;
    rune::UObject* goblin = walker(map);
    rune::UObject* player = map.player();
    rune::actor::Location(player) = rune::actor::Location(goblin) + rune::Vector { 300, 0, 300 };
    rune::vm::Call call(goblin, rune::Name("MoveToward"));
    REQUIRE(call.valid());
    call.obj(player).f(1.0f).run();
    CHECK(rune::pawn::MoveTimer(goblin) == 1.2f);
    CHECK(rune::pawn::MoveTarget(goblin) == player);
    CHECK(std::abs(rune::actor::DesiredRotation(goblin).pitch - 8192) <= 1);
}

TEST_CASE("a move ends within 16 units in 2D when the height difference is below 50", "[data]")
{
    DwarfMap map;
    rune::UObject* goblin = walker(map);
    REQUIRE(rune::actor::CollisionHeight(goblin) < 49.0f);
    rune::actor::Physics(goblin) = rune::PHYS_Flying;
    rune::Vector start = rune::actor::Location(goblin);
    moveTo(goblin, start + rune::Vector { 12, 0, 40 }, 1.0f);
    CHECK(rune::actor::Acceleration(goblin).x == 0.0f);
    CHECK(rune::actor::Acceleration(goblin).z == 0.0f);
    moveTo(goblin, start + rune::Vector { 12, 0, 60 }, 1.0f);
    double length = std::sqrt(12.0 * 12.0 + 60.0 * 60.0);
    CHECK(rune::actor::Acceleration(goblin).x == Catch::Approx(12.0 / length * rune::pawn::AccelRate(goblin)).margin(1e-3));
    CHECK(rune::actor::Acceleration(goblin).z == Catch::Approx(60.0 / length * rune::pawn::AccelRate(goblin)).margin(1e-3));
    moveTo(goblin, start + rune::Vector { 12, 0, 49 }, 1.0f);
    CHECK(rune::actor::Acceleration(goblin).x == 0.0f);
    CHECK(rune::actor::Acceleration(goblin).z == 0.0f);
}

TEST_CASE("TurnTo leaves the acceleration and TurnToward and StrafeFacing aim with FaceTarget", "[data]")
{
    DwarfMap map;
    rune::UObject* goblin = walker(map);
    rune::UObject* player = map.player();
    rune::actor::Acceleration(goblin) = { 100, 0, 0 };
    {
        rune::vm::Call call(goblin, rune::Name("TurnTo"));
        REQUIRE(call.valid());
        call.v(rune::actor::Location(goblin) + rune::Vector { 0, 100, 0 }).run();
    }
    CHECK(rune::actor::Acceleration(goblin).x == 100.0f);
    CHECK(rune::vm::stateFrame(goblin).latentAction == 509);
    rune::UObject* marker = pathNode(map);
    rune::pawn::MoveTarget(goblin) = marker;
    {
        rune::vm::Call call(goblin, rune::Name("TurnToward"));
        REQUIRE(call.valid());
        call.obj(player).run();
    }
    CHECK(rune::pawn::MoveTarget(goblin) == marker);
    CHECK(rune::pawn::FaceTarget(goblin) == player);
    rune::vm::stateFrame(goblin).latentAction = 0;
    {
        rune::vm::Call call(goblin, rune::Name("StrafeFacing"));
        REQUIRE(call.valid());
        call.v(rune::actor::Location(goblin)).obj(nullptr).run();
    }
    CHECK(rune::vm::stateFrame(goblin).latentAction == 0);
}

TEST_CASE("WaitForLanding waits 2.5 seconds before LongFall", "[data]")
{
    DwarfMap map;
    rune::UObject* goblin = walker(map);
    rune::actor::Physics(goblin) = rune::PHYS_Falling;
    rune::vm::Call call(goblin, rune::Name("WaitForLanding"));
    REQUIRE(call.valid());
    call.run();
    CHECK(rune::actor::LatentFloat(goblin) == 2.5f);
    CHECK(rune::vm::stateFrame(goblin).latentAction == 528);
}

TEST_CASE("ClearPaths resets every navigation point", "[data]")
{
    DwarfMap map;
    rune::UObject* goblin = walker(map);
    rune::UObject* node = pathNode(map);
    rune::navigationpoint::visitedWeight(node) = 5;
    rune::navigationpoint::bEndPoint(node) = 1;
    rune::navigationpoint::cost(node) = 77;
    rune::navigationpoint::ExtraCost(node) = 12;
    rune::vm::Call call(goblin, rune::Name("ClearPaths"));
    REQUIRE(call.valid());
    call.run();
    CHECK(rune::navigationpoint::visitedWeight(node) == 10000000);
    CHECK(rune::navigationpoint::bEndPoint(node) == 0);
    CHECK(rune::navigationpoint::cost(node) == 12);
}

TEST_CASE("FindRandomDest picks a navigation point the pawn can reach", "[data]")
{
    DwarfMap map;
    rune::UObject* goblin = walker(map);
    rune::UObject* node = pathNode(map);
    place(map, goblin, rune::actor::Location(node) + rune::Vector { 0, 0, rune::actor::CollisionHeight(goblin) - rune::actor::CollisionHeight(node) });
    rune::vm::Call call(goblin, rune::Name("FindRandomDest"));
    REQUIRE(call.valid());
    call.b(true).run();
    rune::UObject* destination = objectResult(call);
    REQUIRE(destination != nullptr);
    CHECK(destination->isA(rune::engineClasses().navigationPoint));
    CHECK(rune::navigationpoint::bEndPoint(destination) != 0);
}

TEST_CASE("FindPathToward fills RouteCache with the route, clears SpecialPause and never returns the goal pawn", "[data]")
{
    DwarfMap map;
    rune::UObject* goblin = walker(map);
    rune::UObject* player = map.player();
    rune::UObject* node = map.actor("PathNode26");
    REQUIRE(node != nullptr);
    REQUIRE((rune::actor::Location(node) - rune::actor::Location(player)).size() > 600.0f);
    place(map, goblin, rune::actor::Location(node) + rune::Vector { 0, 0, rune::actor::CollisionHeight(goblin) - rune::actor::CollisionHeight(node) });
    rune::pawn::RouteCache(goblin, 0) = nullptr;
    rune::pawn::RouteCache(goblin, 1) = nullptr;
    rune::pawn::SpecialPause(goblin) = 3.0f;
    rune::vm::Call call(goblin, rune::Name("FindPathToward"));
    REQUIRE(call.valid());
    call.obj(player).b(false).b(true).run();
    rune::UObject* next = objectResult(call);
    CHECK(rune::pawn::SpecialPause(goblin) == 0.0f);
    REQUIRE(next != nullptr);
    CHECK(next != player);
    CHECK(rune::pawn::RouteCache(goblin, 0) == next);
    CHECK(rune::pawn::RouteCache(goblin, 1) == map.actor("PlayerStart0"));
}

TEST_CASE("pointReachable fails while falling and limits points to 800 units in 2D", "[data]")
{
    DwarfMap map;
    rune::UObject* player = map.player();
    rune::Vector start = rune::actor::Location(player);
    REQUIRE(map.world->fastLineCheck(start + rune::Vector { -1100, 0, 0 }, start));
    auto reachable = [&](const rune::Vector& point) {
        rune::vm::Call call(player, rune::Name("pointReachable"));
        REQUIRE(call.valid());
        call.v(point).run();
        return boolResult(call);
    };
    CHECK(reachable(start + rune::Vector { -100, 0, 0 }));
    CHECK(reachable(start + rune::Vector { -790, 0, 0 }));
    CHECK_FALSE(reachable(start + rune::Vector { -850, 0, 0 }));
    rune::actor::Physics(player) = rune::PHYS_Falling;
    CHECK_FALSE(reachable(start + rune::Vector { -100, 0, 0 }));
}

TEST_CASE("EAdjustJump simulates the arc in 0.05 second steps from the current velocity", "[data]")
{
    DwarfMap map;
    rune::UObject* goblin = walker(map);
    rune::UObject* zone = rune::zoneOf(goblin);
    REQUIRE(zone != nullptr);
    rune::zoneinfo::ZoneGravity(zone) = { 0, 0, -950 };
    rune::actor::Velocity(goblin) = { 0, 0, 500 };
    rune::pawn::JumpZ(goblin) = 400.0f;
    rune::pawn::GroundSpeed(goblin) = 2000.0f;
    rune::pawn::Destination(goblin) = rune::actor::Location(goblin) + rune::Vector { 300, 0, 0 };
    rune::vm::Call call(goblin, rune::Name("EAdjustJump"));
    REQUIRE(call.valid());
    call.run();
    rune::Vector velocity = *reinterpret_cast<rune::Vector*>(call.returnValue());
    CHECK(velocity.z == 500.0f);
    CHECK(velocity.x == Catch::Approx(299.248).margin(0.05));
    CHECK(velocity.y == Catch::Approx(0.0).margin(1e-3));
}

TEST_CASE("RemovePawn unlinks the pawn and leaves its nextPawn", "[data]")
{
    DwarfMap map;
    rune::UObject* goblin = walker(map);
    rune::UObject* level = map.world->levelInfo;
    rune::UObject* first = rune::levelinfo::PawnList(level);
    REQUIRE(first != nullptr);
    rune::UObject* previous = nullptr;
    for (rune::UObject* p = first; p && p != goblin; p = rune::pawn::nextPawn(p)) {
        previous = p;
    }
    REQUIRE(previous != nullptr);
    REQUIRE(rune::pawn::nextPawn(previous) == goblin);
    rune::UObject* after = rune::pawn::nextPawn(goblin);
    rune::vm::Call call(goblin, rune::Name("RemovePawn"));
    REQUIRE(call.valid());
    call.run();
    CHECK(rune::pawn::nextPawn(previous) == after);
    CHECK(rune::pawn::nextPawn(goblin) == after);
}

TEST_CASE("PickTarget ignores the incoming best distance and only takes pawns within 2000 units", "[data]")
{
    DwarfMap map;
    rune::UObject* goblin = walker(map);
    rune::UObject* player = map.player();
    rune::Vector start = rune::actor::Location(player);
    REQUIRE(map.world->fastLineCheck(start + rune::Vector { -2200, 0, 0 }, start));
    place(map, goblin, start + rune::Vector { -500, 0, 0 });
    REQUIRE(rune::pawn::Health(goblin) > 0);
    REQUIRE(rune::actor::bProjTarget(goblin) != 0);
    auto pick = [&](float& aim, float& distance) {
        rune::vm::Call call(player, rune::Name("PickTarget"));
        REQUIRE(call.valid());
        call.f(aim).f(distance).v({ -1, 0, 0 }).v(start).run();
        aim = *reinterpret_cast<float*>(call.parameter(0));
        distance = *reinterpret_cast<float*>(call.parameter(1));
        return objectResult(call);
    };
    float aim = 0.5f;
    float distance = 0.0f;
    CHECK(pick(aim, distance) == goblin);
    CHECK(aim == 1.0f);
    CHECK(distance == 500.0f);
    place(map, goblin, start + rune::Vector { -2100, 0, 0 });
    aim = 0.5f;
    distance = 0.0f;
    CHECK(pick(aim, distance) == nullptr);
}

TEST_CASE("FindStairRotation eases the view pitch toward level on flat ground", "[data]")
{
    DwarfMap map;
    rune::UObject* player = map.player();
    rune::pawn::ViewRotation(player) = { 2000, 32768, 0 };
    auto stair = [&](float delta) {
        rune::vm::Call call(player, rune::Name("FindStairRotation"));
        REQUIRE(call.valid());
        call.f(delta).run();
        return *reinterpret_cast<int32_t*>(call.returnValue());
    };
    CHECK(stair(0.1f) == 399);
    CHECK(stair(0.5f) == 2000);
    rune::pawn::ViewRotation(player) = { 60000, 32768, 0 };
    stair(0.1f);
    CHECK(rune::pawn::ViewRotation(player).pitch == -5536);
}

TEST_CASE("PickWallAdjust steps a walking pawn sideways by its radius plus 16 and refuses while falling", "[data]")
{
    DwarfMap map;
    rune::UObject* goblin = walker(map);
    rune::UObject* player = map.player();
    rune::Vector start = rune::actor::Location(player) + rune::Vector { -600, 0, 0 };
    REQUIRE(map.world->fastLineCheck(start + rune::Vector { -400, 0, 0 }, start));
    place(map, player, start + rune::Vector { 300, 0, 0 });
    place(map, goblin, start + rune::Vector { 0, 0, rune::actor::CollisionHeight(goblin) - rune::actor::CollisionHeight(player) });
    rune::Vector location = rune::actor::Location(goblin);
    rune::pawn::Destination(goblin) = location + rune::Vector { -300, 0, 0 };
    REQUIRE(rune::pawn::bIsPlayer(goblin) == 0);
    auto adjust = [&]() {
        rune::vm::Call call(goblin, rune::Name("PickWallAdjust"));
        REQUIRE(call.valid());
        call.run();
        return boolResult(call);
    };
    CHECK(adjust());
    float radius = rune::actor::CollisionRadius(goblin);
    CHECK(rune::pawn::Destination(goblin).x == Catch::Approx(location.x).margin(1e-3));
    CHECK(rune::pawn::Destination(goblin).y == Catch::Approx(location.y + (radius + 16.0f)).margin(1e-3));
    CHECK(rune::actor::Physics(goblin) == rune::PHYS_Walking);
    rune::actor::Physics(goblin) = rune::PHYS_Falling;
    rune::pawn::Destination(goblin) = location + rune::Vector { -300, 0, 0 };
    CHECK_FALSE(adjust());
    CHECK(rune::pawn::Destination(goblin).x == Catch::Approx(location.x - 300.0f).margin(1e-3));
}

TEST_CASE("FindBestInventoryPath leads a pawn off the network to the nearest reachable node first", "[data]")
{
    DwarfMap map;
    rune::UObject* player = map.player();
    rune::pawn::MoveTarget(player) = nullptr;
    rune::Vector start = rune::actor::Location(player);
    REQUIRE(map.world->fastLineCheck(start + rune::Vector { -400, 0, 0 }, start));
    place(map, player, start + rune::Vector { -250, 0, 0 });
    rune::vm::Call call(player, rune::Name("FindBestInventoryPath"));
    REQUIRE(call.valid());
    call.f(0.0f).b(false).run();
    rune::UObject* path = objectResult(call);
    REQUIRE(path != nullptr);
    CHECK(path->name == rune::Name("PathNode27"));
}

TEST_CASE("walkReachable steps 16 units in the editor and so gives up after 101 steps", "[data]")
{
    DwarfMap map;
    rune::UObject* player = map.player();
    REQUIRE(rune::pawn::bCanJump(player) != 0);
    rune::Vector start = rune::actor::Location(player);
    rune::Vector destination = start + rune::Vector { -1700, 0, 0 };
    REQUIRE(rune::pawnWalkReachable(player, destination, 15.0f, 0, nullptr) == 1);
    CHECK(rune::actor::Location(player) == start);
    rune::objects::setEditorMode(true);
    int inEditor = rune::pawnWalkReachable(player, destination, 15.0f, 0, nullptr);
    int nearInEditor = rune::pawnWalkReachable(player, start + rune::Vector { -1500, 0, 0 }, 15.0f, 0, nullptr);
    rune::objects::setEditorMode(false);
    CHECK(inEditor == 0);
    CHECK(nearInEditor == 1);
}

TEST_CASE("the editor lifts the reachability distance limits and the pain zone check for non-pawn goals", "[data]")
{
    DwarfMap map;
    rune::UObject* player = map.player();
    rune::pawn::MoveTarget(player) = nullptr;
    rune::Vector start = rune::actor::Location(player);
    REQUIRE(map.world->fastLineCheck(start + rune::Vector { -1500, 0, 0 }, start));
    rune::UClass* bucketClass = rune::objects::findClass("RuneI.Bucket");
    rune::objects::flushLoads();
    REQUIRE(bucketClass != nullptr);
    rune::UObject* farBucket = map.world->spawn(bucketClass, nullptr, rune::Name(), start + rune::Vector { -850, 0, 0 }, {}, nullptr, true);
    rune::UObject* nearBucket = map.world->spawn(bucketClass, nullptr, rune::Name(), start + rune::Vector { -300, 60, 0 }, {}, nullptr, true);
    REQUIRE(farBucket != nullptr);
    REQUIRE(nearBucket != nullptr);
    rune::UObject* painZone = nullptr;
    for (rune::UObject* a : map.world->actors) {
        if (a && !a->destroyed && a->isA(rune::engineClasses().zoneInfo) && a != rune::zoneOf(nearBucket)) {
            painZone = a;
        }
    }
    REQUIRE(painZone != nullptr);
    rune::zoneinfo::bPainZone(painZone) = 1;
    rune::zoneinfo::DamageType(painZone) = rune::Name("fire");
    REQUIRE(rune::pawn::ReducedDamageType(player) != rune::Name("fire"));
    rune::region(nearBucket).zone = painZone;
    auto actorReachable = [&](rune::UObject* target) {
        rune::vm::Call call(player, rune::Name("actorReachable"));
        REQUIRE(call.valid());
        call.obj(target).run();
        return boolResult(call);
    };
    auto pointReachable = [&](const rune::Vector& point) {
        rune::vm::Call call(player, rune::Name("pointReachable"));
        REQUIRE(call.valid());
        call.v(point).run();
        return boolResult(call);
    };
    rune::Vector farPoint = start + rune::Vector { -1100, 0, 0 };
    bool farActorInGame = actorReachable(farBucket);
    bool painActorInGame = actorReachable(nearBucket);
    rune::actor::bBlockActors(farBucket) = 0;
    rune::actor::bBlockPlayers(farBucket) = 0;
    bool farPointInGame = pointReachable(farPoint);
    rune::objects::setEditorMode(true);
    bool farPointInEditor = pointReachable(farPoint);
    rune::actor::bBlockActors(farBucket) = 1;
    rune::actor::bBlockPlayers(farBucket) = 1;
    bool farActorInEditor = actorReachable(farBucket);
    bool painActorInEditor = actorReachable(nearBucket);
    rune::objects::setEditorMode(false);
    CHECK_FALSE(farActorInGame);
    CHECK_FALSE(painActorInGame);
    CHECK_FALSE(farPointInGame);
    CHECK(farActorInEditor);
    CHECK(painActorInEditor);
    CHECK(farPointInEditor);
}

TEST_CASE("StrafeTo and StrafeFacing run non players at eight tenths of MaxDesiredSpeed", "[data]")
{
    DwarfMap map;
    rune::UObject* goblin = walker(map);
    rune::UObject* player = map.player();
    rune::pawn::MaxDesiredSpeed(goblin) = 0.5f;
    rune::pawn::bIsPlayer(goblin) = 0;
    rune::Vector start = rune::actor::Location(goblin);
    {
        rune::vm::Call call(goblin, rune::Name("StrafeTo"));
        REQUIRE(call.valid());
        call.v(start + rune::Vector { 200, 0, 0 }).v(start + rune::Vector { 0, 200, 0 }).run();
    }
    CHECK(rune::pawn::DesiredSpeed(goblin) == 0.4f);
    rune::pawn::DesiredSpeed(goblin) = 0;
    {
        rune::vm::Call call(goblin, rune::Name("StrafeFacing"));
        REQUIRE(call.valid());
        call.v(start + rune::Vector { 200, 0, 0 }).obj(player).run();
    }
    CHECK(rune::pawn::DesiredSpeed(goblin) == 0.4f);
    rune::pawn::bIsPlayer(goblin) = 1;
    {
        rune::vm::Call call(goblin, rune::Name("StrafeTo"));
        REQUIRE(call.valid());
        call.v(start + rune::Vector { 200, 0, 0 }).v(start + rune::Vector { 0, 200, 0 }).run();
    }
    CHECK(rune::pawn::DesiredSpeed(goblin) == 0.5f);
}

TEST_CASE("MoveToward leads a moving target by eight tenths of its velocity when bLeadEnemy is set", "[data]")
{
    DwarfMap map;
    rune::UObject* goblin = walker(map);
    rune::UObject* player = map.player();
    rune::actor::Location(player) = rune::actor::Location(goblin) + rune::Vector { 400, 0, 0 };
    rune::actor::Velocity(player) = { 0, 250, 0 };
    rune::pawn::bLeadEnemy(goblin) = 1;
    {
        rune::vm::Call call(goblin, rune::Name("MoveToward"));
        REQUIRE(call.valid());
        call.obj(player).f(1.0f).run();
    }
    rune::latent::poll(goblin, 0.01f);
    rune::Vector expected = rune::actor::Location(player) + rune::Vector { 0, 200, 0 };
    CHECK(rune::pawn::Destination(goblin).x == expected.x);
    CHECK(rune::pawn::Destination(goblin).y == expected.y);
    CHECK(rune::pawn::Focus(goblin).y == expected.y);
    rune::pawn::bLeadEnemy(goblin) = 0;
    rune::latent::poll(goblin, 0.01f);
    CHECK(rune::pawn::Destination(goblin).y == rune::actor::Location(player).y);
}

TEST_CASE("turning keeps a flyer that cannot strafe accelerating along its current facing", "[data]")
{
    DwarfMap map;
    rune::UObject* goblin = walker(map);
    rune::actor::Physics(goblin) = rune::PHYS_Flying;
    rune::pawn::bCanStrafe(goblin) = 0;
    rune::pawn::AccelRate(goblin) = 500.0f;
    rune::actor::Rotation(goblin) = { 0, 16384, 0 };
    rune::actor::Acceleration(goblin) = {};
    {
        rune::vm::Call call(goblin, rune::Name("TurnTo"));
        REQUIRE(call.valid());
        call.v(rune::actor::Location(goblin) + rune::Vector { 100, 0, 0 }).run();
    }
    CHECK(rune::actor::Acceleration(goblin).x == Catch::Approx(0.0).margin(0.05));
    CHECK(rune::actor::Acceleration(goblin).y == Catch::Approx(500.0).margin(0.05));
    rune::actor::Rotation(goblin) = { 0, 32768, 0 };
    rune::latent::poll(goblin, 0.0f);
    CHECK(rune::actor::Acceleration(goblin).x == Catch::Approx(-500.0).margin(0.05));
    rune::pawn::bCanStrafe(goblin) = 1;
    rune::actor::Acceleration(goblin) = {};
    rune::latent::poll(goblin, 0.0f);
    CHECK(rune::actor::Acceleration(goblin).x == 0.0f);
}

TEST_CASE("actorReachable answers from the path network only for pawns up to 70 units wide", "[data]")
{
    DwarfMap map;
    rune::UObject* goblin = walker(map);
    rune::actor::CollisionRadius(goblin) = 20.0f;
    std::vector<rune::UObject*> nodes;
    for (rune::UObject* a : map.world->actors) {
        if (a && !a->destroyed && a->isA(rune::Name("PathNode"))) {
            nodes.push_back(a);
        }
    }
    auto reachable = [&](rune::UObject* target) {
        rune::vm::Call call(goblin, rune::Name("actorReachable"));
        REQUIRE(call.valid());
        call.obj(target).run();
        return boolResult(call);
    };
    int differing = 0;
    for (rune::UObject* from : nodes) {
        for (rune::UObject* to : nodes) {
            if (from == to || (rune::actor::Location(from) - rune::actor::Location(to)).size() > 400.0f) {
                continue;
            }
            place(map, goblin, rune::actor::Location(from));
            rune::actor::CollisionRadius(goblin) = 70.0f;
            bool network = reachable(to);
            rune::actor::CollisionRadius(goblin) = 70.5f;
            bool direct = reachable(to);
            rune::actor::CollisionRadius(goblin) = 20.0f;
            if (network != direct) {
                differing++;
            }
        }
    }
    CHECK(differing == 45);
}

TEST_CASE("walkReachable allows at least 50 units of height difference", "[data]")
{
    DwarfMap map;
    rune::UObject* player = map.player();
    rune::Vector start = rune::actor::Location(player);
    float height = rune::actor::CollisionHeight(player);
    REQUIRE(height < 45.0f);
    rune::Vector raised = start + rune::Vector { -100, 0, 45 };
    REQUIRE(map.world->fastLineCheck(raised, start));
    rune::vm::Call call(player, rune::Name("pointReachable"));
    REQUIRE(call.valid());
    call.v(raised).run();
    CHECK(boolResult(call));
}

TEST_CASE("Reachable falls back to a jump from the current velocity when walking fails", "[data]")
{
    DwarfMap map;
    rune::UObject* player = map.player();
    rune::Vector start = rune::actor::Location(player);
    rune::Vector target = start + rune::Vector { -100, 0, 0 };
    rune::pawn::bCanJump(player) = 0;
    place(map, player, start + rune::Vector { 0, 0, 100 });
    rune::vm::Call call(player, rune::Name("pointReachable"));
    REQUIRE(call.valid());
    call.v(target).run();
    CHECK(boolResult(call));
    CHECK(rune::actor::Location(player).z == start.z + 100.0f);
}

TEST_CASE("walkReachable jumps up obstacles lower than 48 units", "[data]")
{
    DwarfMap map;
    rune::UObject* player = map.player();
    rune::Vector start = rune::actor::Location(player);
    REQUIRE(rune::pawn::MaxStepHeight(player) < 40.0f);
    rune::pawn::bCanJump(player) = 1;
    rune::UClass* cls = rune::objects::findClass("Engine.Trigger");
    rune::objects::flushLoads();
    REQUIRE(cls != nullptr);
    rune::Vector floor = start - rune::Vector { 0, 0, rune::actor::CollisionHeight(player) };
    rune::Vector boxAt = floor + rune::Vector { -120, 0, 20 };
    rune::UObject* box = map.world->spawn(cls, nullptr, rune::Name(), boxAt, {}, nullptr, true);
    REQUIRE(box != nullptr);
    rune::actor::CollisionRadius(box) = 40.0f;
    rune::actor::CollisionHeight(box) = 20.0f;
    rune::actor::bCollideActors(box) = 1;
    rune::actor::bBlockActors(box) = 1;
    rune::actor::bBlockPlayers(box) = 1;
    place(map, box, boxAt);
    rune::Vector target = start + rune::Vector { -260, 0, 0 };
    REQUIRE(map.world->fastLineCheck(target, start));
    rune::vm::Call call(player, rune::Name("pointReachable"));
    REQUIRE(call.valid());
    call.v(target).run();
    CHECK(boolResult(call));
}
