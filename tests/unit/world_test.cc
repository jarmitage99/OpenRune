#include "core/objects.h"
#include "core/vm.h"
#include "engine/actor.h"
#include "engine/console.h"
#include "engine/props_engine.h"
#include "engine/resources.h"
#include "engine/world.h"

#include <catch_amalgamated.hpp>

#include <algorithm>
#include <unordered_map>
#include <vector>

namespace {
    const char* const Village = "RagnarVillage2";

    rune::UObject* findActor(rune::World& world, const char* name)
    {
        for (rune::UObject* a : world.actors) {
            if (a && !a->destroyed && a->name == rune::Name(name)) {
                return a;
            }
        }
        return nullptr;
    }

    void loadVillage(rune::World& world)
    {
        rune::engine::initialize(RUNE_GAME_DATA);
        rune::objects::unloadPackage(rune::Name(Village));
        REQUIRE(world.loadMap(Village));
    }

    void unloadVillage()
    {
        rune::objects::unloadPackage(rune::Name(Village));
    }

    rune::UObject* spawnKeg(rune::World& world, const rune::Vector& location)
    {
        rune::UClass* cls = rune::objects::findClass("RuneI.Keg");
        rune::objects::flushLoads();
        if (!cls) {
            return nullptr;
        }
        rune::UObject* keg = world.spawn(cls, nullptr, rune::Name(), location, {}, nullptr, true);
        if (keg) {
            rune::actor::bStasis(keg) = 0;
        }
        return keg;
    }

    rune::UObject* firstOfClass(rune::World& world, const char* className)
    {
        for (rune::UObject* a : world.actors) {
            if (a && !a->destroyed && a->isA(rune::Name(className))) {
                return a;
            }
        }
        return nullptr;
    }

    size_t indexOf(rune::World& world, rune::UObject* actor)
    {
        return static_cast<size_t>(std::find(world.actors.begin(), world.actors.end(), actor) - world.actors.begin());
    }
}

TEST_CASE("a hostile goblin acquires the player as its enemy and hurts him", "[data]")
{
    rune::World world;
    loadVillage(world);
    rune::UObject* player = world.player();
    REQUIRE(player != nullptr);
    rune::UClass* cls = rune::objects::findClass("RuneI.GoblinMohawk");
    rune::objects::flushLoads();
    REQUIRE(cls != nullptr);
    rune::UObject* goblin = world.spawn(cls, nullptr, rune::Name(), rune::actor::Location(player) + rune::Vector { 300, 0, 0 }, { 0, 32768, 0 }, nullptr, false);
    REQUIRE(goblin != nullptr);
    REQUIRE(rune::pawn::AttitudeToPlayer(goblin) == 1);
    CHECK(rune::pawn::Enemy(goblin) == nullptr);
    int32_t startHealth = rune::pawn::Health(player);
    REQUIRE(startHealth > 0);
    bool acquired = false;
    for (int i = 0; i < 450; i++) {
        world.tick(1.0f / 30.0f);
        if (rune::pawn::Enemy(goblin) == player) {
            acquired = true;
        }
    }
    CHECK(acquired);
    CHECK(rune::pawn::Health(player) < startHealth);
    unloadVillage();
}

TEST_CASE("actors in stasis stop ticking once their zone has gone unrendered for five seconds", "[data]")
{
    rune::World world;
    loadVillage(world);
    for (int i = 0; i < 25; i++) {
        world.tick(0.25f);
    }
    REQUIRE(world.timeSeconds > 6.0f);
    rune::UObject* keg = spawnKeg(world, rune::actor::Location(world.player()) + rune::Vector { 200, 0, 0 });
    REQUIRE(keg != nullptr);
    rune::actor::bStasis(keg) = 1;
    rune::actor::Physics(keg) = rune::PHYS_None;
    rune::actor::LifeSpan(keg) = 10.0f;
    float& zoneRenderTime = world.level->model->zones[rune::region(keg).zoneNumber].lastRenderTime;
    zoneRenderTime = 0.0f;
    world.tick(0.25f);
    CHECK(rune::actor::LifeSpan(keg) == 10.0f);
    zoneRenderTime = world.timeSeconds - 1.0f;
    world.tick(0.25f);
    CHECK(rune::actor::LifeSpan(keg) == Catch::Approx(9.75f));
    zoneRenderTime = 0.0f;
    rune::actor::Physics(keg) = rune::PHYS_Falling;
    world.tick(0.25f);
    CHECK(rune::actor::LifeSpan(keg) == Catch::Approx(9.5f));
    rune::actor::Physics(keg) = rune::PHYS_Falling;
    rune::actor::bForceStasis(keg) = 1;
    world.tick(0.25f);
    CHECK(rune::actor::LifeSpan(keg) == Catch::Approx(9.5f));
    rune::actor::bStasis(keg) = 0;
    world.tick(0.25f);
    CHECK(rune::actor::LifeSpan(keg) == Catch::Approx(9.25f));
    unloadVillage();
}

TEST_CASE("box traces against level geometry stop short by a tenth of their length", "[data]")
{
    rune::World world;
    loadVillage(world);
    rune::CheckResult hit;
    REQUIRE(world.bspLineCheck(hit, { -70.93f, -1760.78f, -273.25f }, { -70.93f, -1760.78f, -300.25f }, { 18, 18, 42 }));
    CHECK(hit.time == Catch::Approx(4.75f / 27.0f - 0.1f).margin(0.0005f));
    REQUIRE(world.bspLineCheck(hit, { -70.93f, -1760.78f, -273.25f }, { -70.93f, -1760.78f, -373.25f }, {}));
    CHECK(hit.location.z == Catch::Approx(-319.5f).margin(0.01f));
    unloadVillage();
}

TEST_CASE("a walking pawn settles inside the floor distance band and is based on the level", "[data]")
{
    rune::World world;
    loadVillage(world);
    world.tick(0.4f);
    for (int i = 0; i < 150; i++) {
        world.tick(1.0f / 30.0f);
    }
    rune::UObject* player = world.player();
    REQUIRE(player != nullptr);
    REQUIRE(rune::actor::Physics(player) == rune::PHYS_Walking);
    REQUIRE(rune::pawn::MaxStepHeight(player) == 25.0f);
    float gap = rune::actor::Location(player).z - rune::actor::CollisionHeight(player) - -320.0f;
    CHECK(gap >= 4.6f);
    CHECK(gap <= 5.1f);
    CHECK(rune::actor::Base(player) == world.levelInfo);
    unloadVillage();
}

TEST_CASE("an NPC sees a near pawn and loses it beyond 4000 units unless distance is ignored", "[data]")
{
    rune::World world;
    loadVillage(world);
    rune::UObject* player = world.player();
    rune::UObject* sigurd = findActor(world, "Sigurd0");
    REQUIRE(player != nullptr);
    REQUIRE(sigurd != nullptr);
    rune::pawn::Enemy(sigurd) = nullptr;
    rune::pawn::Visibility(player) = 128;
    rune::actor::Location(sigurd) = { -70.93f, -1760.78f, 324.6f };
    rune::actor::Location(player) = { -970.93f, -1760.78f, 324.6f };
    CHECK(rune::lineOfSightTo(sigurd, player, false, false));
    rune::actor::Location(player) = { -4370.93f, -1760.78f, 324.6f };
    CHECK_FALSE(rune::lineOfSightTo(sigurd, player, false, false));
    CHECK(rune::lineOfSightTo(sigurd, player, false, true));
    rune::actor::Location(player) = { -70.93f, -1760.78f, -624.6f };
    CHECK_FALSE(rune::lineOfSightTo(sigurd, player, false, true));
    unloadVillage();
}

TEST_CASE("landing on a pawn that allows standing bases the lander on it", "[data]")
{
    rune::World world;
    loadVillage(world);
    rune::UObject* player = world.player();
    rune::UObject* sigurd = findActor(world, "Sigurd0");
    REQUIRE(player != nullptr);
    REQUIRE(sigurd != nullptr);
    rune::pawn::bAllowStandOn(sigurd) = 1;
    rune::actor::Physics(player) = rune::PHYS_Falling;
    rune::actor::Velocity(player) = {};
    rune::setPhysics(player, rune::PHYS_Walking, sigurd);
    CHECK(rune::actor::Physics(player) == rune::PHYS_Walking);
    CHECK(rune::actor::Base(player) == sigurd);
    unloadVillage();
}

TEST_CASE("switching to rotating physics clears velocity and acceleration", "[data]")
{
    rune::World world;
    loadVillage(world);
    rune::UObject* keg = spawnKeg(world, rune::actor::Location(world.player()) + rune::Vector { 0, 0, 300 });
    REQUIRE(keg != nullptr);
    rune::actor::Physics(keg) = rune::PHYS_Falling;
    rune::actor::Velocity(keg) = { 10, 20, -30 };
    rune::actor::Acceleration(keg) = { 1, 2, 3 };
    rune::setPhysics(keg, rune::PHYS_Rotating, nullptr);
    CHECK(rune::actor::Velocity(keg).zero());
    CHECK(rune::actor::Acceleration(keg).zero());
    unloadVillage();
}

TEST_CASE("a full touch list drops a non-player pawn to make room for a player", "[data]")
{
    rune::World world;
    loadVillage(world);
    rune::UObject* player = world.player();
    rune::UObject* node = firstOfClass(world, "PathNode");
    REQUIRE(node != nullptr);
    std::vector<rune::UObject*> pawns;
    for (rune::UObject* a : world.actors) {
        if (a && !a->destroyed && a->isA(rune::Name("ScriptPawn")) && pawns.size() < 4) {
            pawns.push_back(a);
        }
    }
    REQUIRE(pawns.size() == 4);
    for (int i = 0; i < 4; i++) {
        rune::actor::Physics(pawns[i]) = rune::PHYS_Walking;
        rune::actor::Touching(node, i) = pawns[i];
    }
    rune::beginTouch(node, player);
    CHECK(rune::actor::Touching(node, 0) == player);
    CHECK(rune::actor::Touching(node, 1) == pawns[1]);
    CHECK(rune::actor::Touching(node, 2) == pawns[2]);
    CHECK(rune::actor::Touching(node, 3) == pawns[3]);
    unloadVillage();
}

namespace {
    rune::World* hookWorld = nullptr;
    rune::UObject* hookActor = nullptr;
    rune::NativeFunction hookOriginal = nullptr;
    int hookCalls = 0;
    bool hookSawActorInHash = false;

    void collisionHashProbe(rune::UObject* self, rune::Frame& stack, void* result)
    {
        if (self == hookActor) {
            hookCalls++;
            rune::Vector location = rune::actor::Location(hookActor);
            rune::Vector extent = rune::cylinderExtent(hookActor);
            const std::vector<rune::UObject*>& found = hookWorld->collisionHash.query(location - extent, location + extent);
            hookSawActorInHash = std::find(found.begin(), found.end(), hookActor) != found.end();
        }
        hookOriginal(self, stack, result);
    }
}

TEST_CASE("a destroyed actor leaves the collision hash before its Destroyed event runs", "[data]")
{
    rune::World world;
    loadVillage(world);
    world.tick(1.0f / 30.0f);
    rune::UObject* keg = spawnKeg(world, rune::actor::Location(world.player()) + rune::Vector { 200, 0, 0 });
    REQUIRE(keg != nullptr);
    REQUIRE(rune::actor::bCollideActors(keg));
    rune::UFunction* function = rune::vm::findFunction(keg, rune::Name("PlaySound"));
    REQUIRE(function != nullptr);
    REQUIRE(function->nativeIndex == 264);
    hookWorld = &world;
    hookActor = keg;
    hookOriginal = rune::vm::bindNative(function);
    REQUIRE(hookOriginal != nullptr);
    hookCalls = 0;
    hookSawActorInHash = true;
    rune::vm::registerNative(264, collisionHashProbe);
    world.destroy(keg);
    rune::vm::registerNative(264, hookOriginal);
    CHECK(hookCalls == 1);
    CHECK_FALSE(hookSawActorInHash);
    unloadVillage();
}

TEST_CASE("an actor whose owner ticks later waits until the owner has ticked", "[data]")
{
    rune::World world;
    loadVillage(world);
    rune::UObject* player = world.player();
    rune::UObject* sigurd = findActor(world, "Sigurd0");
    REQUIRE(sigurd != nullptr);
    world.tick(1.0f / 30.0f);
    rune::UObject* keg = spawnKeg(world, rune::actor::Location(player) + rune::Vector { 200, 0, 0 });
    REQUIRE(keg != nullptr);
    REQUIRE(indexOf(world, sigurd) < indexOf(world, player));
    REQUIRE(indexOf(world, player) < indexOf(world, keg));
    rune::actor::Owner(sigurd) = keg;
    bool sampled = false;
    bool ownerTickedBeforePlayer = true;
    bool childTickedBeforePlayer = true;
    world.inputHook = [&](rune::UObject*, float delta) {
        if (delta > 0 && !sampled) {
            sampled = true;
            ownerTickedBeforePlayer = rune::actor::bTicked(keg) == world.tickParity;
            childTickedBeforePlayer = rune::actor::bTicked(sigurd) == world.tickParity;
        }
    };
    world.tick(1.0f / 30.0f);
    world.inputHook = nullptr;
    REQUIRE(sampled);
    CHECK_FALSE(ownerTickedBeforePlayer);
    CHECK_FALSE(childTickedBeforePlayer);
    CHECK(rune::actor::bTicked(sigurd) == rune::actor::bTicked(player));
    CHECK(rune::actor::bTicked(keg) == rune::actor::bTicked(player));
    rune::actor::Owner(sigurd) = nullptr;
    unloadVillage();
}

TEST_CASE("actors spawned between frames are ticked on the next frame", "[data]")
{
    rune::World world;
    loadVillage(world);
    rune::Vector origin = rune::actor::Location(world.player());
    for (int frame = 0; frame < 2; frame++) {
        rune::UObject* keg = spawnKeg(world, origin + rune::Vector { 200.0f + 100.0f * static_cast<float>(frame), 0, 0 });
        REQUIRE(keg != nullptr);
        rune::actor::LifeSpan(keg) = 10.0f;
        world.tick(0.25f);
        INFO(frame);
        CHECK(rune::actor::LifeSpan(keg) == Catch::Approx(9.75f));
    }
    unloadVillage();
}

TEST_CASE("actors without physics do not rotate", "[data]")
{
    rune::World world;
    loadVillage(world);
    rune::UObject* keg = spawnKeg(world, rune::actor::Location(world.player()) + rune::Vector { 200, 0, 0 });
    REQUIRE(keg != nullptr);
    rune::actor::Physics(keg) = rune::PHYS_None;
    rune::actor::Rotation(keg) = {};
    rune::actor::bFixedRotationDir(keg) = 1;
    rune::actor::bRotateToDesired(keg) = 0;
    rune::actor::RotationRate(keg) = { 0, 6000, 0 };
    world.tick(0.1f);
    CHECK(rune::actor::Rotation(keg).yaw == 0);
    unloadVillage();
}

TEST_CASE("players-only mode stops non-player actors after their animation update", "[data]")
{
    rune::World world;
    loadVillage(world);
    rune::UObject* keg = spawnKeg(world, rune::actor::Location(world.player()) + rune::Vector { 200, 0, 0 });
    REQUIRE(keg != nullptr);
    rune::actor::LifeSpan(keg) = 10.0f;
    rune::levelinfo::bPlayersOnly(world.levelInfo) = 1;
    world.tick(0.25f);
    CHECK(rune::actor::LifeSpan(keg) == 10.0f);
    rune::levelinfo::bPlayersOnly(world.levelInfo) = 0;
    world.tick(0.25f);
    CHECK(rune::actor::LifeSpan(keg) == Catch::Approx(9.75f));
    unloadVillage();
}

TEST_CASE("level startup bases actors with an AttachTag on the actor carrying that tag", "[data]")
{
    rune::engine::initialize(RUNE_GAME_DATA);
    rune::objects::unloadPackage(rune::Name(Village));
    rune::Linker* linker = rune::objects::loadPackage(rune::Name(Village));
    REQUIRE(linker != nullptr);
    rune::ULevel* level = rune::cast<rune::ULevel>(linker->object(linker->findExport(rune::Name("MyLevel"), rune::Name("Level"), "")));
    REQUIRE(level != nullptr);
    rune::objects::ensureLoaded(level);
    rune::objects::flushLoads();
    rune::UObject* keg = nullptr;
    rune::UObject* mover = nullptr;
    for (rune::UObject* a : level->actors) {
        if (a && !keg && a->isA(rune::Name("Keg"))) {
            keg = a;
        }
        if (a && !mover && a->name == rune::Name("Mover6")) {
            mover = a;
        }
    }
    REQUIRE(keg != nullptr);
    REQUIRE(mover != nullptr);
    REQUIRE(rune::actor::Tag(mover) == rune::Name("doortoArenaMover"));
    REQUIRE(rune::actor::Base(keg) == nullptr);
    rune::actor::AttachTag(keg) = rune::Name("doortoArenaMover");
    rune::World world;
    REQUIRE(world.loadMap(Village));
    CHECK(rune::actor::Base(keg) == mover);
    unloadVillage();
}

TEST_CASE("level startup moves static actors ahead of dynamic ones and never ticks them", "[data]")
{
    rune::World world;
    loadVillage(world);
    REQUIRE(world.firstDynamicActor > 2);
    REQUIRE(world.firstDynamicActor < world.actors.size());
    for (size_t i = 2; i < world.actors.size(); i++) {
        INFO(i);
        if (!world.actors[i]) {
            continue;
        }
        CHECK((rune::actor::bStatic(world.actors[i]) != 0) == (i < world.firstDynamicActor));
    }
    rune::UObject* staticActor = world.actors[2];
    REQUIRE(rune::actor::bStatic(staticActor));
    rune::actor::LifeSpan(staticActor) = 10.0f;
    world.tick(0.25f);
    CHECK(rune::actor::LifeSpan(staticActor) == 10.0f);
    unloadVillage();
}

TEST_CASE("a rolling actor on flat ground loses speed to ground and fluid friction", "[data]")
{
    rune::World world;
    loadVillage(world);
    rune::UObject* keg = spawnKeg(world, rune::actor::Location(world.player()) + rune::Vector { 120, 0, 0 });
    REQUIRE(keg != nullptr);
    rune::actor::Physics(keg) = rune::PHYS_Falling;
    for (int i = 0; i < 60 && rune::actor::Physics(keg) != rune::PHYS_None; i++) {
        world.tick(1.0f / 30.0f);
    }
    REQUIRE(rune::actor::Physics(keg) == rune::PHYS_None);
    rune::UObject* zone = rune::region(keg).zone;
    REQUIRE(zone != nullptr);
    REQUIRE(rune::zoneinfo::ZoneGroundFriction(zone) == 8.0f);
    REQUIRE(rune::zoneinfo::ZoneFluidFriction(zone) == 1.2f);
    REQUIRE(rune::zoneinfo::ZoneVelocity(zone).zero());
    rune::setPhysics(keg, rune::PHYS_Rolling, nullptr);
    rune::actor::Velocity(keg) = { 100, 0, 0 };
    rune::actor::Acceleration(keg) = {};
    float startX = rune::actor::Location(keg).x;
    rune::performPhysics(keg, 0.05f);
    CHECK(rune::actor::Physics(keg) == rune::PHYS_Rolling);
    CHECK(rune::actor::Location(keg).x - startX == Catch::Approx(2.82f).margin(0.001f));
    CHECK(rune::actor::Velocity(keg).x == Catch::Approx(56.4f).margin(0.02f));
    unloadVillage();
}

TEST_CASE("script spawns give the new actor the spawner's instigator", "[data]")
{
    rune::World world;
    loadVillage(world);
    rune::UObject* sigurd = findActor(world, "Sigurd0");
    rune::UObject* jarl = findActor(world, "Jarl0");
    REQUIRE(sigurd != nullptr);
    REQUIRE(jarl != nullptr);
    rune::actor::Instigator(sigurd) = jarl;
    rune::UClass* cls = rune::objects::findClass("RuneI.SmallFire");
    rune::objects::flushLoads();
    REQUIRE(cls != nullptr);
    REQUIRE_FALSE(rune::actor::bCollideActors(cls->defaultObject));
    rune::vm::Call spawn(sigurd, rune::Name("Spawn"));
    spawn.obj(cls).obj(sigurd);
    spawn.run();
    rune::UObject* fire = *reinterpret_cast<rune::UObject**>(spawn.returnValue());
    REQUIRE(fire != nullptr);
    CHECK(rune::actor::Owner(fire) == sigurd);
    CHECK(rune::actor::Instigator(fire) == jarl);
    unloadVillage();
}

TEST_CASE("traces ignore every actor up the source's owner chain but not its children", "[data]")
{
    rune::World world;
    loadVillage(world);
    rune::Vector origin = rune::actor::Location(world.player()) + rune::Vector { 0, 0, 200 };
    rune::UObject* source = spawnKeg(world, origin);
    rune::UObject* middle = spawnKeg(world, origin + rune::Vector { 0, 300, 0 });
    rune::UObject* top = spawnKeg(world, origin + rune::Vector { 200, 0, 0 });
    REQUIRE(source != nullptr);
    REQUIRE(middle != nullptr);
    REQUIRE(top != nullptr);
    rune::actor::Owner(source) = middle;
    rune::actor::Owner(middle) = top;
    rune::CheckResult hit;
    world.lineCheck(hit, origin, origin + rune::Vector { 400, 0, 0 }, {}, source, rune::TRACE_Others);
    CHECK(hit.actor == nullptr);
    rune::Vector topLocation = rune::actor::Location(top);
    rune::Vector middleLocation = rune::actor::Location(middle);
    world.lineCheck(hit, topLocation, middleLocation, {}, top, rune::TRACE_Others);
    CHECK(hit.actor == middle);
    unloadVillage();
}

TEST_CASE("an actor's owner still blocks its moves", "[data]")
{
    rune::World world;
    loadVillage(world);
    rune::Vector origin = rune::actor::Location(world.player()) + rune::Vector { 0, 0, 200 };
    rune::UObject* mover = spawnKeg(world, origin);
    rune::UObject* owner = spawnKeg(world, origin + rune::Vector { 100, 0, 0 });
    REQUIRE(mover != nullptr);
    REQUIRE(owner != nullptr);
    REQUIRE(rune::isBlockedBy(mover, owner));
    rune::actor::Owner(mover) = owner;
    rune::CheckResult hit;
    world.moveActor(mover, { 100, 0, 0 }, rune::actor::Rotation(mover), hit, false, false, false, false);
    CHECK(hit.actor == owner);
    CHECK(rune::actor::Location(mover).x < origin.x + 100.0f - rune::actor::CollisionRadius(owner));
    unloadVillage();
}

TEST_CASE("test moves update the actor's zone without zone events", "[data]")
{
    rune::World world;
    loadVillage(world);
    rune::UObject* keg = spawnKeg(world, rune::actor::Location(world.player()) + rune::Vector { 0, 0, 200 });
    REQUIRE(keg != nullptr);
    rune::UObject* startZone = rune::region(keg).zone;
    rune::UObject* otherZone = nullptr;
    rune::Vector otherLocation;
    for (rune::UObject* a : world.actors) {
        if (a && a->isA(rune::engineClasses().zoneInfo) && a != startZone && a != world.levelInfo && world.zoneAt(rune::actor::Location(a), nullptr, nullptr) == a) {
            otherZone = a;
            otherLocation = rune::actor::Location(a);
            break;
        }
    }
    REQUIRE(otherZone != nullptr);
    REQUIRE(world.farMoveActor(keg, otherLocation, true, true));
    CHECK(rune::region(keg).zone == otherZone);
    rune::Vector back = rune::actor::Location(world.player()) + rune::Vector { 0, 0, 200 };
    REQUIRE(world.farMoveActor(keg, back, false, true));
    REQUIRE(rune::region(keg).zone == startZone);
    rune::CheckResult hit;
    world.moveActor(keg, otherLocation - back, rune::actor::Rotation(keg), hit, true, false, false, true);
    CHECK(rune::region(keg).zone == world.zoneAt(rune::actor::Location(keg), nullptr, nullptr));
    CHECK(rune::region(keg).zone != startZone);
    unloadVillage();
}

namespace {
    std::unordered_map<rune::UObject*, int> gotoStateCalls;
    rune::NativeFunction gotoStateOriginal = nullptr;

    void countGotoState(rune::UObject* self, rune::Frame& stack, void* result)
    {
        gotoStateCalls[self]++;
        gotoStateOriginal(self, stack, result);
    }
}

TEST_CASE("actors spawned during level startup receive the later startup events too", "[data]")
{
    rune::engine::initialize(RUNE_GAME_DATA);
    rune::objects::unloadPackage(rune::Name(Village));
    rune::UClass* actorClass = rune::objects::findClass("Engine.Actor");
    rune::objects::flushLoads();
    REQUIRE(actorClass != nullptr);
    rune::UFunction* gotoState = actorClass->findFunction(rune::Name("GotoState"));
    REQUIRE(gotoState != nullptr);
    REQUIRE(gotoState->nativeIndex == 113);
    gotoStateOriginal = rune::vm::bindNative(gotoState);
    REQUIRE(gotoStateOriginal != nullptr);
    gotoStateCalls.clear();
    rune::vm::registerNative(113, countGotoState);
    rune::World world;
    bool loaded = world.loadMap(Village);
    rune::vm::registerNative(113, gotoStateOriginal);
    REQUIRE(loaded);
    int fires = 0;
    for (rune::UObject* a : world.actors) {
        if (a && !a->destroyed && a->isA(rune::Name("SmallFire")) && rune::actor::Owner(a) && rune::actor::Owner(a)->isA(rune::Name("Tstadt_torch"))) {
            fires++;
            INFO(a->name.str());
            CHECK(gotoStateCalls[a] == 2);
        }
    }
    CHECK(fires == 9);
    unloadVillage();
}

TEST_CASE("paused frames zero the player's input axes and tick always-tick actors", "[data]")
{
    rune::World world;
    loadVillage(world);
    rune::UObject* player = world.player();
    rune::UObject* keg = spawnKeg(world, rune::actor::Location(player) + rune::Vector { 200, 0, 0 });
    REQUIRE(keg != nullptr);
    rune::actor::bAlwaysTick(keg) = 1;
    rune::actor::LifeSpan(keg) = 10.0f;
    rune::UObject* other = spawnKeg(world, rune::actor::Location(player) + rune::Vector { 300, 0, 0 });
    REQUIRE(other != nullptr);
    rune::actor::LifeSpan(other) = 10.0f;
    rune::levelinfo::Pauser(world.levelInfo) = "test";
    rune::playerpawn::aBaseY(player) = 5.0f;
    world.tick(0.25f);
    CHECK(world.paused);
    CHECK(rune::playerpawn::aBaseY(player) == 0.0f);
    CHECK(rune::actor::LifeSpan(keg) == Catch::Approx(9.75f));
    CHECK(rune::actor::LifeSpan(other) == 10.0f);
    unloadVillage();
}

TEST_CASE("a spider pawn with no surface around it falls", "[data]")
{
    rune::World world;
    loadVillage(world);
    rune::UObject* sigurd = findActor(world, "Sigurd0");
    REQUIRE(sigurd != nullptr);
    REQUIRE(world.farMoveActor(sigurd, rune::actor::Location(sigurd) + rune::Vector { 0, 0, 600 }, false, true));
    rune::actor::Physics(sigurd) = rune::PHYS_Spider;
    rune::pawn::Floor(sigurd) = {};
    rune::actor::Velocity(sigurd) = { 10, 0, 0 };
    rune::performPhysics(sigurd, 0.05f);
    CHECK(rune::actor::Physics(sigurd) == rune::PHYS_Falling);
    unloadVillage();
}

TEST_CASE("physics hands the pending touch actor PostTouch and pops it off the chain", "[data]")
{
    rune::World world;
    loadVillage(world);
    rune::Vector origin = rune::actor::Location(world.player()) + rune::Vector { 0, 0, 200 };
    rune::UObject* a = spawnKeg(world, origin);
    rune::UObject* b = spawnKeg(world, origin + rune::Vector { 100, 0, 0 });
    rune::UObject* c = spawnKeg(world, origin + rune::Vector { 200, 0, 0 });
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    REQUIRE(c != nullptr);
    rune::actor::Physics(a) = rune::PHYS_None;
    rune::actor::PendingTouch(a) = b;
    rune::actor::PendingTouch(b) = c;
    rune::performPhysics(a, 0.05f);
    CHECK(rune::actor::PendingTouch(a) == c);
    CHECK(rune::actor::PendingTouch(b) == nullptr);
    unloadVillage();
}

TEST_CASE("the player's input is read again with -1 after PlayerTick, which zeroes the axes", "[data]")
{
    rune::World world;
    loadVillage(world);
    rune::UObject* player = world.player();
    std::vector<float> deltas;
    world.inputHook = [&](rune::UObject*, float delta) {
        deltas.push_back(delta);
    };
    world.tick(0.25f);
    world.inputHook = nullptr;
    REQUIRE(deltas.size() == 2);
    CHECK(deltas[0] == 0.25f);
    CHECK(deltas[1] == -1.0f);
    rune::playerpawn::aBaseY(player) = 5.0f;
    rune::console::readInput(player, -1.0f);
    CHECK(rune::playerpawn::aBaseY(player) == 0.0f);
    unloadVillage();
}

TEST_CASE("a player pawn without a player object is blocked like any other actor", "[data]")
{
    rune::World world;
    loadVillage(world);
    rune::UObject* player = world.player();
    rune::UObject* keg = spawnKeg(world, rune::actor::Location(player) + rune::Vector { 0, 0, 300 });
    REQUIRE(keg != nullptr);
    rune::actor::bBlockActors(keg) = 1;
    rune::actor::bBlockPlayers(keg) = 0;
    rune::UObject* possessing = rune::playerpawn::Player(player);
    REQUIRE(possessing != nullptr);
    rune::playerpawn::Player(player) = nullptr;
    CHECK(rune::isBlockedBy(player, keg));
    rune::playerpawn::Player(player) = possessing;
    CHECK_FALSE(rune::isBlockedBy(player, keg));
    unloadVillage();
}
