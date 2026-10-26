#include "core/objects.h"
#include "engine/actor.h"
#include "engine/bsp.h"
#include "engine/csg.h"
#include "engine/props_engine.h"
#include "engine/resources.h"
#include "engine/world.h"

#include <catch_amalgamated.hpp>

#include <cmath>

namespace {
    struct TestLevel {
        rune::ULevel* level = nullptr;
        rune::UObject* builder = nullptr;
    };

    template <typename T>
    T* make(const char* className, rune::UObject* outer)
    {
        rune::UObject* object = rune::objects::newObject(rune::csg::nativeClass(className), outer, rune::Name(), 0);
        object->loaded = true;
        return rune::cast<T>(object);
    }

    TestLevel makeLevel(const char* packageName)
    {
        rune::engine::initialize(RUNE_GAME_DATA);
        rune::UObject* package = rune::objects::packageObject(rune::Name(packageName));
        TestLevel result;
        result.level = make<rune::ULevel>("Level", package);
        result.level->model = make<rune::UModel>("Model", package);
        result.builder = make<rune::UObject>("Brush", package);
        rune::UModel* builderModel = make<rune::UModel>("Model", package);
        builderModel->polys = make<rune::UPolys>("Polys", package);
        rune::actor::Brush(result.builder) = builderModel;
        rune::brush::CsgOper(result.builder) = rune::csg::CsgActive;
        result.level->actors.push_back(nullptr);
        result.level->actors.push_back(result.builder);
        return result;
    }

    rune::UPolys& builderPolys(TestLevel& test)
    {
        return *rune::cast<rune::UModel>(rune::actor::Brush(test.builder))->polys;
    }

    const rune::BspSurf* surfFacing(const rune::UModel& model, const rune::Vector& direction)
    {
        for (const rune::BspSurf& surf : model.surfs) {
            if (model.vectors[static_cast<size_t>(surf.vNormal)].dot(direction) > 0.99f) {
                return &surf;
            }
        }
        return nullptr;
    }

    bool nodeCovers(const rune::UModel& model, const rune::BspNode& node, float x, float y)
    {
        for (int32_t i = 0; i < node.numVertices; i++) {
            const rune::Vector& a = model.points[static_cast<size_t>(model.verts[static_cast<size_t>(node.vertPool + i)].vertex)];
            const rune::Vector& b = model.points[static_cast<size_t>(model.verts[static_cast<size_t>(node.vertPool + (i + 1) % node.numVertices)].vertex)];
            float cross = (b.x - a.x) * (y - a.y) - (b.y - a.y) * (x - a.x);
            if (cross < 0) {
                return false;
            }
        }
        return node.numVertices >= 3;
    }

    const rune::BspNode* floorNodeAt(const rune::UModel& model, float floorZ, float x, float y)
    {
        for (const rune::BspNode& node : model.nodes) {
            if (node.numVertices < 3 || node.plane.z < 0.99f || std::abs(node.plane.w - floorZ) > 0.5f) {
                continue;
            }
            if (nodeCovers(model, node, x, y)) {
                return &node;
            }
        }
        return nullptr;
    }
}

TEST_CASE("subtracting a cube from the solid world makes one empty room with six walls", "[data]")
{
    TestLevel test = makeLevel("CsgCubeTest");
    rune::csg::buildCube(builderPolys(test), 512.0f, 512.0f, 256.0f, nullptr);
    rune::UObject* room = rune::csg::addBrush(test.level, test.builder, rune::csg::CsgSubtract);
    REQUIRE(room != nullptr);
    rune::csg::Stats stats = rune::csg::rebuild(test.level);
    const rune::UModel& model = *test.level->model;
    CHECK(stats.brushes == 1);
    CHECK(stats.nodes == 6);
    CHECK(stats.surfs == 6);
    CHECK(stats.leaves == 1);
    CHECK(stats.zones == 2);
    CHECK(model.points.size() == 8);
    CHECK_FALSE(rune::csg::pointSolid(model, { 0, 0, 0 }));
    CHECK(rune::csg::pointZone(model, { 0, 0, 0 }) == 1);
    CHECK_FALSE(rune::csg::pointSolid(model, { 250, -250, 120 }));
    CHECK(rune::csg::pointSolid(model, { 262, 0, 0 }));
    CHECK(rune::csg::pointSolid(model, { 0, 0, 134 }));
    CHECK(rune::csg::pointSolid(model, { 0, -270, -140 }));
    const rune::BspSurf* floor = surfFacing(model, { 0, 0, 1 });
    REQUIRE(floor != nullptr);
    CHECK(floor->actor == room);
    int floors = 0;
    for (const rune::BspNode& node : model.nodes) {
        CHECK(node.numVertices == 4);
        CHECK(node.zone[1] == 1);
        CHECK(node.zone[0] == 0);
        CHECK(node.zoneMask == 2);
        if (node.surf == static_cast<int32_t>(floor - model.surfs.data())) {
            floors++;
            CHECK(model.points[static_cast<size_t>(model.verts[static_cast<size_t>(node.vertPool)].vertex)].z == -128.0f);
        }
    }
    CHECK(floors == 1);
    CHECK(model.leaves[0].zone == 1);
    CHECK(model.leafHulls.size() == 60);
}

TEST_CASE("traces stop at the walls of a rebuilt room", "[data]")
{
    TestLevel test = makeLevel("CsgTraceTest");
    rune::csg::buildCube(builderPolys(test), 512.0f, 512.0f, 256.0f, nullptr);
    rune::csg::addBrush(test.level, test.builder, rune::csg::CsgSubtract);
    rune::csg::rebuild(test.level);
    rune::World world;
    world.level = test.level;
    rune::CheckResult hit;
    REQUIRE(world.bspLineCheck(hit, { 0, 0, 0 }, { 400, 0, 0 }, { 0, 0, 0 }));
    CHECK(std::abs(hit.time - 0.64f) < 0.002f);
    CHECK(hit.normal.x < -0.99f);
    REQUIRE(world.bspLineCheck(hit, { 0, 0, 0 }, { 400, 0, 0 }, { 16, 16, 16 }));
    CHECK(std::abs(hit.time - 0.5f) < 0.002f);
    REQUIRE(world.bspLineCheck(hit, { 0, 0, 0 }, { 0, 0, -300 }, { 0, 0, 0 }));
    CHECK(std::abs(hit.location.z + 127.5f) < 0.01f);
    CHECK_FALSE(world.bspLineCheck(hit, { -100, -100, 0 }, { 100, 100, 50 }, { 16, 16, 16 }));
}

TEST_CASE("an added block inside a subtracted room is solid and has its own surfaces", "[data]")
{
    TestLevel test = makeLevel("CsgPillarTest");
    rune::csg::buildCube(builderPolys(test), 1024.0f, 1024.0f, 512.0f, nullptr);
    rune::csg::addBrush(test.level, test.builder, rune::csg::CsgSubtract);
    rune::csg::buildCube(builderPolys(test), 128.0f, 128.0f, 512.0f, nullptr);
    rune::UObject* pillar = rune::csg::addBrush(test.level, test.builder, rune::csg::CsgAdd);
    rune::csg::Stats stats = rune::csg::rebuild(test.level);
    const rune::UModel& model = *test.level->model;
    CHECK(stats.brushes == 2);
    CHECK(stats.surfs == 10);
    CHECK(rune::csg::pointSolid(model, { 0, 0, 0 }));
    CHECK(rune::csg::pointSolid(model, { 60, 60, 200 }));
    CHECK_FALSE(rune::csg::pointSolid(model, { 70, 0, 0 }));
    CHECK_FALSE(rune::csg::pointSolid(model, { 400, 400, 200 }));
    int pillarSurfs = 0;
    for (const rune::BspSurf& surf : model.surfs) {
        if (surf.actor == pillar) {
            pillarSurfs++;
        }
    }
    CHECK(pillarSurfs == 4);
    CHECK(floorNodeAt(model, -256.0f, 0.0f, 0.0f) == nullptr);
    CHECK(floorNodeAt(model, -256.0f, 200.0f, 0.0f) != nullptr);
    for (const rune::BspNode& node : model.nodes) {
        CHECK(node.numVertices >= 3);
    }
}

TEST_CASE("the later of two coplanar brush faces provides the surface texture", "[data]")
{
    TestLevel test = makeLevel("CsgCoplanarTest");
    rune::UPolys& polys = builderPolys(test);
    rune::UObject* first = rune::objects::loadObject("Brckston.BRCK6_c", nullptr);
    rune::UObject* second = rune::objects::loadObject("Brckston.Brck14_d", nullptr);
    rune::objects::flushLoads();
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    rune::csg::buildCube(polys, 1024.0f, 1024.0f, 512.0f, nullptr);
    rune::csg::addBrush(test.level, test.builder, rune::csg::CsgSubtract);
    rune::csg::buildCube(polys, 256.0f, 256.0f, 128.0f, first);
    rune::actor::Location(test.builder) = { 0.0f, 0.0f, -192.0f };
    rune::csg::addBrush(test.level, test.builder, rune::csg::CsgAdd);
    rune::csg::buildCube(polys, 128.0f, 128.0f, 128.0f, second);
    rune::actor::Location(test.builder) = { 64.0f, 0.0f, -192.0f };
    rune::csg::addBrush(test.level, test.builder, rune::csg::CsgAdd);
    rune::csg::rebuild(test.level);
    const rune::UModel& model = *test.level->model;
    const rune::BspNode* left = floorNodeAt(model, -128.0f, -100.0f, 0.0f);
    const rune::BspNode* right = floorNodeAt(model, -128.0f, 80.0f, 10.0f);
    REQUIRE(left != nullptr);
    REQUIRE(right != nullptr);
    CHECK(model.surfs[static_cast<size_t>(left->surf)].texture == first);
    CHECK(model.surfs[static_cast<size_t>(right->surf)].texture == second);
    int covering = 0;
    for (const rune::BspNode& node : model.nodes) {
        if (node.numVertices >= 3 && node.plane.z > 0.99f && std::abs(node.plane.w + 128.0f) < 0.5f && nodeCovers(model, node, 80.0f, 10.0f)) {
            covering++;
        }
    }
    CHECK(covering == 1);
}

TEST_CASE("a semisolid brush does not cut the floor it stands in but is solid for collision", "[data]")
{
    TestLevel test = makeLevel("CsgSemisolidTest");
    rune::csg::buildCube(builderPolys(test), 1024.0f, 1024.0f, 512.0f, nullptr);
    rune::csg::addBrush(test.level, test.builder, rune::csg::CsgSubtract);
    rune::csg::buildCube(builderPolys(test), 128.0f, 128.0f, 128.0f, nullptr);
    rune::actor::Location(test.builder) = { 200.0f, 0.0f, -256.0f };
    rune::csg::addBrush(test.level, test.builder, rune::csg::CsgAdd, rune::csg::PolySemisolid);
    rune::csg::rebuild(test.level);
    const rune::UModel& model = *test.level->model;
    CHECK(floorNodeAt(model, -256.0f, 200.0f, 0.0f) != nullptr);
    CHECK(rune::csg::pointSolid(model, { 200.0f, 0.0f, -230.0f }));
    CHECK_FALSE(rune::csg::pointSolid(model, { 0.0f, 0.0f, -230.0f }));

    TestLevel solid = makeLevel("CsgSolidBlockTest");
    rune::csg::buildCube(builderPolys(solid), 1024.0f, 1024.0f, 512.0f, nullptr);
    rune::csg::addBrush(solid.level, solid.builder, rune::csg::CsgSubtract);
    rune::csg::buildCube(builderPolys(solid), 128.0f, 128.0f, 128.0f, nullptr);
    rune::actor::Location(solid.builder) = { 200.0f, 0.0f, -256.0f };
    rune::csg::addBrush(solid.level, solid.builder, rune::csg::CsgAdd);
    rune::csg::rebuild(solid.level);
    CHECK(floorNodeAt(*solid.level->model, -256.0f, 200.0f, 0.0f) == nullptr);
}

TEST_CASE("a zone portal polygon joins the zones on its two sides", "[data]")
{
    TestLevel test = makeLevel("CsgPortalZoneTest");
    rune::UPolys& polys = builderPolys(test);
    rune::csg::buildCube(polys, 512.0f, 512.0f, 256.0f, nullptr);
    rune::csg::addBrush(test.level, test.builder, rune::csg::CsgSubtract);
    rune::actor::Location(test.builder) = { 768.0f, 0.0f, 0.0f };
    rune::csg::addBrush(test.level, test.builder, rune::csg::CsgSubtract);
    rune::csg::buildCube(polys, 256.0f, 128.0f, 128.0f, nullptr);
    rune::actor::Location(test.builder) = { 384.0f, 0.0f, 0.0f };
    rune::csg::addBrush(test.level, test.builder, rune::csg::CsgSubtract);
    rune::csg::buildSheet(polys, 192.0f, 192.0f, nullptr);
    rune::actor::Rotation(test.builder) = { 16384, 0, 0 };
    rune::csg::addBrush(test.level, test.builder, rune::csg::CsgAdd, rune::csg::PolyPortal | rune::csg::PolyInvisible);
    rune::csg::Stats stats = rune::csg::rebuild(test.level);
    const rune::UModel& model = *test.level->model;
    CHECK(stats.zones == 3);
    int32_t first = rune::csg::pointZone(model, { 0, 0, 0 });
    int32_t second = rune::csg::pointZone(model, { 768, 0, 0 });
    REQUIRE(first > 0);
    REQUIRE(second > 0);
    REQUIRE(first != second);
    uint64_t both = (1ull << first) | (1ull << second);
    int portals = 0;
    for (const rune::BspNode& node : model.nodes) {
        if (node.numVertices == 0 || !(model.surfs[static_cast<size_t>(node.surf)].polyFlags & rune::csg::PolyPortal)) {
            continue;
        }
        portals++;
        CHECK(std::min(node.zone[0], node.zone[1]) == std::min(first, second));
        CHECK(std::max(node.zone[0], node.zone[1]) == std::max(first, second));
        CHECK((node.zoneMask & both) == both);
    }
    CHECK(portals == 1);
    CHECK(model.zones[static_cast<size_t>(first)].connectivity == both);
    CHECK(model.zones[static_cast<size_t>(second)].connectivity == both);
}

TEST_CASE("brush from intersection keeps the part of the builder inside the solid world", "[data]")
{
    TestLevel test = makeLevel("CsgIntersectTest");
    rune::UPolys& polys = builderPolys(test);
    rune::csg::buildCube(polys, 512.0f, 512.0f, 256.0f, nullptr);
    rune::csg::addBrush(test.level, test.builder, rune::csg::CsgSubtract);
    rune::csg::rebuild(test.level);
    rune::csg::buildCube(polys, 256.0f, 256.0f, 256.0f, nullptr);
    rune::actor::Location(test.builder) = { 256.0f, 0.0f, 0.0f };
    REQUIRE(rune::csg::brushFromIntersection(test.level, test.builder, false));
    rune::UModel& brush = *rune::cast<rune::UModel>(rune::actor::Brush(test.builder));
    CHECK(brush.polys->polys.size() == 6);
    CHECK(brush.bounds.min.x == Catch::Approx(0.0f).margin(0.01));
    CHECK(brush.bounds.max.x == Catch::Approx(128.0f).margin(0.01));
    CHECK(brush.bounds.min.y == Catch::Approx(-128.0f).margin(0.01));
    CHECK(brush.bounds.max.z == Catch::Approx(128.0f).margin(0.01));

    rune::csg::buildCube(polys, 256.0f, 256.0f, 256.0f, nullptr);
    REQUIRE(rune::csg::brushFromIntersection(test.level, test.builder, true));
    CHECK(brush.polys->polys.size() == 6);
    CHECK(brush.bounds.min.x == Catch::Approx(-128.0f).margin(0.01));
    CHECK(brush.bounds.max.x == Catch::Approx(0.0f).margin(0.01));
}

TEST_CASE("the cube brush matches the original cube builder vertex order and texture axes", "[data]")
{
    TestLevel test = makeLevel("CsgCubeBuilderTest");
    rune::UPolys& polys = builderPolys(test);
    rune::csg::buildCube(polys, 256.0f, 128.0f, 64.0f, nullptr);
    REQUIRE(polys.polys.size() == 6);
    const rune::Poly& first = polys.polys[0];
    REQUIRE(first.vertices.size() == 4);
    CHECK(first.vertices[0].x == -128.0f);
    CHECK(first.vertices[0].y == -64.0f);
    CHECK(first.vertices[0].z == -32.0f);
    CHECK(first.vertices[1].z == 32.0f);
    CHECK(first.vertices[2].y == 64.0f);
    CHECK(first.normal.x == -1.0f);
    CHECK(first.base.x == -128.0f);
    CHECK(first.textureU.y == Catch::Approx(1.0f).margin(0.0001));
    CHECK(first.textureV.z == Catch::Approx(-1.0f).margin(0.0001));
}
