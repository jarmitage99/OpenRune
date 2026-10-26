#include "core/config.h"
#include "core/objects.h"
#include "core/vm.h"
#include "engine/actor.h"
#include "engine/anim.h"
#include "engine/net.h"
#include "engine/props_engine.h"
#include "engine/world.h"

#include <catch_amalgamated.hpp>

#include <algorithm>
#include <chrono>
#include <memory>
#include <optional>
#include <string>

namespace {
    struct PhysicsMap {
        std::unique_ptr<rune::World> world;
        std::string name;

        explicit PhysicsMap(const char* map)
            : name(map)
        {
            rune::engine::initialize(RUNE_GAME_DATA);
            rune::objects::unloadPackage(rune::Name(map));
            world = std::make_unique<rune::World>();
            REQUIRE(world->loadMap(map));
        }

        ~PhysicsMap()
        {
            world.reset();
            rune::objects::unloadPackage(rune::Name(name));
        }

        rune::UObject* actor(const char* actorName)
        {
            for (rune::UObject* a : world->actors) {
                if (a && !a->destroyed && a->name == rune::Name(actorName)) {
                    return a;
                }
            }
            return nullptr;
        }

        rune::UObject* standingGoblin()
        {
            rune::UObject* goblin = actor("GoblinWarrior0");
            REQUIRE(goblin != nullptr);
            rune::vm::gotoState(goblin, rune::Name());
            rune::actor::Physics(goblin) = rune::PHYS_Walking;
            rune::actor::Acceleration(goblin) = {};
            rune::actor::Velocity(goblin) = {};
            for (int i = 0; i < 10; i++) {
                rune::performPhysics(goblin, 0.05f);
            }
            REQUIRE(rune::actor::Physics(goblin) == rune::PHYS_Walking);
            return goblin;
        }
    };
}

TEST_CASE("walking brakes in 0.03 second steps and averages the braked velocity", "[data]")
{
    PhysicsMap map("DwarfMap2");
    rune::UObject* goblin = map.standingGoblin();
    rune::UObject* zone = rune::zoneOf(goblin);
    REQUIRE(zone != nullptr);
    rune::zoneinfo::ZoneGroundFriction(zone) = 8.0f;
    rune::pawn::GroundSpeed(goblin) = 1000.0f;
    rune::pawn::DesiredSpeed(goblin) = 1.0f;
    rune::Vector start = rune::actor::Location(goblin);
    rune::actor::Velocity(goblin) = { 300, 0, 0 };
    rune::performPhysics(goblin, 0.1f);
    CHECK(rune::actor::Location(goblin).x - start.x == Catch::Approx(8.73340416).epsilon(1e-4));
    CHECK(rune::actor::Velocity(goblin).x == Catch::Approx(87.3340416).epsilon(1e-4));
    rune::actor::Velocity(goblin) = { 12, 0, 0 };
    rune::performPhysics(goblin, 0.01f);
    CHECK(rune::actor::Velocity(goblin).x == Catch::Approx(10.08).margin(0.01));
    rune::actor::Velocity(goblin) = { 11, 0, 0 };
    rune::performPhysics(goblin, 0.01f);
    CHECK(rune::actor::Velocity(goblin).x == 0.0f);
}

namespace {
    rune::UObject* walkingPlayer(PhysicsMap& map)
    {
        for (int i = 0; i < 5; i++) {
            map.world->tick(0.05f);
        }
        rune::UObject* player = map.world->player();
        REQUIRE(player != nullptr);
        rune::actor::Physics(player) = rune::PHYS_Walking;
        rune::actor::Acceleration(player) = {};
        rune::actor::Velocity(player) = {};
        for (int i = 0; i < 10; i++) {
            rune::performPhysics(player, 0.05f);
        }
        REQUIRE(rune::actor::Physics(player) == rune::PHYS_Walking);
        rune::pawn::bIsWalking(player) = 0;
        return player;
    }
}

TEST_CASE("elliptical speed scaling slows strafing to three quarters and backing up to half the ground speed", "[data]")
{
    PhysicsMap map("DwarfMap2");
    rune::UObject* player = walkingPlayer(map);
    rune::pawn::GroundSpeed(player) = 400.0f;
    rune::pawn::SpeedScale(player) = 1;
    rune::actor::Rotation(player) = { 0, 0, 0 };
    rune::pawn::ViewRotation(player) = { 0, 0, 0 };
    rune::actor::Velocity(player) = { 0, 400, 0 };
    rune::actor::Acceleration(player) = { 0, 100000, 0 };
    rune::Vector start = rune::actor::Location(player);
    rune::performPhysics(player, 0.02f);
    CHECK(rune::actor::Location(player).y - start.y == Catch::Approx(6.0).margin(0.01));
    rune::actor::Velocity(player) = { -400, 0, 0 };
    rune::actor::Acceleration(player) = { -100000, 0, 0 };
    start = rune::actor::Location(player);
    rune::performPhysics(player, 0.02f);
    CHECK(rune::actor::Location(player).x - start.x == Catch::Approx(-4.0).margin(0.01));
    rune::pawn::SpeedScale(player) = 0;
    rune::actor::Velocity(player) = { 0, 400, 0 };
    rune::actor::Acceleration(player) = { 0, 100000, 0 };
    start = rune::actor::Location(player);
    rune::performPhysics(player, 0.02f);
    CHECK(rune::actor::Location(player).y - start.y == Catch::Approx(8.0).margin(0.01));
}

TEST_CASE("a current pushes a walking player by 25 times its velocity per second of the frame", "[data]")
{
    PhysicsMap map("DwarfMap2");
    rune::UObject* player = walkingPlayer(map);
    rune::UObject* zone = rune::zoneOf(player);
    REQUIRE(zone != nullptr);
    rune::zoneinfo::ZoneVelocity(zone) = { 10, 0, 0 };
    rune::Vector start = rune::actor::Location(player);
    rune::performPhysics(player, 0.02f);
    CHECK(rune::actor::Location(player).x - start.x == Catch::Approx(0.1).margin(0.002));
}

TEST_CASE("a walking pawn outside the world gets FellOutOfWorld", "[data]")
{
    PhysicsMap map("RagnarVillage2");
    rune::UObject* player = walkingPlayer(map);
    REQUIRE(rune::pawn::Health(player) > 0);
    rune::region(player).zoneNumber = 0;
    rune::performPhysics(player, 0.05f);
    CHECK(rune::pawn::Health(player) == -1);
}

TEST_CASE("walking records the floor normal and eases a step up through PrePivot", "[data]")
{
    PhysicsMap map("DwarfMap2");
    rune::UObject* player = walkingPlayer(map);
    rune::pawn::Floor(player) = {};
    rune::actor::Velocity(player) = { 100, 0, 0 };
    rune::performPhysics(player, 0.02f);
    CHECK(rune::pawn::Floor(player).z >= 0.7f);
    rune::Vector location = rune::actor::Location(player);
    rune::actor::Location(player) = location + rune::Vector { 0, 0, 1.5f };
    map.world->collisionHash.update(player);
    rune::actor::PrePivot(player) = {};
    rune::actor::Velocity(player) = {};
    float before = rune::actor::Location(player).z;
    rune::performPhysics(player, 0.002f);
    float dropped = before - rune::actor::Location(player).z;
    REQUIRE(dropped > 0.5f);
    CHECK(rune::actor::PrePivot(player).z == Catch::Approx(dropped - 0.4f).margin(1e-3));
    rune::actor::PrePivot(player).z = 30.0f;
    rune::performPhysics(player, 0.01f);
    CHECK(rune::actor::PrePivot(player).z == Catch::Approx(27.0f).margin(1e-3));
    rune::actor::PrePivot(player).z = -1.0f;
    rune::performPhysics(player, 0.01f);
    CHECK(rune::actor::PrePivot(player).z == 0.0f);
}

TEST_CASE("bAlignToFloor tilts a pawn to the floor normal", "[data]")
{
    PhysicsMap map("DwarfMap2");
    rune::UObject* goblin = map.standingGoblin();
    rune::actor::Physics(goblin) = rune::PHYS_None;
    rune::actor::Rotation(goblin) = { 0, 0, 0 };
    rune::actor::DesiredRotation(goblin) = { 0, 0, 0 };
    rune::actor::RotationRate(goblin) = { 0, 0, 0 };
    rune::pawn::Floor(goblin) = { 0.6f, 0, 0.8f };
    rune::pawn::bAlignToFloor(goblin) = 1;
    rune::performPhysics(goblin, 0.05f);
    CHECK((rune::actor::Rotation(goblin).pitch & 65535) == 58825);
    CHECK(rune::actor::Rotation(goblin).yaw == 0);
    CHECK(rune::actor::Rotation(goblin).roll == 0);
}

TEST_CASE("pawns turn pitch toward DesiredRotation at RotationRate and settle their roll", "[data]")
{
    PhysicsMap map("DwarfMap2");
    rune::UObject* goblin = map.standingGoblin();
    rune::actor::Physics(goblin) = rune::PHYS_None;
    rune::actor::Rotation(goblin) = { 0, 0, 1000 };
    rune::actor::DesiredRotation(goblin) = { 4000, 0, 0 };
    rune::actor::RotationRate(goblin) = { 20000, 0, 4000 };
    rune::actor::Velocity(goblin) = {};
    rune::performPhysics(goblin, 0.05f);
    CHECK(rune::actor::Rotation(goblin).pitch == 1000);
    CHECK(rune::actor::Rotation(goblin).roll == 600);
}

TEST_CASE("PHYS_Sliding damps the acceleration by a tenth and walks", "[data]")
{
    PhysicsMap map("DwarfMap2");
    rune::UObject* goblin = map.standingGoblin();
    rune::actor::Physics(goblin) = rune::PHYS_Sliding;
    rune::pawn::AccelRate(goblin) = 1000.0f;
    rune::actor::Acceleration(goblin) = { 100, 0, 0 };
    rune::performPhysics(goblin, 0.05f);
    CHECK(rune::actor::Acceleration(goblin).x == Catch::Approx(90.0f));
}

namespace {
    bool findWater(PhysicsMap& map, rune::Vector& below, float& surface)
    {
        for (rune::UObject* a : map.world->actors) {
            if (!a || a->destroyed || !a->data || !a->isA(rune::Name("ZoneInfo")) || !rune::zoneinfo::bWaterZone(a)) {
                continue;
            }
            rune::Vector point = rune::actor::Location(a);
            rune::UObject* zone = map.world->zoneAt(point, nullptr, nullptr);
            if (zone != a) {
                continue;
            }
            float top = point.z;
            while (map.world->zoneAt({ point.x, point.y, top + 1 }, nullptr, nullptr) == a && top < point.z + 4000) {
                top += 1;
            }
            if (top >= point.z + 4000 || map.world->zoneAt({ point.x, point.y, top + 1 }, nullptr, nullptr) == nullptr) {
                continue;
            }
            rune::CheckResult hit;
            if (map.world->fastLineCheck(point, { point.x, point.y, top + 200 }) == false) {
                continue;
            }
            below = { point.x, point.y, top - 130 };
            surface = top;
            return true;
        }
        return false;
    }
}

TEST_CASE("swimming up out of the water stops at the water line", "[data]")
{
    for (const char* name : { "RagnarVillage2", "DwarfMap2", "Hel2", "Thor1" }) {
        PhysicsMap map(name);
        rune::Vector below;
        float surface = 0;
        if (!findWater(map, below, surface)) {
            continue;
        }
        for (int i = 0; i < 5; i++) {
            map.world->tick(0.05f);
        }
        rune::UObject* player = map.world->player();
        REQUIRE(player != nullptr);
        map.world->farMoveActor(player, below, false, true);
        rune::actor::Physics(player) = rune::PHYS_Swimming;
        rune::pawn::WaterSpeed(player) = 5000.0f;
        rune::actor::Velocity(player) = { 0, 0, 3000 };
        rune::actor::Acceleration(player) = {};
        float start = rune::actor::Location(player).z;
        rune::performPhysics(player, 0.05f);
        REQUIRE(start < surface - 100.0f);
        CHECK(rune::actor::Location(player).z <= surface + 1.5f);
        CHECK(rune::actor::Location(player).z >= surface - 1.5f);
        CHECK(rune::actor::Physics(player) == rune::PHYS_Falling);
        return;
    }
    FAIL("no open water found");
}

TEST_CASE("bNoSurfaceBob stops a swimmer with its head out of the water from rising", "[data]")
{
    for (const char* name : { "RagnarVillage2", "DwarfMap2", "Hel2", "Thor1" }) {
        PhysicsMap map(name);
        rune::Vector below;
        float surface = 0;
        if (!findWater(map, below, surface)) {
            continue;
        }
        for (int i = 0; i < 5; i++) {
            map.world->tick(0.05f);
        }
        rune::UObject* player = map.world->player();
        REQUIRE(player != nullptr);
        map.world->farMoveActor(player, { below.x, below.y, surface - rune::actor::CollisionHeight(player) * 0.5f }, false, true);
        REQUIRE(rune::zoneinfo::bWaterZone(rune::zoneOf(player)));
        REQUIRE_FALSE(rune::zoneinfo::bWaterZone(reinterpret_cast<rune::PointRegion*>(rune::pawn::HeadRegion(player))->zone));
        rune::actor::Physics(player) = rune::PHYS_Swimming;
        rune::actor::bNoSurfaceBob(player) = 1;
        rune::actor::Velocity(player) = { 0, 0, 200 };
        rune::actor::Acceleration(player) = {};
        float start = rune::actor::Location(player).z;
        rune::performPhysics(player, 0.02f);
        CHECK(rune::actor::Location(player).z <= start + 0.01f);
        return;
    }
    FAIL("no open water found");
}

namespace {
    rune::UObject* bucketAbove(PhysicsMap& map, rune::UObject* goblin, float height)
    {
        rune::UClass* cls = rune::objects::findClass("RuneI.Bucket");
        rune::objects::flushLoads();
        REQUIRE(cls != nullptr);
        rune::UObject* bucket = map.world->spawn(cls, nullptr, rune::Name(), rune::actor::Location(goblin) + rune::Vector { 0, 0, height }, {}, nullptr, true);
        REQUIRE(bucket != nullptr);
        return bucket;
    }
}

TEST_CASE("a non pawn landing in a bounce zone is carried by the zone velocity", "[data]")
{
    PhysicsMap map("DwarfMap2");
    rune::UObject* goblin = map.standingGoblin();
    rune::UObject* bucket = bucketAbove(map, goblin, 200);
    rune::UObject* zone = rune::zoneOf(bucket);
    REQUIRE(zone != nullptr);
    rune::zoneinfo::bBounceVelocity(zone) = 1;
    rune::zoneinfo::ZoneVelocity(zone) = { 100, 0, 0 };
    rune::actor::Physics(bucket) = rune::PHYS_Falling;
    rune::processLanded(bucket, { 0, 0, 1 }, map.world->levelInfo, 0, 0);
    CHECK(rune::actor::Velocity(bucket).x == 100.0f);
    CHECK(rune::actor::Velocity(bucket).z == 80.0f);
    CHECK(rune::actor::Physics(bucket) == rune::PHYS_Falling);
}

TEST_CASE("an unsupported decoration landing is pushed off instead of resting", "[data]")
{
    PhysicsMap map("RagnarVillage2");
    rune::UObject* goblin = walkingPlayer(map);
    rune::UObject* bucket = bucketAbove(map, goblin, 100);
    float height = rune::actor::CollisionHeight(bucket);
    bool open = false;
    for (int attempt = 0; attempt < 400 && !open; attempt++) {
        float lift = 200.0f + static_cast<float>(attempt % 20) * 10.0f;
        float side = static_cast<float>(attempt / 20) * 40.0f;
        rune::Vector spot = rune::actor::Location(goblin) + rune::Vector { side, side * 0.5f, lift };
        rune::CheckResult hit;
        open = !map.world->pointCheck(hit, spot, rune::cylinderExtent(bucket), bucket, true) && !map.world->lineCheck(hit, spot, spot - rune::Vector { 0, 0, 2 * height + 40 }, {}, bucket, rune::TRACE_World | rune::TRACE_Pawns | rune::TRACE_Others);
        if (open) {
            map.world->farMoveActor(bucket, spot, false, true);
        }
    }
    REQUIRE(open);
    rune::actor::Physics(bucket) = rune::PHYS_Falling;
    rune::actor::Velocity(bucket) = { 0, 0, -100 };
    rune::decoration::numLandings(bucket) = 0;
    REQUIRE(rune::actor::CollisionRadius(bucket) == 17.0f);
    rune::processLanded(bucket, { 0, 0, 1 }, map.world->levelInfo, 0, 0);
    CHECK(rune::decoration::numLandings(bucket) == 1);
    CHECK(rune::actor::Velocity(bucket).x == 0.0f);
    CHECK(rune::actor::Velocity(bucket).z == Catch::Approx(47.0f));
    CHECK(rune::actor::Physics(bucket) == rune::PHYS_Falling);
}

TEST_CASE("a flyer outside the world survives when it does not collide with the world", "[data]")
{
    PhysicsMap map("DwarfMap2");
    rune::UObject* goblin = map.standingGoblin();
    rune::actor::Physics(goblin) = rune::PHYS_Flying;
    rune::region(goblin).zoneNumber = 0;
    rune::actor::bCollideWorld(goblin) = 0;
    rune::performPhysics(goblin, 0.05f);
    CHECK_FALSE(goblin->destroyed);
    rune::actor::bCollideWorld(goblin) = 1;
    rune::region(goblin).zoneNumber = 0;
    rune::performPhysics(goblin, 0.05f);
    CHECK(goblin->destroyed);
}

TEST_CASE("a hit on a blocking joint reports the joint as a bit mask", "[data]")
{
    PhysicsMap map("RagnarVillage2");
    rune::UObject* player = walkingPlayer(map);
    rune::UObject* rack = map.actor("Skinrack0");
    REQUIRE(rack != nullptr);
    rune::CheckResult hit;
    rune::Vector extent { 18.0f, 18.0f, rune::actor::CollisionHeight(player) };
    REQUIRE(map.world->lineCheck(hit, { 2230, -43, -89 }, { 2260, -43, -89 }, extent, player, rune::TRACE_Blocking));
    REQUIRE(hit.actor == rack);
    uint32_t mask = static_cast<uint32_t>(hit.item) | static_cast<uint32_t>(hit.item2);
    CHECK(mask == 2);
}

TEST_CASE("trailers follow their owner by PrePivot or the owner rotation", "[data]")
{
    PhysicsMap map("DwarfMap2");
    rune::UObject* goblin = map.standingGoblin();
    rune::UObject* trailer = bucketAbove(map, goblin, 0);
    rune::actor::Owner(trailer) = goblin;
    rune::actor::Physics(trailer) = rune::PHYS_Trailer;
    rune::actor::bCollideWorld(trailer) = 0;
    rune::actor::DrawType(trailer) = rune::DT_Sprite;
    rune::actor::bTrailerPrePivot(trailer) = 1;
    rune::actor::PrePivot(trailer) = { 0, 0, 10 };
    rune::actor::Rotation(trailer) = { 0, 1234, 0 };
    rune::performPhysics(trailer, 0.05f);
    rune::Vector owner = rune::actor::Location(goblin);
    CHECK(rune::actor::Location(trailer).z == Catch::Approx(owner.z + 10.0f));
    CHECK(rune::actor::Rotation(trailer).yaw == 1234);
    rune::actor::DrawType(trailer) = 2;
    rune::actor::bTrailerSameRotation(trailer) = 1;
    rune::actor::Rotation(goblin) = { 0, 20000, 0 };
    rune::performPhysics(trailer, 0.05f);
    CHECK(rune::actor::Rotation(trailer).yaw == 20000);
}

TEST_CASE("movers interpolate toward key positions beyond the eighth", "[data]")
{
    PhysicsMap map("DwarfMap2");
    rune::UObject* mover = nullptr;
    for (rune::UObject* a : map.world->actors) {
        if (a && !a->destroyed && a->data && a->isA(rune::Name("Mover")) && !a->isA(rune::Name("Polyobj"))) {
            mover = a;
            break;
        }
    }
    REQUIRE(mover != nullptr);
    rune::Vector start = rune::actor::Location(mover);
    REQUIRE(start.z == 640.0f);
    rune::mover::BasePos(mover) = start;
    rune::mover::OldPos(mover) = start;
    rune::mover::OldRot(mover) = rune::actor::Rotation(mover);
    rune::mover::BaseRot(mover) = rune::actor::Rotation(mover);
    rune::mover::KeyRot(mover, 7) = {};
    rune::mover::KeyRot(mover, 10) = {};
    rune::mover::KeyPos(mover, 7) = { 0, 0, -100 };
    rune::mover::KeyPos(mover, 10) = { 0, 0, 100 };
    rune::mover::KeyNum(mover) = 10;
    rune::actor::Physics(mover) = rune::PHYS_MovingBrush;
    rune::actor::bInterpolating(mover) = 1;
    rune::actor::PhysAlpha(mover) = 0;
    rune::actor::PhysRate(mover) = 1;
    rune::performPhysics(mover, 0.25f);
    CHECK(rune::actor::Location(mover).z == Catch::Approx(655.625f));
}

TEST_CASE("actors based on a rotating mover swing around it and turn with it", "[data]")
{
    PhysicsMap map("DwarfMap2");
    rune::UObject* goblin = map.standingGoblin();
    rune::UObject* mover = nullptr;
    for (rune::UObject* a : map.world->actors) {
        if (a && !a->destroyed && a->data && a->isA(rune::Name("Mover")) && !a->isA(rune::Name("Polyobj"))) {
            mover = a;
            break;
        }
    }
    REQUIRE(mover != nullptr);
    rune::UObject* rider = bucketAbove(map, goblin, 0);
    rune::actor::bCollideWorld(rider) = 0;
    rune::actor::bCollideActors(rider) = 0;
    rune::actor::bBlockActors(rider) = 0;
    rune::actor::bBlockPlayers(rider) = 0;
    rune::actor::Rotation(mover) = { 0, 0, 0 };
    rune::Vector center = rune::actor::Location(mover);
    rune::actor::Location(rider) = center + rune::Vector { 100, 0, 0 };
    rune::actor::Rotation(rider) = { 0, 0, 0 };
    rune::setBase(rider, mover, false);
    REQUIRE(rune::actor::StandingCount(mover) > 0);
    rune::CheckResult hit;
    map.world->moveActor(mover, {}, { 0, 16384, 0 }, hit, false, false, false, true);
    CHECK(rune::actor::Location(rider).x - center.x == Catch::Approx(0.0).margin(0.1));
    CHECK(rune::actor::Location(rider).y - center.y == Catch::Approx(100.0).margin(0.1));
    CHECK(rune::actor::Rotation(rider).yaw == 16384);
    map.world->moveActor(mover, {}, { 4000, 16384, 0 }, hit, false, false, false, true);
    CHECK(rune::actor::Rotation(rider).pitch == 4000);
}

TEST_CASE("teleporting into a trigger touches it at once", "[data]")
{
    PhysicsMap map("RagnarVillage2");
    rune::UObject* player = walkingPlayer(map);
    rune::UClass* cls = rune::objects::findClass("Engine.Trigger");
    rune::objects::flushLoads();
    rune::Vector spot = rune::actor::Location(player) + rune::Vector { 0, 0, 0 };
    rune::actor::Location(player) = spot + rune::Vector { 0, 0, 2000 };
    map.world->collisionHash.update(player);
    rune::UObject* trigger = map.world->spawn(cls, nullptr, rune::Name(), spot, {}, nullptr, true);
    REQUIRE(trigger != nullptr);
    rune::actor::Location(player) = spot + rune::Vector { 300, 0, 0 };
    map.world->collisionHash.update(player);
    REQUIRE(map.world->farMoveActor(player, spot, false, false));
    bool touching = false;
    for (int i = 0; i < 4; i++) {
        touching = touching || rune::actor::Touching(player, i) == trigger;
    }
    CHECK(touching);
}

TEST_CASE("moving a player records its zone in its PlayerReplicationInfo", "[data]")
{
    PhysicsMap map("RagnarVillage2");
    rune::UObject* player = walkingPlayer(map);
    rune::UObject* replication = rune::pawn::PlayerReplicationInfo(player);
    REQUIRE(replication != nullptr);
    rune::playerreplicationinfo::PlayerZone(replication) = nullptr;
    REQUIRE(map.world->farMoveActor(player, rune::actor::Location(player), false, true));
    CHECK(rune::playerreplicationinfo::PlayerZone(replication) == rune::zoneOf(player));
    CHECK(rune::zoneOf(player) != nullptr);
}

TEST_CASE("a pawn that just reset its sight counter still sees the player", "[data]")
{
    PhysicsMap map("DwarfMap2");
    rune::UObject* goblin = map.standingGoblin();
    rune::UObject* player = map.world->player();
    REQUIRE(player != nullptr);
    rune::Vector forward = rune::rotationAxes(rune::actor::Rotation(goblin)).xAxis;
    rune::Vector spot = rune::actor::Location(goblin) + forward * 150.0f;
    REQUIRE(map.world->farMoveActor(player, spot, false, true));
    REQUIRE(rune::lineOfSightTo(goblin, player, true, false));
    auto seen = [&](float counter) {
        rune::vm::gotoState(goblin, rune::Name("Waiting"));
        rune::pawn::Enemy(goblin) = nullptr;
        rune::pawn::SightCounter(goblin) = counter;
        rune::Name before = rune::vm::stateName(goblin);
        map.world->tickPawnSenses(player, 0.001f);
        return rune::pawn::Enemy(goblin) == player || rune::vm::stateName(goblin) != before;
    };
    CHECK_FALSE(seen(0.2f));
    CHECK(seen(0.1f));
}

TEST_CASE("an actor in its last second of LifeSpan fades through AlphaScale", "[data]")
{
    PhysicsMap map("DwarfMap2");
    rune::UObject* goblin = map.standingGoblin();
    rune::actor::Physics(goblin) = rune::PHYS_None;
    rune::actor::Style(goblin) = 1;
    rune::actor::AlphaScale(goblin) = 1.0f;
    rune::actor::LifeSpan(goblin) = 3.0f;
    map.world->tickActor(goblin, 0.5f);
    CHECK(rune::actor::Style(goblin) == 1);
    CHECK(rune::actor::AlphaScale(goblin) == 1.0f);
    rune::actor::LifeSpan(goblin) = 0.75f;
    map.world->tickActor(goblin, 0.25f);
    CHECK(rune::actor::Style(goblin) == 5);
    CHECK(rune::actor::AlphaScale(goblin) == 0.5f);
    rune::actor::Style(goblin) = 4;
    rune::actor::AlphaScale(goblin) = 1.0f;
    map.world->tickActor(goblin, 0.25f);
    CHECK(rune::actor::Style(goblin) == 4);
    CHECK(rune::actor::AlphaScale(goblin) == 1.0f);
}

TEST_CASE("pawn timers only run on the authority", "[data]")
{
    PhysicsMap map("DwarfMap2");
    rune::UObject* goblin = map.standingGoblin();
    rune::actor::Physics(goblin) = rune::PHYS_None;
    rune::actor::Role(goblin) = rune::net::ROLE_SimulatedProxy;
    rune::pawn::PainTime(goblin) = 0.5f;
    rune::pawn::SightCounter(goblin) = 0.5f;
    map.world->tickActor(goblin, 0.25f);
    CHECK(rune::pawn::PainTime(goblin) == 0.5f);
    CHECK(rune::pawn::SightCounter(goblin) == 0.5f);
    rune::actor::Role(goblin) = rune::net::ROLE_Authority;
    map.world->tickActor(goblin, 0.25f);
    CHECK(rune::pawn::PainTime(goblin) == 0.25f);
    CHECK(rune::pawn::SightCounter(goblin) == 0.25f);
}

TEST_CASE("a player that is not a view target looks where its body faces", "[data]")
{
    PhysicsMap map("DwarfMap2");
    rune::UObject* player = walkingPlayer(map);
    rune::actor::Physics(player) = rune::PHYS_None;
    rune::actor::Rotation(player) = { 0, 16384, 0 };
    rune::pawn::ViewRotation(player) = { 3000, 1000, 0 };
    rune::pawn::bViewTarget(player) = 0;
    map.world->tickActor(player, 0.05f);
    CHECK(rune::pawn::ViewRotation(player).pitch == rune::actor::Rotation(player).pitch);
    CHECK(rune::pawn::ViewRotation(player).yaw == rune::actor::Rotation(player).yaw);
}

TEST_CASE("ColorAdjust and Fatness ease toward their desired values", "[data]")
{
    PhysicsMap map("DwarfMap2");
    rune::UObject* goblin = map.standingGoblin();
    rune::actor::Physics(goblin) = rune::PHYS_None;
    rune::actor::ColorAdjust(goblin) = { 100, 0, 50 };
    rune::actor::DesiredColorAdjust(goblin) = { 0, 0.5f, 49.5f };
    rune::actor::Fatness(goblin) = 200;
    rune::actor::DesiredFatness(goblin) = 100;
    map.world->tickActor(goblin, 0.25f);
    CHECK(rune::actor::ColorAdjust(goblin).x == 75.0f);
    CHECK(rune::actor::ColorAdjust(goblin).y == 0.5f);
    CHECK(rune::actor::ColorAdjust(goblin).z == 49.5f);
    CHECK(rune::actor::Fatness(goblin) == 125);
    rune::actor::ColorAdjust(goblin) = { 10, 0, 0 };
    rune::actor::DesiredColorAdjust(goblin) = { 0, 0, 0 };
    rune::actor::Fatness(goblin) = 101;
    map.world->tickActor(goblin, 0.05f);
    CHECK(rune::actor::ColorAdjust(goblin).x == 9.0f);
    CHECK(rune::actor::Fatness(goblin) == 100);
    rune::actor::Fatness(goblin) = 90;
    rune::actor::DesiredFatness(goblin) = 0;
    map.world->tickActor(goblin, 0.05f);
    CHECK(rune::actor::Fatness(goblin) == 90);
}

TEST_CASE("stasis is tested before the owner tick order and only in standalone games", "[data]")
{
    PhysicsMap map("DwarfMap2");
    rune::UObject* goblin = map.standingGoblin();
    rune::UObject* player = map.world->player();
    REQUIRE(player != nullptr);
    rune::actor::Physics(goblin) = rune::PHYS_None;
    rune::actor::bStasis(goblin) = 1;
    rune::actor::Owner(goblin) = player;
    rune::actor::bTicked(player) = map.world->tickParity ? 0 : 1;
    map.world->timeSeconds = 1000.0f;
    map.world->newlySpawned.clear();
    CHECK(map.world->tickActor(goblin, 0.05f));
    CHECK(map.world->newlySpawned.empty());
    rune::levelinfo::NetMode(map.world->levelInfo) = rune::net::NM_ListenServer;
    CHECK_FALSE(map.world->inStasis(goblin));
    rune::levelinfo::NetMode(map.world->levelInfo) = rune::net::NM_Standalone;
    CHECK(map.world->inStasis(goblin));
}

TEST_CASE("references to destroyed actors are cleared once 128 are pending", "[data]")
{
    PhysicsMap map("DwarfMap2");
    rune::UObject* goblin = map.standingGoblin();
    rune::UClass* cls = rune::objects::findClass("Engine.Trigger");
    rune::objects::flushLoads();
    REQUIRE(cls != nullptr);
    rune::Vector spot = rune::actor::Location(goblin);
    rune::UObject* first = map.world->spawn(cls, nullptr, rune::Name(), spot, {}, nullptr, true);
    REQUIRE(first != nullptr);
    map.world->cleanupDestroyed(true);
    rune::pawn::Enemy(goblin) = first;
    REQUIRE(map.world->destroy(first));
    map.world->cleanupDestroyed(false);
    CHECK(rune::pawn::Enemy(goblin) == first);
    for (int i = 0; i < 126; i++) {
        rune::UObject* other = map.world->spawn(cls, nullptr, rune::Name(), spot, {}, nullptr, true);
        REQUIRE(other != nullptr);
        REQUIRE(map.world->destroy(other));
    }
    map.world->cleanupDestroyed(false);
    CHECK(rune::pawn::Enemy(goblin) == first);
    rune::UObject* last = map.world->spawn(cls, nullptr, rune::Name(), spot, {}, nullptr, true);
    REQUIRE(last != nullptr);
    REQUIRE(map.world->destroy(last));
    map.world->cleanupDestroyed(false);
    CHECK(rune::pawn::Enemy(goblin) == nullptr);
    rune::UObject* forced = map.world->spawn(cls, nullptr, rune::Name(), spot, {}, nullptr, true);
    REQUIRE(forced != nullptr);
    rune::pawn::Enemy(goblin) = forced;
    REQUIRE(map.world->destroy(forced));
    map.world->cleanupDestroyed(true);
    CHECK(rune::pawn::Enemy(goblin) == nullptr);
}

TEST_CASE("the local player of a standalone game is an administrator with the play show flags", "[data]")
{
    PhysicsMap map("DwarfMap2");
    rune::UObject* player = map.world->player();
    REQUIRE(player != nullptr);
    CHECK(rune::playerpawn::bAdmin(player) == 1);
    CHECK(rune::playerpawn::ShowFlags(player) == 0x480c);
}

TEST_CASE("spawn returns the actor even when PostBeginPlay destroyed it", "[data]")
{
    PhysicsMap map("DwarfMap2");
    rune::UObject* goblin = map.standingGoblin();
    rune::UClass* cls = rune::objects::findClass("RuneI.BubbleSystemOneShot");
    rune::objects::flushLoads();
    REQUIRE(cls != nullptr);
    REQUIRE_FALSE(rune::zoneinfo::bWaterZone(rune::zoneOf(goblin)));
    rune::UObject* bubbles = map.world->spawn(cls, nullptr, rune::Name(), rune::actor::Location(goblin), {}, nullptr, true);
    REQUIRE(bubbles != nullptr);
    CHECK(bubbles->destroyed);
    CHECK(rune::actor::bDeleteMe(bubbles) == 1);
}

TEST_CASE("the level tick records the wall clock millisecond", "[data]")
{
    PhysicsMap map("DwarfMap2");
    auto milliseconds = [] {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    };
    rune::levelinfo::Millisecond(map.world->levelInfo) = -1;
    int64_t before = milliseconds();
    map.world->tick(0.05f);
    int64_t after = milliseconds();
    int64_t recorded = rune::levelinfo::Millisecond(map.world->levelInfo);
    REQUIRE(recorded >= 0);
    REQUIRE(recorded < 1000);
    int64_t offset = (recorded - before % 1000 + 1000) % 1000;
    CHECK(offset <= after - before);
}

TEST_CASE("slow frames set bDropDetail and bAggressiveLOD from MinDesiredFrameRate", "[data]")
{
    PhysicsMap map("DwarfMap2");
    rune::Ini& system = rune::config::system();
    std::string client = system.get("Engine.Engine", "ViewportManager").value_or("WinDrv.WindowsClient");
    std::optional<std::string> saved = system.get(client, "MinDesiredFrameRate");
    system.set(client, "MinDesiredFrameRate", "20");
    map.world->tick(0.04f);
    CHECK(rune::levelinfo::bDropDetail(map.world->levelInfo) == 0);
    CHECK(rune::levelinfo::bAggressiveLOD(map.world->levelInfo) == 0);
    map.world->tick(0.06f);
    CHECK(rune::levelinfo::bDropDetail(map.world->levelInfo) == 1);
    CHECK(rune::levelinfo::bAggressiveLOD(map.world->levelInfo) == 0);
    map.world->tick(0.07f);
    CHECK(rune::levelinfo::bDropDetail(map.world->levelInfo) == 1);
    CHECK(rune::levelinfo::bAggressiveLOD(map.world->levelInfo) == 1);
    system.set(client, "MinDesiredFrameRate", saved.value_or(""));
}

TEST_CASE("the game info runs its begin play events once, after InitGame", "[data]")
{
    PhysicsMap map("RagnarVillage2");
    rune::UClass* replicationClass = rune::objects::findClass("Engine.GameReplicationInfo");
    REQUIRE(replicationClass != nullptr);
    int count = 0;
    for (rune::UObject* a : map.world->actors) {
        if (a && !a->destroyed && a->isA(replicationClass)) {
            count++;
        }
    }
    CHECK(count == 1);
    rune::levelinfo::bBegunPlay(map.world->levelInfo) = 0;
    rune::UObject* goblin = nullptr;
    for (rune::UObject* a : map.world->actors) {
        if (a && !a->destroyed && rune::isPawn(a) && !rune::pawn::bIsPlayer(a)) {
            goblin = a;
            break;
        }
    }
    REQUIRE(goblin != nullptr);
    rune::vm::Call(goblin, rune::Name("Destroy")).run();
    CHECK_FALSE(goblin->destroyed);
    rune::levelinfo::bBegunPlay(map.world->levelInfo) = 1;
    rune::vm::Call(goblin, rune::Name("Destroy")).run();
    CHECK(goblin->destroyed);
}

TEST_CASE("a looping animation with a notify at frame zero advances by the elapsed time", "[data]")
{
    PhysicsMap map("DwarfMap2");
    rune::UObject* player = map.world->player();
    REQUIRE(player != nullptr);
    rune::vm::Call loop(player, rune::Name("LoopAnim"));
    REQUIRE(loop.valid());
    loop.n(rune::Name("MOV_ALL_run1_AN0N")).f(1.0f).f(0.0f).run();
    REQUIRE(rune::actor::AnimSequence(player) == rune::Name("MOV_ALL_run1_AN0N"));
    REQUIRE(rune::actor::bAnimNotify(player) == 1);
    REQUIRE(rune::actor::AnimRate(player) == 1.5f);
    rune::actor::AnimFrame(player) = 0;
    rune::updateAnimation(player, 0.03f);
    CHECK(rune::actor::AnimFrame(player) == Catch::Approx(0.045f));
    rune::actor::AnimFrame(player) = 0.99f;
    for (int i = 0; i < 5; i++) {
        rune::updateAnimation(player, 0.03f);
    }
    CHECK(rune::actor::AnimFrame(player) == Catch::Approx(0.215f));
}

TEST_CASE("a parent moves its joint children to its location until it is rendered and leaves their rotation alone", "[data]")
{
    PhysicsMap map("DwarfMap2");
    rune::UObject* goblin = map.standingGoblin();
    rune::UClass* cls = rune::objects::findClass("Engine.Trigger");
    rune::objects::flushLoads();
    rune::UObject* child = map.world->spawn(cls, nullptr, rune::Name(), rune::actor::Location(goblin), {}, nullptr, true);
    REQUIRE(child != nullptr);
    rune::actor::bCollideActors(child) = 0;
    rune::vm::Call attach(goblin, rune::Name("AttachActorToJoint"));
    REQUIRE(attach.valid());
    attach.obj(child).i(1).run();
    REQUIRE(rune::actor::JointChild(goblin, 1) == child);
    rune::actor::Rotation(child) = { 100, 200, 300 };
    rune::actor::Location(child) = rune::actor::Location(goblin) + rune::Vector { 500, 0, 0 };
    map.world->tickActor(goblin, 0.01f);
    CHECK(rune::actor::Location(child).x == rune::actor::Location(goblin).x);
    CHECK(rune::actor::Location(child).y == rune::actor::Location(goblin).y);
    CHECK(rune::actor::Rotation(child) == rune::Rotator { 100, 200, 300 });
    rune::Vector joint = rune::actor::Location(goblin) + rune::Vector { 1, 2, 3 };
    map.world->renderedJoints[goblin] = std::vector<rune::Vector>(2, joint);
    map.world->tickActor(goblin, 0.01f);
    CHECK(rune::actor::Location(child).x == joint.x);
    CHECK(rune::actor::Location(child).y == joint.y);
}

TEST_CASE("a pawn placed overlapping a mover is not encroached by it", "[data]")
{
    PhysicsMap map("DwarfMap2");
    rune::UObject* goblin = map.standingGoblin();
    rune::UObject* mover = nullptr;
    for (rune::UObject* a : map.world->actors) {
        if (a && !a->destroyed && rune::isMover(a) && rune::actor::bCollideActors(a)) {
            mover = a;
            break;
        }
    }
    REQUIRE(mover != nullptr);
    std::vector<rune::UObject*> hit = map.world->encroachedActors(goblin, rune::actor::Location(mover), rune::actor::Rotation(goblin));
    CHECK(std::find(hit.begin(), hit.end(), mover) == hit.end());
}
