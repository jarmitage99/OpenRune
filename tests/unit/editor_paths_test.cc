#include "core/objects.h"
#include "engine/actor.h"
#include "engine/csg.h"
#include "engine/path_builder.h"
#include "engine/props_engine.h"
#include "engine/resources.h"
#include "engine/world.h"

#include <catch_amalgamated.hpp>

#include <vector>

namespace {
    template <typename T>
    T* make(const char* className, rune::UObject* outer)
    {
        rune::UObject* object = rune::objects::newObject(rune::csg::nativeClass(className), outer, rune::Name(), 0);
        object->loaded = true;
        return rune::cast<T>(object);
    }

    rune::UObject* place(rune::ULevel* level, const char* className, const rune::Vector& location)
    {
        rune::UObject* actor = rune::objects::newObject(rune::objects::findClass(className), level->outermost(), rune::Name(), 0);
        actor->loaded = true;
        rune::actor::Location(actor) = location;
        rune::actor::Level(actor) = level->actors[0];
        rune::actor::XLevel(actor) = level;
        level->actors.push_back(actor);
        return actor;
    }

    rune::ULevel* makeRoom(const char* packageName)
    {
        rune::engine::initialize(RUNE_GAME_DATA);
        rune::UObject* package = rune::objects::packageObject(rune::Name(packageName));
        rune::ULevel* level = make<rune::ULevel>("Level", package);
        level->model = make<rune::UModel>("Model", package);
        rune::UObject* info = rune::objects::newObject(rune::objects::findClass("Engine.LevelInfo"), package, rune::Name(), 0);
        info->loaded = true;
        rune::UObject* builder = make<rune::UObject>("Brush", package);
        rune::UModel* builderModel = make<rune::UModel>("Model", package);
        builderModel->polys = make<rune::UPolys>("Polys", package);
        rune::actor::Brush(builder) = builderModel;
        level->actors = { info, builder };
        rune::actor::Level(info) = info;
        rune::csg::buildCube(*builderModel->polys, 2048.0f, 1024.0f, 512.0f, nullptr);
        rune::csg::addBrush(level, builder, rune::csg::CsgSubtract);
        rune::csg::rebuild(level);
        return level;
    }

    int pathCount(rune::UObject* node)
    {
        int count = 0;
        while (count < 16 && rune::navigationpoint::Paths(node, count) != -1) {
            count++;
        }
        return count;
    }
}

TEST_CASE("define paths links nearby path nodes with reach specs and prunes the long way round", "[data]")
{
    rune::ULevel* level = makeRoom("PathsDefineTest");
    rune::UObject* a = place(level, "Engine.PathNode", { -300.0f, 0.0f, -241.0f });
    rune::UObject* b = place(level, "Engine.PathNode", { 0.0f, 0.0f, -241.0f });
    rune::UObject* c = place(level, "Engine.PathNode", { 300.0f, 0.0f, -241.0f });
    rune::UObject* far = place(level, "Engine.PathNode", { 900.0f, 450.0f, -241.0f });
    rune::paths::undefinePaths(level);
    rune::paths::definePaths(level);
    CHECK(level->reachSpecs.size() == 8);
    REQUIRE(rune::levelinfo::NavigationPointList(level->actors[0]) == far);
    CHECK(rune::navigationpoint::nextNavigationPoint(far) == c);
    REQUIRE(pathCount(a) == 1);
    const rune::ReachSpec& ab = level->reachSpecs[static_cast<size_t>(rune::navigationpoint::Paths(a, 0))];
    CHECK(ab.start == a);
    CHECK(ab.end == b);
    CHECK(ab.distance == 300);
    CHECK(ab.reachFlags == 1);
    CHECK(ab.collisionRadius == 115);
    REQUIRE(rune::navigationpoint::PrunedPaths(a, 0) != -1);
    const rune::ReachSpec& ac = level->reachSpecs[static_cast<size_t>(rune::navigationpoint::PrunedPaths(a, 0))];
    CHECK(ac.end == c);
    CHECK(ac.pruned == 1);
    CHECK(pathCount(b) == 2);
    CHECK(pathCount(c) == 2);
    CHECK(pathCount(far) == 1);
    for (rune::UObject* actor : level->actors) {
        CHECK_FALSE(actor->isA(rune::Name("Scout")));
    }
    rune::paths::undefinePaths(level);
    CHECK(level->reachSpecs.empty());
    CHECK(rune::navigationpoint::Paths(b, 0) == -1);
    CHECK(rune::levelinfo::NavigationPointList(level->actors[0]) == nullptr);
}

namespace {
    rune::ULevel* makeLRoom(const char* packageName)
    {
        rune::ULevel* level = makeRoom(packageName);
        rune::UObject* builder = level->actors[1];
        rune::actor::Location(builder) = { 512.0f, 1024.0f, 0.0f };
        rune::csg::buildCube(*rune::cast<rune::UModel>(rune::actor::Brush(builder))->polys, 1024.0f, 1024.0f, 512.0f, nullptr);
        rune::csg::addBrush(level, builder, rune::csg::CsgSubtract);
        rune::csg::rebuild(level);
        return level;
    }

    std::vector<rune::UObject*> actorsOf(rune::ULevel* level, const char* className)
    {
        std::vector<rune::UObject*> result;
        for (rune::UObject* actor : level->actors) {
            if (!actor->destroyed && actor->isA(rune::Name(className))) {
                result.push_back(actor);
            }
        }
        return result;
    }
}

TEST_CASE("build paths places no path node in a convex room", "[data]")
{
    rune::ULevel* level = makeRoom("PathsBuildConvexTest");
    place(level, "Engine.Inventory", { 400.0f, 100.0f, -200.0f });
    CHECK(rune::paths::buildPaths(level, 1) == 0);
    CHECK(actorsOf(level, "PathNode").empty());
    CHECK(actorsOf(level, "Scout").empty());
}

TEST_CASE("build paths places one path node at the inside corner of an L shaped room", "[data]")
{
    rune::ULevel* level = makeLRoom("PathsBuildCornerTest");
    place(level, "Engine.Inventory", { -600.0f, 0.0f, -200.0f });
    CHECK(rune::paths::buildPaths(level, 1) == 1);
    std::vector<rune::UObject*> nodes = actorsOf(level, "PathNode");
    REQUIRE(nodes.size() == 1);
    rune::Vector location = rune::actor::Location(nodes[0]);
    CHECK(location.x >= 115.0f);
    CHECK(location.x <= 131.0f);
    CHECK(location.y >= 381.0f);
    CHECK(location.y <= 397.0f);
    CHECK(location.z > -256.0f);
    CHECK(location.z < -192.0f);
    CHECK(rune::navigationpoint::Paths(nodes[0], 0) == -1);
    CHECK(rune::navigationpoint::upstreamPaths(nodes[0], 15) == -1);
    CHECK(actorsOf(level, "Scout").empty());
}

TEST_CASE("build paths merges the inside corner into an existing path node", "[data]")
{
    rune::ULevel* level = makeLRoom("PathsBuildMergeTest");
    place(level, "Engine.Inventory", { -600.0f, 0.0f, -200.0f });
    REQUIRE(rune::paths::buildPaths(level, 1) == 1);
    CHECK(rune::paths::removePaths(level) == 1);
    rune::UObject* existing = place(level, "Engine.PathNode", { 160.0f, 360.0f, -245.0f });
    CHECK(rune::paths::buildPaths(level, 1) == 0);
    std::vector<rune::UObject*> nodes = actorsOf(level, "PathNode");
    REQUIRE(nodes.size() == 1);
    CHECK(nodes[0] == existing);
    CHECK(rune::actor::Location(existing).x == 160.0f);
    CHECK(rune::actor::Location(existing).y == 360.0f);
}
