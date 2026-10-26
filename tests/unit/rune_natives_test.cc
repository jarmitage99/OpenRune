#include "core/objects.h"
#include "core/vm.h"
#include "engine/actor.h"
#include "engine/skeletal.h"
#include "engine/world.h"

#include <catch_amalgamated.hpp>

#include <cmath>
#include <memory>

namespace {
    std::unique_ptr<rune::World> loadedWorld()
    {
        rune::engine::initialize(RUNE_GAME_DATA);
        auto world = std::make_unique<rune::World>();
        REQUIRE(world->loadMap("DM-Hel"));
        return world;
    }

    template <typename T>
    T result(rune::vm::Call& call)
    {
        return *reinterpret_cast<T*>(call.returnValue());
    }

    float planarAngle(const rune::Vector& v)
    {
        return std::atan2(v.y, v.x) * 180.0f / rune::Pi;
    }
}

TEST_CASE("VSize2D ignores the vertical component", "[data]")
{
    std::unique_ptr<rune::World> loaded = loadedWorld();
    rune::World& world = *loaded;
    rune::vm::Call call(world.player(), rune::Name("VSize2D"));
    REQUIRE(call.valid());
    call.v({ 3.0f, 4.0f, 12.0f }).run();
    CHECK(result<float>(call) == 5.0f);
}

TEST_CASE("CalcArcVelocity aims along the trajectory pitch with the speed that lands on the target", "[data]")
{
    std::unique_ptr<rune::World> loaded = loadedWorld();
    rune::World& world = *loaded;
    rune::UObject* player = world.player();
    REQUIRE(player != nullptr);
    rune::UObject* zone = rune::zoneOf(player);
    REQUIRE(zone != nullptr);
    REQUIRE(rune::zoneinfo::ZoneGravity(zone).z == -1150.0f);
    rune::vm::Call call(player, rune::Name("CalcArcVelocity"));
    REQUIRE(call.valid());
    call.i(8192).v({ 0.0f, 0.0f, 0.0f }).v({ 1000.0f, 0.0f, 0.0f }).run();
    rune::Vector velocity = result<rune::Vector>(call);
    CHECK(std::abs(velocity.x - 758.3f) < 0.5f);
    CHECK(std::abs(velocity.y) < 0.01f);
    CHECK(std::abs(velocity.z - 758.3f) < 0.5f);
    rune::vm::Call above(player, rune::Name("CalcArcVelocity"));
    above.i(8192).v({ 0.0f, 0.0f, 0.0f }).v({ 1000.0f, 0.0f, 2000.0f }).run();
    CHECK(result<rune::Vector>(above).zero());
}

TEST_CASE("PathEndPoint reads the end of the reach spec through describeSpec", "[data]")
{
    std::unique_ptr<rune::World> loaded = loadedWorld();
    rune::World& world = *loaded;
    rune::UObject* node = nullptr;
    for (rune::UObject* a : world.actors) {
        if (a && a->isA(rune::Name("NavigationPoint")) && rune::navigationpoint::Paths(a, 0) >= 0) {
            node = a;
            break;
        }
    }
    REQUIRE(node != nullptr);
    int32_t spec = rune::navigationpoint::Paths(node, 0);
    REQUIRE(static_cast<size_t>(spec) < world.level->reachSpecs.size());
    rune::vm::Call call(node, rune::Name("PathEndPoint"));
    REQUIRE(call.valid());
    call.i(0).run();
    CHECK(result<rune::UObject*>(call) == world.level->reachSpecs[static_cast<size_t>(spec)].end);
}

TEST_CASE("TurnJointTo turns the joint and its children about the vertical axis", "[data]")
{
    std::unique_ptr<rune::World> loaded = loadedWorld();
    rune::World& world = *loaded;
    rune::UObject* player = world.player();
    REQUIRE(player != nullptr);
    rune::USkelModel* model = rune::skeletal::animationModel(player);
    REQUIRE(model != nullptr);
    int32_t torso = model->jointNamed(rune::Name("torso"));
    int32_t shoulder = model->jointNamed(rune::Name("lshoulda"));
    REQUIRE(torso > 0);
    REQUIRE(shoulder > torso);
    rune::SkelPose before;
    REQUIRE(rune::skeletal::framePose(player, rune::Name("baseframe"), 0, before));
    rune::Rotator turned = rune::actor::Rotation(player);
    turned.yaw += 8192;
    rune::vm::Call turn(player, rune::Name("TurnJointTo"));
    REQUIRE(turn.valid());
    turn.i(torso).r(turned).run();
    rune::SkelPose after;
    REQUIRE(rune::skeletal::framePose(player, rune::Name("baseframe"), 0, after));
    rune::Vector armBefore = before.joints[static_cast<size_t>(shoulder)].origin - before.joints[static_cast<size_t>(torso)].origin;
    rune::Vector armAfter = after.joints[static_cast<size_t>(shoulder)].origin - after.joints[static_cast<size_t>(torso)].origin;
    float turnedBy = planarAngle(armAfter) - planarAngle(armBefore);
    if (turnedBy < -180.0f) {
        turnedBy += 360.0f;
    }
    CHECK(std::abs(turnedBy - 45.0f) < 1.0f);
    rune::vm::Call reset(player, rune::Name("TurnJointTo"));
    reset.i(torso).r(rune::actor::Rotation(player)).run();
    rune::SkelPose restored;
    REQUIRE(rune::skeletal::framePose(player, rune::Name("baseframe"), 0, restored));
    CHECK((restored.joints[static_cast<size_t>(shoulder)].origin - before.joints[static_cast<size_t>(shoulder)].origin).size() < 0.01f);
}

namespace {
    rune::UObject* spawnPoint(rune::World& world, const char* className, const rune::Vector& location)
    {
        rune::UClass* cls = rune::objects::findClass(className);
        rune::objects::flushLoads();
        REQUIRE(cls != nullptr);
        rune::UObject* point = world.spawn(cls, nullptr, rune::Name(), location, {}, nullptr, true);
        REQUIRE(point != nullptr);
        rune::actor::bCollideWorld(point) = 0;
        rune::actor::Physics(point) = rune::PHYS_None;
        rune::actor::Velocity(point) = {};
        rune::actor::Location(point) = location;
        world.collisionHash.update(point);
        return point;
    }

    template <typename T>
    T& property(rune::UObject* object, const char* name, int32_t index = 0)
    {
        uint8_t* address = object->propertyAddress(rune::Name(name), index);
        REQUIRE(address != nullptr);
        return *reinterpret_cast<T*>(address);
    }

    void tick(rune::UObject* point, const char* native, float deltaSeconds)
    {
        rune::vm::Call call(point, rune::Name(native));
        REQUIRE(call.valid());
        call.f(deltaSeconds).run();
    }
}

TEST_CASE("SpringPointTick pulls toward the original position and damps by DampFactor per second", "[data]")
{
    std::unique_ptr<rune::World> loaded = loadedWorld();
    rune::World& world = *loaded;
    rune::Vector origin = rune::actor::Location(world.player()) + rune::Vector { 0, 0, 200 };
    rune::UObject* spring = spawnPoint(world, "RuneI.SpringPoint", origin);
    rune::actor::Mass(spring) = 100;
    property<float>(spring, "SpringConstant") = 50;
    property<float>(spring, "DampFactor") = 0.5f;
    property<rune::Vector>(spring, "OriginalPos") = origin + rune::Vector { 100, 0, 0 };
    tick(spring, "SpringPointTick", 0.1f);
    CHECK(rune::actor::Velocity(spring).x == Catch::Approx(4.75));
    CHECK(rune::actor::Velocity(spring).y == 0.0f);
    property<float>(spring, "DampFactor") = 10;
    tick(spring, "SpringPointTick", 0.1f);
    CHECK(rune::actor::Velocity(spring).zero());
}

TEST_CASE("MeshPointTick springs home and pushes a quarter of the impulse back into free neighbours", "[data]")
{
    std::unique_ptr<rune::World> loaded = loadedWorld();
    rune::World& world = *loaded;
    rune::Vector origin = rune::actor::Location(world.player()) + rune::Vector { 0, 0, 200 };
    rune::UObject* point = spawnPoint(world, "RuneI.MeshPoint", origin);
    rune::UObject* free = spawnPoint(world, "RuneI.MeshPoint", origin + rune::Vector { 60, 0, 0 });
    rune::UObject* anchored = spawnPoint(world, "RuneI.MeshPoint", origin + rune::Vector { -60, 0, 0 });
    rune::actor::Mass(point) = 100;
    property<float>(point, "SpringConstant") = 100;
    property<float>(point, "DampFactor") = 0.25f;
    property<rune::Vector>(point, "OriginalPos") = origin + rune::Vector { 0, 0, 40 };
    property<uint32_t>(point, "bAnchored") = 0;
    property<uint32_t>(free, "bAnchored") = 0;
    property<uint32_t>(anchored, "bAnchored") = 1;
    property<rune::UObject*>(point, "Adjacent", 0) = free;
    property<rune::UObject*>(point, "Adjacent", 1) = anchored;
    tick(point, "MeshPointTick", 0.5f);
    CHECK(rune::actor::Velocity(point).z == Catch::Approx(15.0));
    CHECK(rune::actor::Velocity(free).z == Catch::Approx(-5.0));
    CHECK(rune::actor::Velocity(anchored).zero());
    property<uint32_t>(point, "bAnchored") = 1;
    rune::actor::Velocity(point) = {};
    tick(point, "MeshPointTick", 0.5f);
    CHECK(rune::actor::Velocity(point).zero());
}

TEST_CASE("ConfinementTick moves to the average location of its confiners", "[data]")
{
    std::unique_ptr<rune::World> loaded = loadedWorld();
    rune::World& world = *loaded;
    rune::Vector origin = rune::actor::Location(world.player()) + rune::Vector { 0, 0, 200 };
    rune::UObject* confinement = spawnPoint(world, "RuneI.Confinement", origin);
    rune::actor::bCollideActors(confinement) = 0;
    rune::UObject* first = spawnPoint(world, "RuneI.SpringPoint", origin + rune::Vector { 40, 0, 0 });
    rune::UObject* second = spawnPoint(world, "RuneI.SpringPoint", origin + rune::Vector { 40, 60, 30 });
    property<rune::UObject*>(confinement, "Confiner", 0) = first;
    property<rune::UObject*>(confinement, "Confiner", 2) = second;
    tick(confinement, "ConfinementTick", 0.1f);
    rune::Vector moved = rune::actor::Location(confinement) - origin;
    CHECK(moved.x == Catch::Approx(40.0));
    CHECK(moved.y == Catch::Approx(30.0));
    CHECK(moved.z == Catch::Approx(15.0));
}

TEST_CASE("TreePointTick keeps the branch length to the parent and copies yaw into roll", "[data]")
{
    std::unique_ptr<rune::World> loaded = loadedWorld();
    rune::World& world = *loaded;
    rune::Vector origin = rune::actor::Location(world.player()) + rune::Vector { 0, 0, 200 };
    rune::UObject* parent = spawnPoint(world, "RuneI.TreePoint", origin + rune::Vector { 0, 300, 0 });
    rune::UObject* child = spawnPoint(world, "RuneI.TreePoint", origin);
    rune::actor::bCollideActors(child) = 0;
    property<rune::UObject*>(child, "Parent") = parent;
    property<rune::UObject*>(parent, "Parent") = nullptr;
    property<float>(child, "BranchLength") = 100;
    property<float>(child, "DampFactor") = 0;
    property<uint32_t>(child, "bApplyAcceleration") = 0;
    property<uint32_t>(child, "bApplySway") = 0;
    property<uint32_t>(child, "bSpring") = 0;
    property<uint32_t>(child, "bRestrictVelocity") = 0;
    property<uint32_t>(child, "bStiffRotation") = 0;
    property<uint32_t>(child, "bAffectsParent") = 1;
    rune::actor::Velocity(parent) = { 8, 0, 0 };
    tick(child, "TreePointTick", 0.5f);
    CHECK(rune::actor::Velocity(child).x == Catch::Approx(0.0).margin(1e-4));
    CHECK(rune::actor::Velocity(child).y == Catch::Approx(400.0));
    CHECK(rune::actor::Rotation(child).yaw == -16383);
    CHECK(rune::actor::Rotation(child).roll == -16383);
    CHECK(rune::actor::Rotation(child).pitch == 0);
    CHECK(rune::actor::Velocity(parent).x == Catch::Approx(4.0));
    CHECK(rune::actor::Velocity(parent).y == Catch::Approx(100.0));

    rune::actor::Velocity(child) = {};
    property<uint32_t>(child, "bSpring") = 1;
    property<uint32_t>(child, "bAffectsParent") = 0;
    property<float>(child, "SpringConstant") = 10;
    rune::actor::Mass(child) = 100;
    tick(child, "TreePointTick", 0.5f);
    CHECK(rune::actor::Velocity(child).y == Catch::Approx(10.0));

    rune::actor::Velocity(child) = {};
    property<uint32_t>(child, "bSpring") = 0;
    property<uint32_t>(child, "bRestrictVelocity") = 1;
    property<float>(child, "MaxVelocityAllowed") = 50;
    tick(child, "TreePointTick", 0.5f);
    CHECK(rune::actor::Velocity(child).y == Catch::Approx(100.0));
}
