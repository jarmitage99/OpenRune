#include "core/objects.h"
#include "core/vm.h"
#include "engine/actor.h"
#include "engine/bsp.h"
#include "engine/canvas_hooks.h"
#include "engine/props_engine.h"
#include "engine/skeletal.h"
#include "engine/world.h"
#include "render/actor_shadow.h"
#include "render/lighting.h"
#include "render/lightmap.h"
#include "render/procedural.h"
#include "render/scene_renderer.h"
#include "render/soft_device.h"

#include <catch_amalgamated.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {
    std::unique_ptr<rune::World> openMap(const std::string& map)
    {
        rune::engine::initialize(RUNE_GAME_DATA);
        rune::objects::unloadPackage(rune::Name(map));
        auto world = std::make_unique<rune::World>();
        REQUIRE(world->loadMap(map));
        return world;
    }

    void closeMap(std::unique_ptr<rune::World>& world, const std::string& map)
    {
        world.reset();
        rune::objects::unloadPackage(rune::Name(map));
    }

    rune::UObject* spawnPoint(rune::World& world, const char* className, const rune::Vector& location, int32_t yaw, const char* event)
    {
        rune::UClass* cls = rune::objects::findClass(className);
        rune::objects::flushLoads();
        rune::UObject* point = world.spawn(cls, nullptr, rune::Name("TestPath"), location, { 0, yaw, 0 }, nullptr, true);
        REQUIRE(point != nullptr);
        rune::actor::Event(point) = rune::Name(event);
        return point;
    }

    void link(rune::UObject* from, rune::UObject* to)
    {
        *reinterpret_cast<rune::UObject**>(from->propertyAddress(rune::Name("Next"))) = to;
        *reinterpret_cast<rune::UObject**>(to->propertyAddress(rune::Name("Prev"))) = from;
    }

    rune::UObject* spawnCounter(rune::World& world, const char* tag)
    {
        rune::UClass* cls = rune::objects::findClass("Engine.Counter");
        rune::objects::flushLoads();
        rune::UObject* counter = world.spawn(cls, nullptr, rune::Name(tag), {}, {}, nullptr, true);
        REQUIRE(counter != nullptr);
        *counter->propertyAddress(rune::Name("NumToCount")) = 10;
        return counter;
    }

    rune::UObject* startCamera(rune::World& world, rune::UObject* first)
    {
        rune::UClass* cls = rune::objects::findClass("RuneI.CineCamera");
        rune::objects::flushLoads();
        rune::UObject* camera = world.spawn(cls, nullptr, rune::Name(), rune::actor::Location(first), rune::actor::Rotation(first), nullptr, true);
        REQUIRE(camera != nullptr);
        rune::actor::Target(camera) = first;
        rune::actor::Physics(camera) = rune::PHYS_Interpolating;
        rune::actor::PhysRate(camera) = 1.0f;
        rune::actor::PhysAlpha(camera) = 0.0f;
        rune::actor::bInterpolating(camera) = 1;
        return camera;
    }
}

TEST_CASE("the intro map hides the world from its player while other maps render it", "[data]")
{
    auto intro = openMap("intro");
    REQUIRE(intro->player() != nullptr);
    CHECK(rune::playerpawn::RendMap(intro->player()) == 0);
    closeMap(intro, "intro");

    auto village = openMap("RagnarVillage2");
    REQUIRE(village->player() != nullptr);
    CHECK(rune::playerpawn::RendMap(village->player()) == 5);
    CHECK(rune::levelinfo::EngineVersion(village->levelInfo) == "111");
    CHECK(rune::levelinfo::GameVersion(village->levelInfo) == "111");
    closeMap(village, "RagnarVillage2");
}

TEST_CASE("the local player receives Tick so the level fade in clears", "[data]")
{
    auto world = openMap("RagnarVillage2");
    rune::UObject* player = world->player();
    REQUIRE(player != nullptr);
    float* fade = reinterpret_cast<float*>(player->propertyAddress(rune::Name("LevelFadeAlpha")));
    REQUIRE(fade != nullptr);
    *fade = 1.0f;
    rune::levelinfo::FadeRate(world->levelInfo) = 1.0f;
    for (int i = 0; i < 15; i++) {
        world->tick(1.0f / 30.0f);
    }
    CHECK(*fade == Catch::Approx(0.5f).margin(0.01f));
    for (int i = 0; i < 30; i++) {
        world->tick(1.0f / 30.0f);
    }
    CHECK(*fade == 0.0f);
    closeMap(world, "RagnarVillage2");
}

TEST_CASE("an alpha blended canvas takes its alpha from AlphaScale", "[data]")
{
    rune::engine::initialize(RUNE_GAME_DATA);
    rune::UObject* canvas = rune::objects::newObject(rune::objects::findClass("Engine.Canvas"), rune::objects::packageObject(rune::Name("Transient")), rune::Name(), 0);
    REQUIRE(canvas != nullptr);
    rune::canvas::AlphaScale(canvas) = 0.17f;
    rune::canvas::Style(canvas) = 5;
    CHECK(rune::canvasColor(canvas).a == 0.17f);
    rune::canvas::Style(canvas) = 1;
    CHECK(rune::canvasColor(canvas).a == 1.0f);
}

TEST_CASE("the screen flash darkens by FlashScale and adds FlashFog", "[data]")
{
    rune::engine::initialize(RUNE_GAME_DATA);
    rune::UObject* player = rune::objects::newObject(rune::objects::findClass("RuneI.Ragnar"), rune::objects::packageObject(rune::Name("Transient")), rune::Name(), 0);
    REQUIRE(player != nullptr);
    rune::LinearColor color;
    rune::playerpawn::FlashScale(player) = { 1, 1, 1 };
    rune::playerpawn::FlashFog(player) = { 0, 0, 0 };
    CHECK_FALSE(rune::screenFlash(player, color));
    rune::playerpawn::FlashScale(player) = { 0.25f, 0.25f, 0.25f };
    rune::playerpawn::FlashFog(player) = { 0.5f, 0, 2.0f };
    REQUIRE(rune::screenFlash(player, color));
    CHECK(color.r == 0.5f);
    CHECK(color.g == 0.0f);
    CHECK(color.b == 1.0f);
    CHECK(color.a == 0.75f);
    rune::playerpawn::FlashScale(player) = { 0, 0, 0 };
    rune::playerpawn::FlashFog(player) = { 0, 0, 0 };
    REQUIRE(rune::screenFlash(player, color));
    CHECK(color.a == 1.0f);
}

TEST_CASE("decals blend with the style the level designer gave them", "[data]")
{
    rune::engine::initialize(RUNE_GAME_DATA);
    rune::UObject* decal = rune::objects::newObject(rune::objects::findClass("RuneI.WorldDecal"), rune::objects::packageObject(rune::Name("Transient")), rune::Name(), 0);
    REQUIRE(decal != nullptr);
    rune::actor::Style(decal) = 2;
    CHECK(rune::decalPolyFlags(decal) == 0x2u);
    rune::actor::Style(decal) = 3;
    CHECK(rune::decalPolyFlags(decal) == 0x4u);
    rune::actor::Style(decal) = 4;
    CHECK(rune::decalPolyFlags(decal) == 0x40u);
    rune::actor::Style(decal) = 5;
    CHECK(rune::decalPolyFlags(decal) == 0x1000u);
    rune::actor::Style(decal) = 1;
    CHECK(rune::decalPolyFlags(decal) == 0u);
}

TEST_CASE("special rendered actors such as look targets are only drawn in the editor", "[data]")
{
    rune::engine::initialize(RUNE_GAME_DATA);
    rune::UObject* target = rune::objects::newObject(rune::objects::findClass("RuneI.LookTarget"), rune::objects::packageObject(rune::Name("Transient")), rune::Name(), 0);
    REQUIRE(target != nullptr);
    REQUIRE(rune::actor::bHidden(target) == 0);
    REQUIRE(rune::actor::bSpecialRender(target) != 0);
    CHECK(rune::hiddenInGame(target));
    rune::objects::setEditorMode(true);
    CHECK_FALSE(rune::hiddenInGame(target));
    rune::objects::setEditorMode(false);
}

TEST_CASE("polyobjs render the faces of their BSP nodes with the level surface textures", "[data]")
{
    auto world = openMap("DwarfMap2");
    rune::UObject* polyobj = nullptr;
    for (rune::UObject* a : world->actors) {
        if (a && !a->destroyed && a->name == rune::Name("Polyobj20")) {
            polyobj = a;
        }
    }
    REQUIRE(polyobj != nullptr);
    rune::UModel* brush = rune::cast<rune::UModel>(rune::actor::Brush(polyobj));
    REQUIRE(brush != nullptr);
    rune::objects::ensureLoaded(brush);
    rune::objects::ensureLoaded(brush->polys);
    REQUIRE(brush->polys->polys.size() == 11);
    int untextured = 0;
    for (const rune::Poly& poly : brush->polys->polys) {
        if (!poly.texture) {
            untextured++;
        }
    }
    REQUIRE(untextured == 2);
    std::vector<rune::Poly> polys = rune::polyobjRenderPolys(world->level->model, brush);
    CHECK(polys.size() == 13);
    for (const rune::Poly& poly : polys) {
        CHECK(poly.texture != nullptr);
        CHECK(poly.vertices.size() >= 3);
    }
    closeMap(world, "DwarfMap2");
}

TEST_CASE("static polyobjs are lit by the lights baked into their brush", "[data]")
{
    auto world = openMap("DwarfMap2");
    rune::UObject* polyobj = nullptr;
    for (rune::UObject* a : world->actors) {
        if (a && !a->destroyed && a->name == rune::Name("Polyobj10")) {
            polyobj = a;
        }
    }
    REQUIRE(polyobj != nullptr);
    REQUIRE(rune::polyobj::bDynamicLightPolyobj(polyobj) == 0);
    rune::UModel* brush = rune::cast<rune::UModel>(rune::actor::Brush(polyobj));
    REQUIRE(brush != nullptr);
    rune::objects::ensureLoaded(brush->polys);
    std::shared_ptr<rune::UModel> frame = rune::brushFrameModel(brush, rune::polyobjRenderPolys(world->level->model, brush));
    REQUIRE(frame->surfs.size() == 26);
    rune::LightmapTransform transform;
    transform.identity = false;
    transform.location = rune::actor::Location(polyobj);
    transform.rotation = rune::rotationAxes(rune::actor::Rotation(polyobj));
    transform.prePivot = rune::actor::PrePivot(polyobj);
    auto brightest = [&](size_t index, int32_t& width, int32_t& height) {
        const rune::BspSurf& surf = frame->surfs[index];
        REQUIRE(surf.lightMap >= 0);
        rune::SurfaceLighting lighting = rune::prepareSurfaceLighting(frame.get(), surf, {}, transform, nullptr);
        std::vector<uint8_t> bytes;
        rune::evaluateSurfaceLighting(lighting, frame.get(), surf, 0, bytes);
        width = lighting.width;
        height = lighting.height;
        int peak = 0;
        for (size_t i = 0; i < bytes.size(); i++) {
            if (i % 4 != 3) {
                peak = std::max(peak, static_cast<int>(bytes[i]));
            }
        }
        return peak;
    };
    int32_t width = 0;
    int32_t height = 0;
    int lit = brightest(16, width, height);
    CHECK(width == 6);
    CHECK(height == 2);
    CHECK(lit == 100);
    lit = brightest(24, width, height);
    CHECK(width == 2);
    CHECK(height == 6);
    CHECK(lit == 38);
    closeMap(world, "DwarfMap2");
}

TEST_CASE("lights flagged not to affect actors leave actor lighting alone", "[data]")
{
    rune::engine::initialize(RUNE_GAME_DATA);
    rune::UObject* outer = rune::objects::packageObject(rune::Name("Transient"));
    rune::UObject* light = rune::objects::newObject(rune::objects::findClass("Engine.Light"), outer, rune::Name(), 0);
    rune::UObject* pawn = rune::objects::newObject(rune::objects::findClass("RuneI.Ragnar"), outer, rune::Name(), 0);
    REQUIRE(light != nullptr);
    REQUIRE(pawn != nullptr);
    rune::actor::LightRadius(light) = 10;
    rune::actor::Location(light) = { 0, 0, 0 };
    rune::actor::Location(pawn) = { 200, 0, 0 };
    REQUIRE(rune::actor::bAffectActors(light) != 0);
    CHECK(rune::lightReachesActor(pawn, light));
    rune::actor::bAffectActors(light) = 0;
    CHECK_FALSE(rune::lightReachesActor(pawn, light));
    rune::actor::bAffectActors(light) = 1;
    rune::actor::Location(pawn) = { 300, 0, 0 };
    CHECK_FALSE(rune::lightReachesActor(pawn, light));
}

TEST_CASE("moving lights flagged not to affect the world leave world surfaces alone", "[data]")
{
    rune::engine::initialize(RUNE_GAME_DATA);
    rune::UObject* light = rune::objects::newObject(rune::objects::findClass("Engine.Light"), rune::objects::packageObject(rune::Name("Transient")), rune::Name(), 0);
    REQUIRE(light != nullptr);
    rune::actor::LightRadius(light) = 10;
    REQUIRE(rune::actor::bStatic(light) != 0);
    CHECK_FALSE(rune::lightDynamicOnWorld(light));
    rune::actor::bDynamicLight(light) = 1;
    REQUIRE(rune::actor::bAffectWorld(light) != 0);
    CHECK(rune::lightDynamicOnWorld(light));
    rune::actor::bAffectWorld(light) = 0;
    CHECK_FALSE(rune::lightDynamicOnWorld(light));
}

TEST_CASE("zones with a fog distance fog by their fog colour whether or not they are fog zones", "[data]")
{
    auto world = openMap("DeepUnder1Odin");
    rune::UObject* zone = nullptr;
    for (rune::UObject* a : world->actors) {
        if (a && !a->destroyed && a->name == rune::Name("WaterZone0")) {
            zone = a;
        }
    }
    REQUIRE(zone != nullptr);
    REQUIRE(rune::zoneinfo::FogDistance(zone) == 400.0f);
    REQUIRE(rune::zoneinfo::FogHue(zone) == 135);
    REQUIRE(rune::zoneinfo::FogSaturation(zone) == 20);
    REQUIRE(rune::zoneinfo::FogBrightness(zone) == 5);
    rune::ZoneFog fog = rune::zoneDistanceFog(zone);
    CHECK(fog.inverseDistance == 1.0f / 400.0f);
    CHECK(fog.color.x == Catch::Approx(0.0085786f).margin(0.000001));
    CHECK(fog.color.y == Catch::Approx(0.0500837f).margin(0.000001));
    CHECK(fog.color.z == Catch::Approx(0.0678717f).margin(0.000001));
    CHECK(fog.color.z > fog.color.x);
    rune::zoneinfo::bFogZone(zone) = 0;
    CHECK(rune::zoneDistanceFog(zone).inverseDistance == 1.0f / 400.0f);
    rune::zoneinfo::FogDistance(zone) = 0;
    CHECK(rune::zoneDistanceFog(zone).inverseDistance == 0.0f);
    CHECK(rune::zoneDistanceFog(nullptr).inverseDistance == 0.0f);
    closeMap(world, "DeepUnder1Odin");
}

TEST_CASE("actors take the fog of their own zone unless unlit, translucent or a polyobj", "[data]")
{
    auto world = openMap("DeepUnder1Odin");
    rune::UObject* player = world->player();
    REQUIRE(player != nullptr);
    world->tick(1.0f / 30.0f);
    REQUIRE(rune::zoneOf(player) != nullptr);
    REQUIRE(rune::zoneOf(player)->name == rune::Name("WaterZone0"));
    CHECK(rune::actorDistanceFog(player).inverseDistance == 1.0f / 400.0f);
    rune::actor::Style(player) = 4;
    CHECK(rune::actorDistanceFog(player).inverseDistance == 1.0f / 400.0f);
    rune::actor::Style(player) = 3;
    CHECK(rune::actorDistanceFog(player).inverseDistance == 0.0f);
    rune::actor::Style(player) = 1;
    rune::actor::bUnlit(player) = 1;
    CHECK(rune::actorDistanceFog(player).inverseDistance == 0.0f);
    rune::actor::bUnlit(player) = 0;
    rune::UObject* polyobj = rune::objects::newObject(rune::objects::findClass("Engine.Polyobj"), rune::objects::packageObject(rune::Name("Transient")), rune::Name(), 0);
    REQUIRE(polyobj != nullptr);
    *reinterpret_cast<rune::UObject**>(rune::actor::Region(polyobj)) = rune::zoneOf(player);
    CHECK(rune::actorDistanceFog(polyobj).inverseDistance == 0.0f);
    closeMap(world, "DeepUnder1Odin");
}

TEST_CASE("blended decals are not drawn in fog while masked decals are", "[data]")
{
    rune::engine::initialize(RUNE_GAME_DATA);
    rune::UObject* decal = rune::objects::newObject(rune::objects::findClass("RuneI.WorldDecal"), rune::objects::packageObject(rune::Name("Transient")), rune::Name(), 0);
    REQUIRE(decal != nullptr);
    rune::ZoneFog fog;
    fog.inverseDistance = 1.0f / 400.0f;
    rune::ZoneFog clear;
    for (uint8_t style : { 3, 4, 5 }) {
        rune::actor::Style(decal) = style;
        CHECK(rune::decalHiddenByFog(decal, fog));
        CHECK_FALSE(rune::decalHiddenByFog(decal, clear));
    }
    for (uint8_t style : { 1, 2 }) {
        rune::actor::Style(decal) = style;
        CHECK_FALSE(rune::decalHiddenByFog(decal, fog));
    }
}

TEST_CASE("zombies stand on the floor of the polyobj cages they start in", "[data]")
{
    auto world = openMap("Hel1");
    rune::UObject* zombie = nullptr;
    for (rune::UObject* a : world->actors) {
        if (a && !a->destroyed && a->name == rune::Name("Zombie0")) {
            zombie = a;
        }
    }
    REQUIRE(zombie != nullptr);
    float startZ = rune::actor::Location(zombie).z;
    for (int i = 0; i < 60; i++) {
        world->tick(1.0f / 30.0f);
    }
    CHECK(rune::pawn::Health(zombie) > 0);
    CHECK(rune::actor::Location(zombie).z > startZ - 40.0f);
    closeMap(world, "Hel1");
}

TEST_CASE("an interpolating camera turns the short way across the yaw wrap", "[data]")
{
    auto world = openMap("DM-Hel");
    rune::Vector base = rune::actor::Location(world->player()) + rune::Vector { 0, 0, 200 };
    rune::UObject* points[4] = {
        spawnPoint(*world, "RuneI.CineInterpolationPoint", base, 1000, "None"),
        spawnPoint(*world, "RuneI.CineInterpolationPoint", base + rune::Vector { 100, 0, 0 }, 1000, "None"),
        spawnPoint(*world, "RuneI.CineInterpolationPoint", base + rune::Vector { 200, 0, 0 }, 63536, "None"),
        spawnPoint(*world, "RuneI.CineInterpolationPoint", base + rune::Vector { 300, 0, 0 }, 63536, "None"),
    };
    for (int i = 0; i < 3; i++) {
        link(points[i], points[i + 1]);
    }
    rune::UObject* camera = startCamera(*world, points[1]);
    for (int i = 0; i < 15; i++) {
        world->tick(1.0f / 30.0f);
        int32_t yaw = rune::actor::Rotation(camera).yaw & 65535;
        CHECK((yaw <= 1000 || yaw >= 63536));
    }
    closeMap(world, "DM-Hel");
}

TEST_CASE("an interpolation point fires its event when the camera reaches it", "[data]")
{
    auto world = openMap("DM-Hel");
    rune::Vector base = rune::actor::Location(world->player()) + rune::Vector { 0, 0, 200 };
    rune::UObject* first = spawnPoint(*world, "RuneI.CineInterpolationPoint", base, 0, "LeftPoint");
    rune::UObject* second = spawnPoint(*world, "RuneI.CineInterpolationPoint", base + rune::Vector { 100, 0, 0 }, 0, "ReachedPoint");
    rune::UObject* third = spawnPoint(*world, "RuneI.CineInterpolationPoint", base + rune::Vector { 200, 0, 0 }, 0, "None");
    link(first, second);
    link(second, third);
    rune::UObject* left = spawnCounter(*world, "LeftPoint");
    rune::UObject* reached = spawnCounter(*world, "ReachedPoint");
    rune::UObject* camera = startCamera(*world, first);
    for (int i = 0; i < 36; i++) {
        world->tick(1.0f / 30.0f);
    }
    CHECK(rune::actor::Target(camera) == second);
    CHECK(*reached->propertyAddress(rune::Name("NumToCount")) == 9);
    CHECK(*left->propertyAddress(rune::Name("NumToCount")) == 10);
    closeMap(world, "DM-Hel");
}

TEST_CASE("meshes are lit by each light from the actor centre without a specular term")
{
    std::vector<rune::MeshLight> lights { rune::meshLight({ 100, 0, 0 }, 200, { 1.0f, 0.5f, 0.25f }, { 0, 0, 0 }) };
    REQUIRE(lights[0].falloff == Catch::Approx(0.5f));
    rune::Vector ambient { 0.1f, 0.1f, 0.1f };
    rune::Vector facing = rune::meshVertexLight(lights, { 1, 0, 0 }, ambient, 1.0f);
    CHECK(facing.x == Catch::Approx(0.975f).margin(0.0001f));
    CHECK(facing.y == Catch::Approx(0.5375f).margin(0.0001f));
    CHECK(facing.z == Catch::Approx(0.31875f).margin(0.0001f));
    rune::Vector away = rune::meshVertexLight(lights, { -1, 0, 0 }, ambient, 1.0f);
    CHECK(away.x == Catch::Approx(0.1f));
    CHECK(away.y == Catch::Approx(0.1f));
    rune::Vector side = rune::meshVertexLight(lights, { 0, 1, 0 }, ambient, 1.0f);
    CHECK(side.x == Catch::Approx(0.1f));
    rune::Vector centre = rune::meshVertexLight(lights, { 0, 0, 0 }, ambient, 1.0f);
    CHECK(centre.x == Catch::Approx(0.6f).margin(0.0001f));
    CHECK(centre.y == Catch::Approx(0.35f).margin(0.0001f));
    CHECK(centre.z == Catch::Approx(0.225f).margin(0.0001f));
    rune::Vector glowing = rune::meshVertexLight(lights, { 1, 0, 0 }, ambient, 3.0f);
    CHECK(glowing.x == 1.0f);
}

TEST_CASE("ColorAdjust blends the lit colour toward the adjust colour")
{
    rune::Vector grey { 0.5f, 0.5f, 0.5f };
    rune::Vector unchanged = rune::adjustColor(grey, { 0, 0, 0 });
    CHECK(unchanged.x == 0.5f);
    rune::Vector reddened = rune::adjustColor(grey, { 51, 0, 0 });
    CHECK(reddened.x == Catch::Approx(0.6f).margin(0.0001f));
    CHECK(reddened.y == Catch::Approx(0.4f).margin(0.0001f));
    CHECK(reddened.z == Catch::Approx(0.4f).margin(0.0001f));
    rune::Vector red = rune::adjustColor(grey, { 255, 0, 0 });
    CHECK(red.x == Catch::Approx(1.0f));
    CHECK(red.y == Catch::Approx(0.0f).margin(0.0001f));
}

namespace {
    double playerBrightness(const std::string& map)
    {
        auto world = openMap(map);
        for (int i = 0; i < 60; i++) {
            world->tick(1.0f / 30.0f);
        }
        rune::Vector location;
        rune::Rotator rotation;
        float fov = 90;
        REQUIRE(world->calcView(location, rotation, fov));
        rune::SceneRenderer renderer(std::make_unique<rune::SoftRenderDevice>());
        const int width = 320;
        const int height = 240;
        renderer.setTime(world->timeSeconds);
        renderer.beginFrame(width, height);
        renderer.drawScene(world->level, world->actors, location, rotation, fov, nullptr);
        renderer.beginFrame(width, height);
        renderer.drawScene(world->level, world->actors, location, rotation, fov, nullptr);
        std::vector<uint8_t> rgba;
        int readWidth = 0;
        int readHeight = 0;
        renderer.readPixels(rgba, readWidth, readHeight);
        double sum = 0;
        int count = 0;
        for (int y = height * 55 / 100; y < height * 85 / 100; y++) {
            for (int x = width * 42 / 100; x < width * 58 / 100; x++) {
                const uint8_t* pixel = rgba.data() + (static_cast<size_t>(y) * width + static_cast<size_t>(x)) * 4;
                sum += pixel[0] + pixel[1] + pixel[2];
                count += 3;
            }
        }
        closeMap(world, map);
        return sum / count;
    }
}

TEST_CASE("the player is lit as dimly as in the original at the DeepUnder3 and RagnarVillage2 starts", "[data]")
{
    double cave = playerBrightness("DeepUnder3");
    CHECK(cave < 12.0);
    double village = playerBrightness("RagnarVillage2");
    CHECK(village > 34.0);
    CHECK(village < 46.0);
}

TEST_CASE("far clip fog zones clear to their fog colour and report their fog distance", "[data]")
{
    auto world = openMap("Mountain2");
    world->tick(1.0f / 30.0f);
    rune::UObject* zone = rune::zoneOf(world->player());
    REQUIRE(zone != nullptr);
    REQUIRE(zone->name == rune::Name("ZoneInfo25"));
    REQUIRE(rune::zoneinfo::bFogZone(zone) != 0);
    REQUIRE(rune::zoneinfo::bFarClipZone(zone) != 0);
    REQUIRE(rune::zoneinfo::FogHue(zone) == 142);
    REQUIRE(rune::zoneinfo::FogSaturation(zone) == 90);
    REQUIRE(rune::zoneinfo::FogBrightness(zone) == 70);
    rune::Vector color;
    float distance = 0;
    REQUIRE(rune::farClipZone(zone, color, distance));
    CHECK(distance == 2000.0f);
    CHECK(color.x == Catch::Approx(0.1507279f).margin(0.000001));
    CHECK(color.y == Catch::Approx(0.2417558f).margin(0.000001));
    CHECK(color.z == Catch::Approx(0.3360346f).margin(0.000001));
    rune::zoneinfo::bFarClipZone(zone) = 0;
    CHECK_FALSE(rune::farClipZone(zone, color, distance));
    CHECK_FALSE(rune::farClipZone(nullptr, color, distance));
    closeMap(world, "Mountain2");
}

TEST_CASE("the software renderer clips the world at the far clip distance and shows the fog colour behind it", "[data]")
{
    auto world = openMap("Mountain2");
    world->tick(1.0f / 30.0f);
    rune::UObject* zone = rune::zoneOf(world->player());
    REQUIRE(zone != nullptr);
    rune::Vector color;
    float distance = 0;
    REQUIRE(rune::farClipZone(zone, color, distance));
    REQUIRE(distance == 2000.0f);
    const int expected[3] = { static_cast<int>(color.x * 255.0f + 0.5f), static_cast<int>(color.y * 255.0f + 0.5f), static_cast<int>(color.z * 255.0f + 0.5f) };
    rune::Vector location;
    rune::Rotator rotation;
    float fov = 90;
    REQUIRE(world->calcView(location, rotation, fov));

    auto fogPixels = [&](float fogDistance) {
        rune::SceneRenderer renderer(std::make_unique<rune::SoftRenderDevice>());
        rune::RenderOptions options;
        options.fogDistance = fogDistance;
        renderer.setOptions(options);
        renderer.setTime(world->timeSeconds);
        renderer.beginFrame(160, 120);
        renderer.drawScene(world->level, world->actors, location, rotation, fov, nullptr);
        renderer.beginFrame(160, 120);
        renderer.drawScene(world->level, world->actors, location, rotation, fov, nullptr);
        std::vector<uint8_t> rgba;
        int width = 0;
        int height = 0;
        renderer.readPixels(rgba, width, height);
        int matching = 0;
        for (size_t i = 0; i + 3 < rgba.size(); i += 4) {
            if (std::abs(rgba[i] - expected[0]) <= 1 && std::abs(rgba[i + 1] - expected[1]) <= 1 && std::abs(rgba[i + 2] - expected[2]) <= 1) {
                matching++;
            }
        }
        return static_cast<double>(matching) / (160.0 * 120.0);
    };

    CHECK(fogPixels(1.0f) < 0.2);
    CHECK(fogPixels(0.005f) > 0.9);
    closeMap(world, "Mountain2");
}

TEST_CASE("a mirrored skeletal actor is the left-right reflection of the unmirrored one", "[data]")
{
    auto world = openMap("RagnarVillage2");
    rune::UObject* player = world->player();
    REQUIRE(player != nullptr);
    rune::USkelModel* model = rune::skeletal::geometryModel(player);
    REQUIRE(model != nullptr);
    REQUIRE_FALSE(model->meshes.empty());
    REQUIRE(rune::actor::Rotation(player).pitch == 0);
    REQUIRE(rune::actor::Rotation(player).roll == 0);

    rune::SkelPose plain;
    rune::SkelPose mirrored;
    REQUIRE(rune::skeletal::framePose(player, rune::Name("baseframe"), 0, plain));
    rune::actor::bMirrored(player) = 1;
    REQUIRE(rune::skeletal::framePose(player, rune::Name("baseframe"), 0, mirrored));
    rune::actor::bMirrored(player) = 0;
    REQUIRE(plain.joints.size() == mirrored.joints.size());

    float yaw = static_cast<float>(rune::actor::Rotation(player).yaw) * 6.2831853f / 65536.0f;
    rune::Vector right { -std::sin(yaw), std::cos(yaw), 0 };
    rune::Vector pivot = rune::actor::Location(player) + rune::actor::PrePivot(player);
    auto reflect = [&](const rune::Vector& point) {
        float side = (point - pivot).dot(right);
        return point - right * (2 * side);
    };

    float widest = 0;
    for (size_t j = 0; j < plain.joints.size(); j++) {
        INFO("joint " << j);
        rune::Vector expected = reflect(plain.joints[j].origin);
        CHECK((mirrored.joints[j].origin - expected).size() < 0.05f);
        widest = std::max(widest, std::abs((plain.joints[j].origin - pivot).dot(right)));
    }
    CHECK(widest > 10.0f);

    const rune::SkelMesh& mesh = model->meshes[0];
    std::vector<rune::Vector> plainSkin;
    std::vector<rune::Vector> mirroredSkin;
    rune::skeletal::skin(*model, mesh, plain, false, true, plainSkin);
    rune::skeletal::skin(*model, mesh, mirrored, true, true, mirroredSkin);
    REQUIRE(plainSkin.size() == 659);
    REQUIRE(mirroredSkin.size() == 659);
    int off = 0;
    for (size_t i = 0; i < plainSkin.size(); i++) {
        if ((mirroredSkin[i] - reflect(plainSkin[i])).size() > 0.05f) {
            off++;
        }
    }
    CHECK(off == 0);
    closeMap(world, "RagnarVillage2");
}

namespace {
    struct AlphaSoftDevice : rune::SoftRenderDevice {
        bool blendsAlpha() override
        {
            return true;
        }
    };
}

TEST_CASE("an alpha blended player shows one faded layer of its surface over the world behind it", "[data]")
{
    auto world = openMap("RagnarVillage2");
    for (int i = 0; i < 5; i++) {
        world->tick(0.05f);
    }
    rune::UObject* player = world->player();
    REQUIRE(player != nullptr);
    float yaw = static_cast<float>(rune::actor::Rotation(player).yaw) * 6.2831853f / 65536.0f;
    rune::Vector center = rune::actor::Location(player);
    rune::Vector eye = center + rune::Vector { std::cos(yaw), std::sin(yaw), 0 } * 120.0f + rune::Vector { 0, 0, 20 };
    rune::Rotator rotation = rune::vectorRotation(center - eye);
    auto render = [&]() {
        rune::SceneRenderer renderer(std::make_unique<AlphaSoftDevice>());
        renderer.setTime(world->timeSeconds);
        renderer.beginFrame(160, 120);
        renderer.drawScene(world->level, world->actors, eye, rotation, 90, nullptr);
        renderer.beginFrame(160, 120);
        renderer.drawScene(world->level, world->actors, eye, rotation, 90, nullptr);
        std::vector<uint8_t> rgba;
        int width = 0;
        int height = 0;
        renderer.readPixels(rgba, width, height);
        return rgba;
    };
    std::vector<uint8_t> opaque = render();
    rune::actor::bHidden(player) = 1;
    std::vector<uint8_t> background = render();
    rune::actor::bHidden(player) = 0;
    rune::actor::Style(player) = 5;
    rune::actor::AlphaScale(player) = 0.5f;
    std::vector<uint8_t> faded = render();
    REQUIRE(opaque.size() == faded.size());
    REQUIRE(background.size() == faded.size());
    int covered = 0;
    int blended = 0;
    for (size_t i = 0; i + 3 < faded.size(); i += 4) {
        bool onPlayer = false;
        bool halfway = true;
        for (size_t c = 0; c < 3; c++) {
            int front = opaque[i + c];
            int behind = background[i + c];
            if (std::abs(front - behind) > 8) {
                onPlayer = true;
            }
            if (std::abs(2 * faded[i + c] - (front + behind)) > 6) {
                halfway = false;
            }
        }
        if (onPlayer) {
            covered++;
            if (halfway) {
                blended++;
            }
        }
    }
    REQUIRE(covered > 500);
    CHECK(blended * 10 >= covered * 9);
    closeMap(world, "RagnarVillage2");
}

TEST_CASE("a sight trigger fires only once the player's view sees it, opening the chieftain's longhouse door", "[data]")
{
    auto world = openMap("RagnarVillage2");
    rune::UObject* player = world->player();
    rune::UObject* sight = nullptr;
    rune::UObject* door = nullptr;
    for (rune::UObject* a : world->actors) {
        if (a && !a->destroyed && a->name == rune::Name("SightTrigger0")) {
            sight = a;
        }
        if (a && !a->destroyed && a->name == rune::Name("RunePolyobj0")) {
            door = a;
        }
    }
    REQUIRE(player != nullptr);
    REQUIRE(sight != nullptr);
    REQUIRE(door != nullptr);
    rune::vm::Call(sight, rune::Name("Trigger")).obj(player).obj(player).run();
    REQUIRE(rune::trigger::bInitiallyActive(sight) != 0);

    rune::Vector eye { 1060, 2050, -200 };
    auto look = [&](const rune::Rotator& rotation) {
        rune::SceneRenderer renderer(std::make_unique<rune::SoftRenderDevice>());
        renderer.beginFrame(160, 120);
        renderer.drawScene(world->level, world->actors, eye, rotation, 90, nullptr);
        renderer.beginFrame(160, 120);
        renderer.drawScene(world->level, world->actors, eye, rotation, 90, nullptr);
        for (int i = 0; i < 90; i++) {
            world->tick(1.0f / 30.0f);
        }
    };

    look({ 0, 16384, 0 });
    CHECK(rune::polyobj::KeyNum(door) == 0);
    CHECK(rune::actor::bHidden(sight) == 0);

    look(rune::vectorRotation(rune::actor::Location(sight) - eye));
    CHECK(rune::polyobj::KeyNum(door) == 1);
    CHECK(rune::actor::Rotation(door).yaw == -1024);
    CHECK(rune::actor::bHidden(sight) != 0);
    closeMap(world, "RagnarVillage2");
}

TEST_CASE("skeletal LOD curves follow the original distance shapes and percent clamps", "[data]")
{
    auto world = openMap("RagnarVillage2");
    rune::UObject* sven = nullptr;
    for (rune::UObject* a : world->actors) {
        if (a && !a->destroyed && a->name == rune::Name("Sven1")) {
            sven = a;
        }
    }
    REQUIRE(sven != nullptr);
    rune::actor::LODDistMax(sven) = 2000;
    rune::actor::DrawScale(sven) = 2;
    rune::actor::LODPercentMin(sven) = 0;
    rune::actor::LODPercentMax(sven) = 1;
    rune::Vector eye = rune::actor::Location(sven) + rune::Vector { 1000, 0, 0 };
    const float expected[7] = { 0.0f, 0.0625f, 0.15625f, 0.25f, 0.375f, 0.5f, 0.0761205f };
    for (uint8_t curve = 0; curve < 7; curve++) {
        INFO("curve " << static_cast<int>(curve));
        rune::actor::LODCurve(sven) = curve;
        CHECK(std::abs(rune::skeletal::lodCurve(sven, eye) - expected[curve]) < 1e-5f);
    }
    rune::actor::LODCurve(sven) = 3;
    CHECK(rune::skeletal::lodCurve(sven, rune::actor::Location(sven) + rune::Vector { 9000, 0, 0 }) == 1.0f);
    rune::actor::LODPercentMax(sven) = 0.5f;
    CHECK(rune::skeletal::lodCurve(sven, eye) == 0.5f);
    rune::actor::LODPercentMax(sven) = 1;
    rune::actor::LODPercentMin(sven) = 0.9f;
    CHECK(std::abs(rune::skeletal::lodCurve(sven, eye) - 0.1f) < 1e-6f);
    closeMap(world, "RagnarVillage2");
}

TEST_CASE("the skeletal poly budget raises decimation the way the original renderer does", "[data]")
{
    auto world = openMap("RagnarVillage2");
    rune::UObject* sven = nullptr;
    for (rune::UObject* a : world->actors) {
        if (a && !a->destroyed && a->name == rune::Name("Sven1")) {
            sven = a;
        }
    }
    REQUIRE(sven != nullptr);
    rune::USkelModel* model = rune::skeletal::geometryModel(sven);
    REQUIRE(model != nullptr);
    const rune::SkelMesh& mesh = model->meshes[rune::actor::SkelMesh(sven)];
    REQUIRE(mesh.decCount == 449);
    rune::actor::LODCurve(sven) = 2;
    CHECK(rune::skeletal::lodApproximation(mesh, 0.5f) == 225);
    CHECK(rune::skeletal::decimationLevel(sven, mesh, 0.5f, 1000, 1.0f, false) == 224);
    CHECK(rune::skeletal::decimationLevel(sven, mesh, 0.0f, 5000, 1.0f, false) == 225);
    CHECK(rune::skeletal::decimationLevel(sven, mesh, 0.0f, 5000, 1.0f, true) == 0);
    CHECK(rune::skeletal::decimationLevel(world->player(), mesh, 0.0f, 5000, 1.0f, false) == 169);
    rune::actor::LODCurve(sven) = 0;
    CHECK(rune::skeletal::decimationLevel(sven, mesh, 0.0f, 5000, 1.0f, false) == 0);
    closeMap(world, "RagnarVillage2");
}

TEST_CASE("decimating a skeletal mesh collapses the jaw into the head and restores it exactly", "[data]")
{
    auto world = openMap("RagnarVillage2");
    rune::UObject* sven = nullptr;
    for (rune::UObject* a : world->actors) {
        if (a && !a->destroyed && a->name == rune::Name("Sven1")) {
            sven = a;
        }
    }
    REQUIRE(sven != nullptr);
    rune::USkelModel* model = rune::skeletal::geometryModel(sven);
    REQUIRE(model != nullptr);
    const rune::SkelMesh& mesh = model->meshes[rune::actor::SkelMesh(sven)];
    int32_t jaw = model->jointNamed(rune::Name("Jaw"));
    REQUIRE(jaw == 18);
    REQUIRE(mesh.verts[249].joint1 == jaw);
    auto uses = [&](const rune::SkelDecimation& state, uint16_t vertex) {
        int count = 0;
        for (size_t t = 0; t < state.tris.size(); t++) {
            if (state.removed[t]) {
                continue;
            }
            for (uint16_t v : state.tris[t].vertex) {
                count += v == vertex ? 1 : 0;
            }
        }
        return count;
    };
    auto visible = [&](const rune::SkelDecimation& state) {
        int count = 0;
        for (uint8_t removed : state.removed) {
            count += removed ? 0 : 1;
        }
        return count;
    };

    const rune::SkelDecimation& full = rune::skeletal::decimate(mesh, 0);
    CHECK(visible(full) == 942);
    CHECK(uses(full, 249) > 0);

    const rune::SkelDecimation& reduced = rune::skeletal::decimate(mesh, 5);
    CHECK(reduced.level == 5);
    CHECK(visible(reduced) == 932);
    CHECK(uses(reduced, 249) == 0);
    CHECK(uses(reduced, 282) > 0);

    const rune::SkelDecimation& restored = rune::skeletal::decimate(mesh, 0);
    CHECK(restored.offset == 0);
    CHECK(visible(restored) == 942);
    bool same = true;
    for (size_t t = 0; t < mesh.tris.size(); t++) {
        for (int c = 0; c < 3; c++) {
            same = same && restored.tris[t].vertex[c] == mesh.tris[t].vertex[c] && restored.tris[t].uv[c][0] == mesh.tris[t].uv[c][0] && restored.tris[t].uv[c][1] == mesh.tris[t].uv[c][1];
        }
    }
    CHECK(same);
    closeMap(world, "RagnarVillage2");
}

TEST_CASE("distant skeletal actors stop interpolating animation frames like the original", "[data]")
{
    auto world = openMap("RagnarVillage2");
    rune::UObject* sven = nullptr;
    for (rune::UObject* a : world->actors) {
        if (a && !a->destroyed && a->name == rune::Name("Sven1")) {
            sven = a;
        }
    }
    REQUIRE(sven != nullptr);
    rune::USkelModel* model = rune::skeletal::animationModel(sven);
    REQUIRE(model != nullptr);
    const rune::SkelSequence* sequence = model->findSequence(rune::Name("neutral_idle"));
    REQUIRE(sequence != nullptr);
    REQUIRE(sequence->info.numFrames > 2);
    rune::actor::AnimSequence(sven) = rune::Name("neutral_idle");
    rune::actor::AnimFrame(sven) = 1.5f / static_cast<float>(sequence->info.numFrames);
    rune::actor::BlendAnimAlpha(sven) = 0;
    rune::SkelPose frameOne;
    REQUIRE(rune::skeletal::framePose(sven, rune::Name("neutral_idle"), 1, frameOne));

    rune::skeletal::setLodCurve(sven, 0.95f);
    const rune::SkelPose* coarse = rune::skeletal::pose(sven);
    REQUIRE(coarse != nullptr);
    float coarseOff = 0;
    for (size_t j = 0; j < frameOne.joints.size(); j++) {
        coarseOff = std::max(coarseOff, (coarse->joints[j].origin - frameOne.joints[j].origin).size());
    }
    CHECK(coarseOff < 0.001f);

    rune::skeletal::setLodCurve(sven, 0.0f);
    const rune::SkelPose* smooth = rune::skeletal::pose(sven);
    float smoothOff = 0;
    for (size_t j = 0; j < frameOne.joints.size(); j++) {
        smoothOff = std::max(smoothOff, (smooth->joints[j].origin - frameOne.joints[j].origin).size());
    }
    CHECK(smoothOff > 0.01f);
    closeMap(world, "RagnarVillage2");
}

TEST_CASE("the world tick clamps time steps and dilation to the original limits", "[data]")
{
    auto world = openMap("RagnarVillage2");
    rune::UClass* cls = rune::objects::findClass("Engine.Effects");
    rune::objects::flushLoads();
    rune::UObject* effect = world->spawn(cls, nullptr, rune::Name("TestEffect"), {}, {}, nullptr, true);
    REQUIRE(effect != nullptr);
    rune::actor::LifeSpan(effect) = 10;
    for (int i = 0; i < 100; i++) {
        world->tick(0.002f);
    }
    CHECK(std::abs(rune::actor::LifeSpan(effect) - 9.8f) < 1e-3f);
    for (int i = 0; i < 10; i++) {
        world->tick(0.0002f);
    }
    CHECK(std::abs(rune::actor::LifeSpan(effect) - 9.79f) < 1e-3f);

    rune::levelinfo::TimeDilation(world->levelInfo) = 4;
    float before = rune::levelinfo::TimeSeconds(world->levelInfo);
    world->tick(0.1f);
    CHECK(std::abs(rune::levelinfo::TimeSeconds(world->levelInfo) - before - 0.2f) < 1e-4f);
    CHECK(std::abs(rune::actor::LifeSpan(effect) - 9.59f) < 1e-3f);
    rune::levelinfo::TimeDilation(world->levelInfo) = 0.1f;
    before = rune::levelinfo::TimeSeconds(world->levelInfo);
    world->tick(0.1f);
    CHECK(std::abs(rune::levelinfo::TimeSeconds(world->levelInfo) - before - 0.05f) < 1e-4f);
    CHECK(std::abs(rune::actor::LifeSpan(effect) - 9.54f) < 1e-3f);
    closeMap(world, "RagnarVillage2");
}

TEST_CASE("actors with blocking joints collide with their joint boxes instead of their cylinder", "[data]")
{
    auto world = openMap("RagnarVillage2");
    for (int i = 0; i < 3; i++) {
        world->tick(0.05f);
    }
    rune::UObject* player = world->player();
    REQUIRE(player != nullptr);
    REQUIRE(rune::actor::CollisionRadius(player) == 18.0f);
    rune::UObject* rack = nullptr;
    for (rune::UObject* a : world->actors) {
        if (a && !a->destroyed && a->name.str() == "Skinrack0") {
            rack = a;
        }
    }
    REQUIRE(rack != nullptr);
    REQUIRE(rune::actor::Location(rack).x == Catch::Approx(2345.0f).margin(1.0f));
    REQUIRE(rune::actor::Location(rack).y == Catch::Approx(-43.0f).margin(1.0f));
    REQUIRE(rune::actor::CollisionRadius(rack) == 107.0f);
    rune::Vector extent { 18.0f, 18.0f, rune::actor::CollisionHeight(player) };
    rune::CheckResult hit;
    bool blocked = world->lineCheck(hit, { 2500, 40, -89 }, { 2200, 40, -89 }, extent, player, rune::TRACE_Blocking);
    CHECK_FALSE((blocked && hit.actor == rack));
    blocked = world->lineCheck(hit, { 2230, -43, -89 }, { 2260, -43, -89 }, extent, player, rune::TRACE_Blocking);
    REQUIRE(blocked);
    CHECK(hit.actor == rack);
    CHECK(hit.location.x > 2234.5f);
    CHECK(hit.location.x < 2238.0f);
    for (int j = 0; j < rune::SkelMaxJoints; j++) {
        rune::actor::JointFlags(rack, j) = static_cast<uint8_t>(rune::actor::JointFlags(rack, j) & ~0x10);
    }
    blocked = world->lineCheck(hit, { 2500, 40, -89 }, { 2200, 40, -89 }, extent, player, rune::TRACE_Blocking);
    REQUIRE(blocked);
    CHECK(hit.actor == rack);
    CHECK(hit.location.x == Catch::Approx(2438.5f).margin(1.0f));
    closeMap(world, "RagnarVillage2");
}

TEST_CASE("a falling player facing a ledge grabs it through GrabEdge", "[data]")
{
    auto world = openMap("RagnarVillage2");
    for (int i = 0; i < 5; i++) {
        world->tick(0.05f);
    }
    rune::UObject* player = world->player();
    REQUIRE(player != nullptr);
    REQUIRE(rune::actor::CollisionRadius(player) == 18.0f);
    REQUIRE(rune::actor::CollisionHeight(player) == 42.0f);
    rune::CheckResult top;
    REQUIRE(world->lineCheck(top, { -110.0f, -752.8f, 0.0f }, { -110.0f, -752.8f, -400.0f }, {}, nullptr, rune::TRACE_World));
    rune::Vector start { -138.0f, -752.8f, -294.6f };
    for (int pass = 0; pass < 2; pass++) {
        REQUIRE(world->farMoveActor(player, start, false, false));
        rune::vm::gotoState(player, rune::Name("PlayerWalking"));
        rune::actor::Rotation(player) = { 0, 0, 0 };
        rune::pawn::ViewRotation(player) = { 0, 0, 0 };
        rune::actor::Velocity(player) = { 0, 0, 0 };
        rune::actor::Physics(player) = rune::PHYS_Falling;
        rune::pawn::bCanGrabEdges(player) = pass == 0 ? 1 : 0;
        world->tick(0.02f);
        if (pass == 0) {
            CHECK(rune::vm::stateName(player) == rune::Name("EdgeHanging"));
            CHECK(rune::actor::Location(player).x == Catch::Approx(-116.0f).margin(0.1f));
            CHECK(rune::actor::Location(player).y == Catch::Approx(-752.8f).margin(0.1f));
            CHECK(rune::actor::Location(player).z == Catch::Approx(top.location.z + 8.0f + 42.0f).margin(1.0f));
        } else {
            CHECK(rune::vm::stateName(player) == rune::Name("PlayerWalking"));
            CHECK(rune::actor::Location(player).x < -130.0f);
        }
    }
    closeMap(world, "RagnarVillage2");
}

TEST_CASE("falling pawns steer with their own AirControl and keep their acceleration", "[data]")
{
    auto world = openMap("RagnarVillage2");
    for (int i = 0; i < 5; i++) {
        world->tick(0.05f);
    }
    rune::UObject* player = world->player();
    REQUIRE(player != nullptr);
    REQUIRE(rune::pawn::AirControl(player) == 0.25f);
    REQUIRE(world->farMoveActor(player, rune::actor::Location(player) + rune::Vector { 0, 0, 300 }, false, false));
    rune::actor::Physics(player) = rune::PHYS_Falling;
    rune::actor::Velocity(player) = { 100, 0, 0 };
    rune::actor::Acceleration(player) = { 100000, 0, 0 };
    REQUIRE(rune::pawn::AccelRate(player) == 2048.0f);
    rune::performPhysics(player, 0.05f);
    CHECK(rune::actor::Velocity(player).x == Catch::Approx(125.6f).margin(0.05f));
    CHECK(rune::actor::Acceleration(player).x == 100000.0f);
    closeMap(world, "RagnarVillage2");
}

TEST_CASE("an actor falling outside the world gets FellOutOfWorld", "[data]")
{
    auto world = openMap("RagnarVillage2");
    for (int i = 0; i < 5; i++) {
        world->tick(0.05f);
    }
    rune::UObject* player = world->player();
    REQUIRE(player != nullptr);
    REQUIRE(rune::pawn::Health(player) > 0);
    rune::actor::Physics(player) = rune::PHYS_Falling;
    rune::region(player).zoneNumber = 0;
    rune::performPhysics(player, 0.05f);
    CHECK(rune::pawn::Health(player) == -1);
    CHECK(rune::vm::stateName(player) == rune::Name("Dying"));
    closeMap(world, "RagnarVillage2");
}

TEST_CASE("drawing a pawn with ActorShadows 1 makes it spawn its blob shadow", "[data]")
{
    auto world = openMap("RagnarVillage2");
    for (int i = 0; i < 3; i++) {
        world->tick(0.05f);
    }
    rune::UObject* villager = nullptr;
    rune::Vector eye;
    for (rune::UObject* a : world->actors) {
        if (villager || !a || a->destroyed || !a->isA(rune::Name("ScriptPawn")) || rune::actor::DrawType(a) != 6) {
            continue;
        }
        float* scale = reinterpret_cast<float*>(a->propertyAddress(rune::Name("ShadowScale")));
        if (!scale || *scale <= 0) {
            continue;
        }
        for (int dir = 0; dir < 8 && !villager; dir++) {
            float angle = static_cast<float>(dir) * 0.785398f;
            rune::Vector candidate = rune::actor::Location(a) + rune::Vector { std::cos(angle) * 150.0f, std::sin(angle) * 150.0f, 20.0f };
            rune::CheckResult hit;
            if (!world->lineCheck(hit, rune::actor::Location(a), candidate, {}, nullptr, rune::TRACE_World)) {
                villager = a;
                eye = candidate;
            }
        }
    }
    REQUIRE(villager != nullptr);
    rune::UObject** shadow = reinterpret_cast<rune::UObject**>(villager->propertyAddress(rune::Name("Shadow")));
    REQUIRE(shadow != nullptr);
    REQUIRE(*shadow == nullptr);
    rune::Rotator look = rune::vectorRotation(rune::actor::Location(villager) - eye);
    for (int setting = 0; setting < 2; setting++) {
        rune::SceneRenderer renderer(std::make_unique<rune::SoftRenderDevice>());
        rune::RenderOptions options;
        options.actorShadows = setting;
        renderer.setOptions(options);
        renderer.beginFrame(160, 120);
        renderer.drawScene(world->level, world->actors, eye, look, 90, nullptr);
        renderer.beginFrame(160, 120);
        renderer.drawScene(world->level, world->actors, eye, look, 90, nullptr);
        if (setting == 0) {
            CHECK(*shadow == nullptr);
        } else {
            REQUIRE(*shadow != nullptr);
            CHECK((*shadow)->isA(rune::Name("PlayerShadow")));
            CHECK(rune::actor::Owner(*shadow) == villager);
        }
    }
    closeMap(world, "RagnarVillage2");
}

TEST_CASE("pawns count down AmbientSoundTime and get AmbientSoundTimer", "[data]")
{
    auto world = openMap("RagnarVillage2");
    for (int i = 0; i < 3; i++) {
        world->tick(0.05f);
    }
    rune::UClass* cls = rune::objects::findClass("RuneI.GiantCrab");
    rune::objects::flushLoads();
    REQUIRE(cls != nullptr);
    rune::UObject* crab = world->spawn(cls, nullptr, rune::Name(), rune::actor::Location(world->player()) + rune::Vector { 300, 0, 0 }, {}, nullptr, true);
    REQUIRE(crab != nullptr);
    REQUIRE(rune::vm::gotoState(crab, rune::Name("InShell")));
    rune::pawn::AmbientSoundTime(crab) = 0.03f;
    world->tick(0.05f);
    REQUIRE(rune::vm::stateName(crab) == rune::Name("InShell"));
    CHECK(rune::pawn::AmbientSoundTime(crab) >= 8.5f);
    CHECK(rune::pawn::AmbientSoundTime(crab) <= 17.0f);
    rune::pawn::AmbientSoundTime(crab) = 1.0f;
    world->tick(0.05f);
    CHECK(rune::pawn::AmbientSoundTime(crab) == Catch::Approx(0.95f).margin(0.001f));
    closeMap(world, "RagnarVillage2");
}

TEST_CASE("moving through a touchable skeleton sends JointTouchedBy for the touched joint", "[data]")
{
    auto world = openMap("RagnarVillage2");
    for (int i = 0; i < 3; i++) {
        world->tick(0.05f);
    }
    rune::UObject* player = world->player();
    REQUIRE(player != nullptr);
    rune::UClass* cls = rune::objects::findClass("RuneI.HangingChain");
    rune::objects::flushLoads();
    REQUIRE(cls != nullptr);
    rune::Vector origin = rune::actor::Location(player) + rune::Vector { 0, 0, 150 };
    rune::UObject* chain = world->spawn(cls, nullptr, rune::Name(), origin, {}, nullptr, true);
    REQUIRE(chain != nullptr);
    world->tick(0.05f);
    const rune::SkelPose* pose = rune::skeletal::pose(chain);
    rune::USkelModel* model = rune::skeletal::animationModel(chain);
    REQUIRE(pose != nullptr);
    REQUIRE(model != nullptr);
    int touchJoint = -1;
    for (size_t j = 0; j < model->joints.size() && touchJoint < 0; j++) {
        if (rune::actor::JointFlags(chain, static_cast<int>(j)) & 0x10) {
            touchJoint = static_cast<int>(j);
        }
    }
    REQUIRE(touchJoint >= 0);
    rune::Vector through = pose->joints[static_cast<size_t>(touchJoint)].origin;
    float* lastSound = reinterpret_cast<float*>(chain->propertyAddress(rune::Name("LastSoundPlayed")));
    REQUIRE(lastSound != nullptr);
    for (int pass = 0; pass < 2; pass++) {
        *lastSound = -100.0f;
        rune::actor::Location(player) = through - rune::Vector { 60, 0, 0 };
        rune::CheckResult hit;
        world->moveActor(player, { 120, 0, 0 }, rune::actor::Rotation(player), hit, pass == 1, false, false, false);
        if (pass == 0) {
            CHECK(*lastSound == rune::levelinfo::TimeSeconds(world->levelInfo));
        } else {
            CHECK(*lastSound == -100.0f);
        }
    }
    closeMap(world, "RagnarVillage2");
}

TEST_CASE("hit-joint traces use the original joint box test and report the joint bit", "[data]")
{
    auto world = openMap("RagnarVillage2");
    for (int i = 0; i < 3; i++) {
        world->tick(0.05f);
    }
    rune::UObject* player = world->player();
    REQUIRE(player != nullptr);
    rune::UObject* rack = nullptr;
    for (rune::UObject* a : world->actors) {
        if (a && !a->destroyed && a->name.str() == "Skinrack0") {
            rack = a;
        }
    }
    REQUIRE(rack != nullptr);
    rune::Vector extent { 18.0f, 18.0f, 42.0f };
    rune::CheckResult hit;
    REQUIRE(world->lineCheck(hit, { 2230, -43, -89 }, { 2260, -43, -89 }, extent, player, rune::TRACE_Others | rune::TRACE_HitJoint));
    CHECK(hit.actor == rack);
    CHECK(hit.location.x > 2234.5f);
    CHECK(hit.location.x < 2238.0f);
    CHECK(hit.item == 0x2);
    CHECK(hit.item2 == 0);
    closeMap(world, "RagnarVillage2");
}

TEST_CASE("spawning an actor runs the level's spawn notifiers for matching classes", "[data]")
{
    const char* xdg = std::getenv("XDG_DATA_HOME");
    REQUIRE(xdg != nullptr);
    std::filesystem::path home = std::filesystem::path(xdg) / "rune-spawnnotify";
    std::filesystem::remove_all(home);
    std::filesystem::create_directories(home / "Classes");
    {
        std::ofstream out(home / "Classes" / "TestNotify.uc", std::ios::binary);
        out << "class TestNotify extends SpawnNotify;\r\n\r\nvar int Count;\r\n\r\nsimulated event Actor SpawnNotification(Actor A)\r\n{\r\n\tCount++;\r\n\tA.Tag = 'Notified';\r\n\treturn A;\r\n}\r\n\r\ndefaultproperties\r\n{\r\n     ActorClass=Class'Engine.Effects'\r\n}\r\n";
    }
    std::filesystem::path output = home / "TestNotify.u";
    std::string command = "XDG_DATA_HOME=\"" + home.string() + "\" \"" UCC_PATH "\" makepackage TestNotify \"" + (home / "Classes").string() + "\" \"" + output.string() + "\" > /dev/null 2>&1";
    REQUIRE(std::system(command.c_str()) == 0);

    auto world = openMap("RagnarVillage2");
    rune::objects::unloadPackage(rune::Name("TestNotify"));
    REQUIRE(rune::objects::loadPackageFile(rune::Name("TestNotify"), output, false) != nullptr);
    rune::UClass* notifyClass = rune::objects::findClass("TestNotify.TestNotify");
    rune::UClass* effects = rune::objects::findClass("Engine.Effects");
    rune::UClass* counter = rune::objects::findClass("Engine.Counter");
    rune::objects::flushLoads();
    REQUIRE(notifyClass != nullptr);
    REQUIRE(effects != nullptr);
    REQUIRE(counter != nullptr);
    rune::UObject* notify = world->spawn(notifyClass, nullptr, rune::Name(), {}, {}, nullptr, true);
    REQUIRE(notify != nullptr);
    REQUIRE(rune::levelinfo::SpawnNotify(world->levelInfo) == notify);
    int32_t* count = reinterpret_cast<int32_t*>(notify->propertyAddress(rune::Name("Count")));
    REQUIRE(count != nullptr);
    CHECK(*count == 0);
    rune::UObject* effect = world->spawn(effects, nullptr, rune::Name(), {}, {}, nullptr, true);
    REQUIRE(effect != nullptr);
    CHECK(rune::actor::Tag(effect) == rune::Name("Notified"));
    CHECK(*count == 1);
    rune::UObject* point = world->spawn(counter, nullptr, rune::Name(), {}, {}, nullptr, true);
    REQUIRE(point != nullptr);
    CHECK(rune::actor::Tag(point) != rune::Name("Notified"));
    CHECK(*count == 1);
    closeMap(world, "RagnarVillage2");
    rune::objects::unloadPackage(rune::Name("TestNotify"));
    std::filesystem::remove_all(home);
}

TEST_CASE("a scripted texture restores its source and lets its notify actor draw tiles and text", "[data]")
{
    const char* xdg = std::getenv("XDG_DATA_HOME");
    REQUIRE(xdg != nullptr);
    std::filesystem::path home = std::filesystem::path(xdg) / "rune-scriptedtexture";
    std::filesystem::remove_all(home);
    std::filesystem::create_directories(home / "Classes");
    {
        std::ofstream out(home / "Classes" / "TexDrawer.uc", std::ios::binary);
        out << "class TexDrawer extends Actor;\r\n\r\nvar Texture Tile;\r\nvar Font TextFont;\r\nvar float TextWidth;\r\nvar float TextHeight;\r\n\r\n"
               "event RenderTexture(ScriptedTexture Tex)\r\n{\r\n\tlocal color Red;\r\n\r\n\tRed.R = 255;\r\n\tTex.DrawTile(2, 3, 4, 2, 0, 0, 4, 2, Tile, false);\r\n"
               "\tTex.DrawColoredText(0, 8, \"A\", TextFont, Red);\r\n\tTex.TextSize(\"AB\", TextWidth, TextHeight, TextFont);\r\n}\r\n";
    }
    std::filesystem::path output = home / "TexDrawer.u";
    std::string command = "XDG_DATA_HOME=\"" + home.string() + "\" \"" UCC_PATH "\" makepackage TexDrawer \"" + (home / "Classes").string() + "\" \"" + output.string() + "\" > /dev/null 2>&1";
    REQUIRE(std::system(command.c_str()) == 0);

    auto world = openMap("RagnarVillage2");
    rune::objects::unloadPackage(rune::Name("TexDrawer"));
    REQUIRE(rune::objects::loadPackageFile(rune::Name("TexDrawer"), output, false) != nullptr);
    rune::UClass* drawerClass = rune::objects::findClass("TexDrawer.TexDrawer");
    rune::UFont* font = rune::cast<rune::UFont>(rune::objects::loadObject("Engine.SmallFont", rune::objects::findClass("Engine.Font")));
    rune::objects::flushLoads();
    REQUIRE(drawerClass != nullptr);
    REQUIRE(font != nullptr);
    rune::UObject* transient = rune::objects::packageObject(rune::Name("Transient"));
    rune::UPalette* palette = rune::cast<rune::UPalette>(rune::objects::newObject(rune::objects::findClass("Engine.Palette"), transient, rune::Name(), 0));
    REQUIRE(palette != nullptr);
    palette->colors.resize(256);
    for (uint32_t i = 0; i < 256; i++) {
        palette->colors[i] = 0xff000000u | i | (i << 8) | (i << 16);
    }
    palette->colors[200] = 0xff0000ffu;
    auto makeTexture = [&](const char* className, int32_t size, uint8_t bits, uint8_t fill) {
        rune::UTexture* texture = rune::cast<rune::UTexture>(rune::objects::newObject(rune::objects::findClass(className), transient, rune::Name(), 0));
        REQUIRE(texture != nullptr);
        rune::bitmap::USize(texture) = size;
        rune::bitmap::VSize(texture) = size;
        rune::bitmap::UClamp(texture) = size;
        rune::bitmap::VClamp(texture) = size;
        rune::bitmap::UBits(texture) = bits;
        rune::bitmap::VBits(texture) = bits;
        rune::bitmap::Palette(texture) = palette;
        rune::TextureMip mip;
        mip.width = static_cast<uint32_t>(size);
        mip.height = static_cast<uint32_t>(size);
        mip.ubits = bits;
        mip.vbits = bits;
        mip.data.assign(static_cast<size_t>(size * size), fill);
        texture->mips.push_back(mip);
        return texture;
    };
    rune::UTexture* source = makeTexture("Engine.Texture", 16, 4, 7);
    rune::UTexture* tile = makeTexture("Engine.Texture", 4, 2, 0);
    for (int i = 0; i < 16; i++) {
        tile->mips[0].data[static_cast<size_t>(i)] = static_cast<uint8_t>(10 + i);
    }
    rune::UTexture* scripted = makeTexture("Engine.ScriptedTexture", 16, 4, 0);
    rune::scriptedtexture::SourceTexture(scripted) = source;
    rune::UObject* drawer = world->spawn(drawerClass, nullptr, rune::Name(), {}, {}, nullptr, true);
    REQUIRE(drawer != nullptr);
    *reinterpret_cast<rune::UObject**>(drawer->propertyAddress(rune::Name("Tile"))) = tile;
    *reinterpret_cast<rune::UObject**>(drawer->propertyAddress(rune::Name("TextFont"))) = font;
    rune::scriptedtexture::NotifyActor(scripted) = drawer;
    scripted->mips[0].data[0] = 99;

    rune::ProceduralTextures procedural;
    std::vector<uint8_t> rgba;
    uint32_t width = 0;
    uint32_t height = 0;
    REQUIRE(procedural.update(scripted, 1.0, rgba, width, height));
    REQUIRE(width == 16);
    REQUIRE(height == 16);
    const std::vector<uint8_t>& pixels = scripted->mips[0].data;
    CHECK(pixels[0] == 7);
    CHECK(pixels[3 * 16 + 1] == 7);
    CHECK(pixels[3 * 16 + 2] == 10);
    CHECK(pixels[3 * 16 + 5] == 13);
    CHECK(pixels[3 * 16 + 6] == 7);
    CHECK(pixels[4 * 16 + 2] == 14);
    CHECK(pixels[4 * 16 + 5] == 17);
    CHECK(pixels[5 * 16 + 2] == 7);
    CHECK(rgba[(3 * 16 + 2) * 4] == 10);
    int red = 0;
    for (int i = 8 * 16; i < 16 * 16; i++) {
        if (pixels[static_cast<size_t>(i)] == 200) {
            red++;
        }
    }
    CHECK(red > 0);
    rune::UTexture* glyphPage = nullptr;
    const rune::FontCharacter* a = font->glyph('A', &glyphPage);
    const rune::FontCharacter* b = font->glyph('B', nullptr);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    CHECK(*reinterpret_cast<float*>(drawer->propertyAddress(rune::Name("TextWidth"))) == static_cast<float>(a->width + b->width));
    CHECK(*reinterpret_cast<float*>(drawer->propertyAddress(rune::Name("TextHeight"))) == static_cast<float>(std::max(a->height, b->height)));
    closeMap(world, "RagnarVillage2");
    rune::objects::unloadPackage(rune::Name("TexDrawer"));
    std::filesystem::remove_all(home);
}

TEST_CASE("the shadow texture fills back-facing triangles with the original row gradient", "[data]")
{
    rune::engine::initialize(RUNE_GAME_DATA);
    rune::UTexture* texture = rune::cast<rune::UTexture>(rune::objects::newObject(rune::objects::findClass("Engine.Texture"), rune::objects::packageObject(rune::Name("Transient")), rune::Name(), 0));
    REQUIRE(texture != nullptr);
    rune::bitmap::USize(texture) = 128;
    rune::bitmap::VSize(texture) = 128;
    rune::TextureMip mip;
    mip.width = 128;
    mip.height = 128;
    mip.data.assign(128 * 128, 0);
    texture->mips.push_back(mip);
    rune::shadow::Projection projection;
    projection.origin = { 0, 0, 200 };
    projection.xAxis = { 1, 0, 0 };
    projection.yAxis = { 0, 1, 0 };
    projection.zAxis = { 0, 0, -1 };
    rune::shadow::clear(texture);
    rune::Vector reversed[3] = { { -10, -10, 100 }, { -10, 10, 100 }, { 10, -10, 100 } };
    rune::shadow::drawTriangle(texture, projection, reversed);
    const std::vector<uint8_t>& texels = texture->mips[0].data;
    CHECK(std::count(texels.begin(), texels.end(), 128) == 128 * 128);
    rune::Vector triangle[3] = { { -10, -10, 100 }, { 10, -10, 100 }, { -10, 10, 100 } };
    rune::shadow::drawTriangle(texture, projection, triangle);
    CHECK(texels[60 * 128 + 53] == 128);
    CHECK(texels[60 * 128 + 54] == 83);
    CHECK(texels[60 * 128 + 66] == 83);
    CHECK(texels[60 * 128 + 67] == 128);
    CHECK(texels[53 * 128 + 60] == 128);
    CHECK(texels[72 * 128 + 54] == 74);
    CHECK(texels[72 * 128 + 55] == 128);
    CHECK(texels[73 * 128 + 54] == 128);
}

TEST_CASE("an actor's shadow vector eases toward its lights and keeps a length of 130", "[data]")
{
    auto world = openMap("RagnarVillage2");
    rune::UObject* player = world->player();
    REQUIRE(player != nullptr);
    rune::Vector location = rune::actor::Location(player);
    rune::actor::ShadowVector(player) = {};
    rune::PreparedLight light;
    light.location = location + rune::Vector { 100, 0, 0 };
    light.radius = 500;
    rune::shadow::updateVector(player, { light });
    rune::Vector vector = rune::actor::ShadowVector(player);
    CHECK(vector.x == Catch::Approx(-128.60f).margin(0.02f));
    CHECK(vector.y == Catch::Approx(0.0f).margin(0.001f));
    CHECK(vector.z == Catch::Approx(-19.02f).margin(0.02f));
    rune::actor::ShadowVector(player) = {};
    rune::shadow::updateVector(player, {});
    CHECK(rune::actor::ShadowVector(player).x == Catch::Approx(0.0f).margin(0.001f));
    CHECK(rune::actor::ShadowVector(player).z == Catch::Approx(-130.0f).margin(0.01f));
    closeMap(world, "RagnarVillage2");
}

TEST_CASE("with PlayerShadows 2 the player's projected shadow lands on the floor under him", "[data]")
{
    auto world = openMap("RagnarVillage2");
    for (int i = 0; i < 5; i++) {
        world->tick(0.05f);
    }
    rune::UObject* player = world->player();
    REQUIRE(player != nullptr);
    rune::Vector location = rune::actor::Location(player);
    rune::Vector eye = location + rune::Vector { 0, -200, 60 };
    for (int setting = 0; setting < 3; setting += 2) {
        rune::SceneRenderer renderer(std::make_unique<rune::SoftRenderDevice>());
        rune::RenderOptions options;
        options.playerShadows = setting;
        renderer.setOptions(options);
        renderer.beginFrame(160, 120);
        renderer.drawScene(world->level, world->actors, eye, rune::vectorRotation(location - eye), 90, nullptr);
        renderer.beginFrame(160, 120);
        renderer.drawScene(world->level, world->actors, eye, rune::vectorRotation(location - eye), 90, nullptr);
        rune::UTexture* texture = rune::cast<rune::UTexture>(rune::actor::ShadowTexture(player));
        if (setting == 0) {
            CHECK(texture == nullptr);
            continue;
        }
        REQUIRE(texture != nullptr);
        REQUIRE(texture->mips.size() == 1);
        const std::vector<uint8_t>& texels = texture->mips[0].data;
        CHECK(std::count_if(texels.begin(), texels.end(), [](uint8_t v) { return v < 128; }) > 200);
        rune::CheckResult floor;
        REQUIRE(world->lineCheck(floor, location, location - rune::Vector { 0, 0, 200 }, {}, nullptr, rune::TRACE_World));
        std::vector<rune::shadow::Polygon> polygons;
        rune::shadow::receivers(*world->level->model, rune::shadow::projection(player, texture), eye, polygons);
        bool under = false;
        for (const rune::shadow::Polygon& polygon : polygons) {
            for (const rune::Vector& point : polygon.points) {
                if (std::abs(point.z - (floor.location.z + 2.0f)) < 1.0f) {
                    under = true;
                }
            }
        }
        CHECK(under);
    }
    closeMap(world, "RagnarVillage2");
}

TEST_CASE("HasAnim looks only at the mesh while the skeletal sequence still exists", "[data]")
{
    auto world = openMap("RagnarVillage2");
    rune::UObject* player = world->player();
    REQUIRE(player != nullptr);
    REQUIRE(rune::actor::Mesh(player) == nullptr);
    rune::objects::ensureLoaded(rune::actor::Skeletal(player));
    rune::USkelModel* model = rune::cast<rune::USkelModel>(rune::actor::Skeletal(player));
    REQUIRE(model != nullptr);
    REQUIRE_FALSE(model->sequences.empty());
    rune::Name sequence = model->sequences.front().info.name;
    rune::vm::Call has(player, rune::Name("HasAnim"));
    REQUIRE(has.valid());
    has.n(sequence).run();
    CHECK(*reinterpret_cast<uint32_t*>(has.returnValue()) == 0);
    rune::vm::Call play(player, rune::Name("PlayAnim"));
    REQUIRE(play.valid());
    play.n(sequence).run();
    CHECK(rune::actor::AnimSequence(player) == sequence);
    closeMap(world, "RagnarVillage2");
}

TEST_CASE("TraceTexture returns the coplanar surface under the hit point and its auto pan scroll", "[data]")
{
    auto world = openMap("RagnarVillage2");
    rune::UObject* player = world->player();
    REQUIRE(player != nullptr);
    const rune::UModel& model = *world->level->model;
    auto centroid = [&](const rune::BspNode& node) {
        rune::Vector sum;
        for (int32_t i = 0; i < node.numVertices; i++) {
            sum = sum + model.points[static_cast<size_t>(model.verts[static_cast<size_t>(node.vertPool + i)].vertex)];
        }
        return sum * (1.0f / static_cast<float>(node.numVertices));
    };
    auto trace = [&](const rune::Vector& point, const rune::Vector& normal, int32_t& flags, rune::Vector& scroll) {
        rune::vm::Call call(player, rune::Name("TraceTexture"));
        REQUIRE(call.valid());
        call.v(point - normal * 8.0f).v(point + normal * 8.0f);
        int32_t* flagSlot = reinterpret_cast<int32_t*>(call.parameter(2));
        rune::Vector* scrollSlot = reinterpret_cast<rune::Vector*>(call.parameter(3));
        call.skip().skip().run();
        flags = *flagSlot;
        scroll = *scrollSlot;
        return *reinterpret_cast<rune::UObject**>(call.returnValue());
    };
    int checked = 0;
    for (size_t i = 0; i < model.nodes.size() && checked < 20; i++) {
        const rune::BspNode& first = model.nodes[i];
        if (first.iPlane < 0 || first.numVertices < 3) {
            continue;
        }
        const rune::BspNode& second = model.nodes[static_cast<size_t>(first.iPlane)];
        const rune::BspSurf& firstSurf = model.surfs[static_cast<size_t>(first.surf)];
        const rune::BspSurf& secondSurf = model.surfs[static_cast<size_t>(second.surf)];
        if (second.numVertices < 3 || firstSurf.texture == secondSurf.texture || !firstSurf.texture || !secondSurf.texture) {
            continue;
        }
        if ((firstSurf.polyFlags | secondSurf.polyFlags) & (rune::bsp::PF_NotSolid | rune::bsp::PF_Semisolid | rune::bsp::PF_Invisible)) {
            continue;
        }
        rune::Vector normal = model.vectors[static_cast<size_t>(secondSurf.vNormal)];
        int32_t flags = 0;
        rune::Vector scroll;
        rune::UObject* texture = trace(centroid(second), normal, flags, scroll);
        if (!texture) {
            continue;
        }
        CHECK(texture == secondSurf.texture);
        CHECK(static_cast<uint32_t>(flags) == secondSurf.polyFlags);
        checked++;
    }
    CHECK(checked >= 5);
    rune::UObject* zone = rune::zoneOf(player);
    REQUIRE(zone != nullptr);
    rune::zoneinfo::TexUPanSpeed(zone) = 2.0f;
    rune::zoneinfo::TexVPanSpeed(zone) = 3.0f;
    bool panned = false;
    for (size_t i = 0; i < model.nodes.size() && !panned; i++) {
        const rune::BspNode& node = model.nodes[i];
        const rune::BspSurf& surf = model.surfs[static_cast<size_t>(node.surf)];
        if (node.numVertices < 3 || !(surf.polyFlags & (rune::bsp::PF_AutoUPan | rune::bsp::PF_AutoVPan)) || (surf.polyFlags & (rune::bsp::PF_NotSolid | rune::bsp::PF_Semisolid))) {
            continue;
        }
        int32_t flags = 0;
        rune::Vector scroll;
        rune::Vector normal = model.vectors[static_cast<size_t>(surf.vNormal)];
        if (trace(centroid(node), normal, flags, scroll) != surf.texture) {
            continue;
        }
        REQUIRE(i == 5854);
        CHECK(scroll.x == Catch::Approx(2.0f));
        CHECK(scroll.y == Catch::Approx(0.0f));
        CHECK(scroll.z == Catch::Approx(0.0f));
        CHECK(scroll.sizeSquared() > 0.0f);
        panned = true;
    }
    CHECK(panned);
    closeMap(world, "RagnarVillage2");
}

namespace {
    struct SpriteView {
        std::unique_ptr<rune::World> world;
        rune::UObject* sprite = nullptr;
        rune::Vector location;
        rune::Rotator rotation;
        float fov = 90;

        std::string map;

        explicit SpriteView(const std::string& name = "RagnarVillage2")
            : map(name)
        {
            world = openMap(map);
            world->tick(1.0f / 30.0f);
            REQUIRE(world->calcView(location, rotation, fov));
            rune::Linker* linker = rune::objects::loadPackage(rune::Name("Palettes"));
            REQUIRE(linker != nullptr);
            rune::UTexture* texture = rune::cast<rune::UTexture>(linker->object(linker->findExport(rune::Name("Crayon"), rune::Name("Texture"), "Misc")));
            REQUIRE(texture != nullptr);
            rune::objects::ensureLoaded(texture);
            rune::UClass* cls = rune::objects::findClass("Engine.Trigger");
            rune::objects::flushLoads();
            rune::Vector forward = rune::rotationAxes(rotation).xAxis;
            sprite = world->spawn(cls, nullptr, rune::Name(), location + forward * 60.0f, {}, nullptr, true);
            REQUIRE(sprite != nullptr);
            rune::actor::bCollideActors(sprite) = 0;
            rune::actor::Location(sprite) = location + forward * 60.0f;
            rune::actor::bHidden(sprite) = 0;
            rune::actor::DrawType(sprite) = 1;
            rune::actor::Texture(sprite) = texture;
            rune::actor::DrawScale(sprite) = 4.0f;
            rune::actor::bUnlit(sprite) = 1;
            rune::actor::ScaleGlow(sprite) = 1.0f;
        }

        ~SpriteView()
        {
            closeMap(world, map);
        }

        std::vector<uint8_t> frame()
        {
            rune::SceneRenderer renderer(std::make_unique<rune::SoftRenderDevice>());
            renderer.setTime(world->timeSeconds);
            for (int i = 0; i < 2; i++) {
                renderer.beginFrame(64, 48);
                renderer.drawScene(world->level, world->actors, location, rotation, fov, world->player());
            }
            std::vector<uint8_t> rgba;
            int width = 0;
            int height = 0;
            renderer.readPixels(rgba, width, height);
            return rgba;
        }

        std::vector<int> centre(int frames = 2)
        {
            rune::SceneRenderer renderer(std::make_unique<rune::SoftRenderDevice>());
            renderer.setTime(world->timeSeconds);
            for (int i = 0; i < frames; i++) {
                renderer.beginFrame(64, 48);
                renderer.drawScene(world->level, world->actors, location, rotation, fov, world->player());
            }
            std::vector<uint8_t> rgba;
            int width = 0;
            int height = 0;
            renderer.readPixels(rgba, width, height);
            size_t at = (static_cast<size_t>(height / 2) * static_cast<size_t>(width) + static_cast<size_t>(width / 2)) * 4;
            return { rgba[at], rgba[at + 1], rgba[at + 2] };
        }
    };
}

TEST_CASE("the software renderer draws STY_AlphaBlend sprites only from half alpha, like SoftDrv", "[data]")
{
    SpriteView view;
    rune::actor::bHidden(view.sprite) = 1;
    std::vector<int> background = view.centre();
    rune::actor::bHidden(view.sprite) = 0;
    rune::actor::Style(view.sprite) = 1;
    std::vector<int> opaque = view.centre();
    REQUIRE(opaque != background);
    rune::actor::Style(view.sprite) = 5;
    rune::actor::AlphaScale(view.sprite) = 0.4f;
    CHECK(view.centre() == background);
    rune::actor::AlphaScale(view.sprite) = 0.6f;
    CHECK(view.centre() == opaque);
}

TEST_CASE("actors beyond VisibilityRadius or VisibilityHeight from the camera are not drawn", "[data]")
{
    SpriteView view;
    rune::actor::bHidden(view.sprite) = 1;
    std::vector<int> background = view.centre();
    rune::actor::bHidden(view.sprite) = 0;
    rune::actor::Location(view.sprite) = rune::actor::Location(view.sprite) + rune::Vector { 0, 0, 5 };
    std::vector<int> shown = view.centre();
    REQUIRE(shown != background);
    rune::Vector offset = rune::actor::Location(view.sprite) - view.location;
    float flat = std::sqrt(offset.x * offset.x + offset.y * offset.y);
    rune::actor::VisibilityRadius(view.sprite) = flat - 1.0f;
    CHECK(view.centre() == background);
    rune::actor::VisibilityRadius(view.sprite) = flat + 1.0f;
    CHECK(view.centre() == shown);
    rune::actor::VisibilityRadius(view.sprite) = 0;
    rune::actor::VisibilityHeight(view.sprite) = std::fabs(offset.z) - 0.5f;
    CHECK(view.centre() == background);
    rune::actor::VisibilityHeight(view.sprite) = std::fabs(offset.z) + 0.5f;
    CHECK(view.centre() == shown);
}

TEST_CASE("bOwnerNoSee hides actors owned through the owner chain by the first person viewer", "[data]")
{
    SpriteView view;
    rune::actor::bHidden(view.sprite) = 1;
    std::vector<int> background = view.centre();
    rune::actor::bHidden(view.sprite) = 0;
    std::vector<int> shown = view.centre();
    REQUIRE(shown != background);
    rune::UObject* player = view.world->player();
    rune::UClass* cls = rune::objects::findClass("Engine.Trigger");
    rune::UObject* middle = view.world->spawn(cls, nullptr, rune::Name(), view.location, {}, nullptr, true);
    REQUIRE(middle != nullptr);
    rune::actor::Owner(middle) = player;
    rune::actor::Owner(view.sprite) = middle;
    rune::actor::bOwnerNoSee(view.sprite) = 1;
    rune::pawn::bBehindView(player) = 1;
    CHECK(view.centre() == shown);
    rune::pawn::bBehindView(player) = 0;
    CHECK(view.centre() == background);
}

TEST_CASE("actors farther than 1.25 times the deepest world point of the previous frame are culled", "[data]")
{
    SpriteView view;
    rune::actor::bHidden(view.sprite) = 1;
    std::vector<int> background = view.centre();
    rune::actor::bHidden(view.sprite) = 0;
    std::vector<int> shown = view.centre();
    REQUIRE(shown != background);
    CHECK(view.centre(1) == background);
    rune::actor::SpriteProjForward(view.sprite) = 70.0f;
    CHECK(view.centre(1) == shown);
}

TEST_CASE("sprites are lit like Render.dll: unlit ones by ScaleGlow alone, lit ones at most three quarters bright", "[data]")
{
    SpriteView view;
    std::vector<int> unlit = view.centre();
    REQUIRE(unlit[0] == 69);
    rune::actor::ScaleGlow(view.sprite) = 0.5f;
    std::vector<int> halved = view.centre();
    CHECK(halved[0] >= 34);
    CHECK(halved[0] <= 35);
    rune::actor::ScaleGlow(view.sprite) = 1.0f;
    rune::actor::bUnlit(view.sprite) = 0;
    std::vector<int> lit = view.centre();
    for (int channel = 0; channel < 3; channel++) {
        CHECK(lit[static_cast<size_t>(channel)] <= 52);
    }
}

TEST_CASE("sprites are depth tested SpriteProjForward units nearer the camera than their location", "[data]")
{
    SpriteView view;
    rune::Vector forward = rune::rotationAxes(view.rotation).xAxis;
    rune::actor::bHidden(view.sprite) = 1;
    std::vector<int> background = view.centre();
    rune::actor::bHidden(view.sprite) = 0;
    std::vector<int> shown = view.centre();
    REQUIRE(shown != background);
    rune::UClass* cls = rune::objects::findClass("Engine.Trigger");
    rune::UObject* wall = view.world->spawn(cls, nullptr, rune::Name(), view.location, {}, nullptr, true);
    REQUIRE(wall != nullptr);
    rune::actor::bCollideActors(wall) = 0;
    rune::actor::Location(wall) = view.location + forward * 50.0f;
    rune::actor::bHidden(wall) = 0;
    rune::actor::DrawType(wall) = 1;
    rune::actor::Texture(wall) = rune::actor::Texture(view.sprite);
    rune::actor::DrawScale(wall) = 4.0f;
    rune::actor::bUnlit(wall) = 1;
    rune::actor::ScaleGlow(wall) = 0.5f;
    rune::actor::SpriteProjForward(wall) = 0.0f;
    rune::actor::SpriteProjForward(view.sprite) = 0.0f;
    std::vector<int> behind = view.centre();
    CHECK(behind[0] < shown[0]);
    rune::actor::SpriteProjForward(view.sprite) = 20.0f;
    CHECK(view.centre() == shown);
}

TEST_CASE("unlit skeletal meshes blend ColorAdjust at six tenths into the glow", "[data]")
{
    SpriteView view;
    rune::actor::bHidden(view.sprite) = 1;
    rune::UClass* cls = rune::objects::findClass("RuneI.Bucket");
    rune::objects::flushLoads();
    REQUIRE(cls != nullptr);
    rune::Vector forward = rune::rotationAxes(view.rotation).xAxis;
    rune::UObject* bucket = view.world->spawn(cls, nullptr, rune::Name(), view.location + forward * 40.0f, {}, nullptr, true);
    REQUIRE(bucket != nullptr);
    rune::actor::Location(bucket) = view.location + forward * 40.0f;
    rune::actor::Physics(bucket) = rune::PHYS_None;
    rune::actor::bUnlit(bucket) = 1;
    rune::actor::ScaleGlow(bucket) = 2.0f;
    rune::actor::AmbientGlow(bucket) = 0;
    rune::actor::DrawScale(bucket) = 3.0f;
    rune::actor::ColorAdjust(bucket) = {};
    std::vector<int> plain = view.centre();
    REQUIRE(plain[1] > 20);
    rune::actor::ColorAdjust(bucket) = { 255, 0, 0 };
    std::vector<int> adjusted = view.centre();
    CHECK(std::abs(adjusted[1] * 5 - plain[1] * 2) <= 10);
    CHECK(std::abs(adjusted[2] * 5 - plain[2] * 2) <= 10);
    CHECK(std::abs(adjusted[0] - plain[0]) <= 1);
}

TEST_CASE("Fatness pushes mesh vertices along their normals by Fatness over 16 minus 8", "[data]")
{
    SpriteView view;
    rune::actor::bHidden(view.sprite) = 1;
    std::vector<uint8_t> background = view.frame();
    rune::UClass* cls = rune::objects::findClass("RuneI.Bucket");
    rune::objects::flushLoads();
    REQUIRE(cls != nullptr);
    rune::Vector forward = rune::rotationAxes(view.rotation).xAxis;
    rune::UObject* bucket = view.world->spawn(cls, nullptr, rune::Name(), view.location + forward * 60.0f, {}, nullptr, true);
    REQUIRE(bucket != nullptr);
    rune::actor::Location(bucket) = view.location + forward * 60.0f;
    rune::actor::Physics(bucket) = rune::PHYS_None;
    auto covered = [&]() {
        std::vector<uint8_t> pixels = view.frame();
        int count = 0;
        for (size_t i = 0; i + 3 < pixels.size(); i += 4) {
            if (pixels[i] != background[i] || pixels[i + 1] != background[i + 1] || pixels[i + 2] != background[i + 2]) {
                count++;
            }
        }
        return count;
    };
    rune::actor::Fatness(bucket) = 128;
    int normal = covered();
    REQUIRE(normal > 0);
    rune::actor::Fatness(bucket) = 200;
    CHECK(covered() > normal);
}

TEST_CASE("skeletal meshes are never environment mapped", "[data]")
{
    SpriteView view;
    rune::actor::bHidden(view.sprite) = 1;
    rune::UClass* cls = rune::objects::findClass("RuneI.Bucket");
    rune::objects::flushLoads();
    REQUIRE(cls != nullptr);
    rune::Vector forward = rune::rotationAxes(view.rotation).xAxis;
    rune::UObject* bucket = view.world->spawn(cls, nullptr, rune::Name(), view.location + forward * 40.0f, {}, nullptr, true);
    REQUIRE(bucket != nullptr);
    rune::actor::Location(bucket) = view.location + forward * 40.0f;
    rune::actor::Physics(bucket) = rune::PHYS_None;
    rune::actor::DrawScale(bucket) = 3.0f;
    std::vector<uint8_t> plain = view.frame();
    for (int group = 0; group < 16; group++) {
        rune::actor::SkelGroupFlags(bucket, group) |= 0x10;
    }
    rune::actor::bMeshEnviroMap(bucket) = 1;
    CHECK(view.frame() == plain);
}

TEST_CASE("actors stay drawn while their zone was rendered within the last two seconds", "[data]")
{
    SpriteView view("DwarfMap2");
    rune::actor::bHidden(view.sprite) = 1;
    std::vector<int> background = view.centre();
    rune::actor::bHidden(view.sprite) = 0;
    std::vector<int> shown = view.centre();
    REQUIRE(shown != background);
    rune::UModel* model = view.world->level->model;
    REQUIRE(model != nullptr);
    float now = rune::levelinfo::TimeSeconds(view.world->levelInfo);
    uint8_t* zoneNumber = rune::actor::Region(view.sprite) + sizeof(rune::UObject*) + sizeof(int32_t);
    uint8_t original = *zoneNumber;
    int hiddenZone = -1;
    for (size_t zone = 1; zone < model->zones.size() && hiddenZone < 0; zone++) {
        *zoneNumber = static_cast<uint8_t>(zone);
        model->zones[zone].lastRenderTime = now - 1000.0f;
        if (view.centre() == background) {
            hiddenZone = static_cast<int>(zone);
        }
    }
    REQUIRE(hiddenZone > 0);
    REQUIRE(static_cast<uint8_t>(hiddenZone) != original);
    model->zones[static_cast<size_t>(hiddenZone)].lastRenderTime = now - 1.0f;
    CHECK(view.centre() == shown);
    model->zones[static_cast<size_t>(hiddenZone)].lastRenderTime = now - 2.5f;
    CHECK(view.centre() == background);
}

TEST_CASE("DT_VerticalSprite draws a two sided quad in the actor's rotated Y and Z plane", "[data]")
{
    SpriteView view;
    rune::actor::bHidden(view.sprite) = 1;
    std::vector<int> background = view.centre();
    rune::actor::bHidden(view.sprite) = 0;
    rune::actor::DrawType(view.sprite) = 5;
    rune::actor::Rotation(view.sprite) = view.rotation;
    std::vector<int> facing = view.centre();
    CHECK(facing != background);
    rune::actor::Rotation(view.sprite) = { view.rotation.pitch, view.rotation.yaw + 32768, view.rotation.roll };
    CHECK(view.centre() == facing);
    rune::actor::Rotation(view.sprite) = { view.rotation.pitch, view.rotation.yaw + 16384, view.rotation.roll };
    CHECK(view.centre() == background);
}

TEST_CASE("carried items are drawn only through their visible parent's joints", "[data]")
{
    SpriteView view;
    rune::actor::bHidden(view.sprite) = 1;
    std::vector<int> background = view.centre();
    rune::actor::bHidden(view.sprite) = 0;
    std::vector<int> shown = view.centre();
    REQUIRE(shown != background);
    rune::UClass* cls = rune::objects::findClass("RuneI.Bucket");
    rune::objects::flushLoads();
    REQUIRE(cls != nullptr);
    rune::Coords axes = rune::rotationAxes(view.rotation);
    rune::Vector spot = view.location + axes.xAxis * 200.0f + axes.yAxis * 60.0f;
    rune::UObject* bucket = view.world->spawn(cls, nullptr, rune::Name(), spot, {}, nullptr, true);
    REQUIRE(bucket != nullptr);
    rune::actor::Location(bucket) = spot;
    rune::actor::Physics(bucket) = rune::PHYS_None;
    rune::actor::JointChild(bucket, 0) = view.sprite;
    rune::actor::bCarriedItem(view.sprite) = 1;
    CHECK(view.centre() == shown);
    rune::actor::bHidden(bucket) = 1;
    CHECK(view.centre() == background);
}

TEST_CASE("the software renderer draws wireframe actors as lines in the Render.dll wire colour", "[data]")
{
    SpriteView view;
    rune::actor::bHidden(view.sprite) = 1;
    rune::UClass* cls = rune::objects::findClass("RuneI.Bucket");
    rune::objects::flushLoads();
    REQUIRE(cls != nullptr);
    rune::Vector forward = rune::rotationAxes(view.rotation).xAxis;
    rune::UObject* bucket = view.world->spawn(cls, nullptr, rune::Name(), view.location + forward * 60.0f, {}, nullptr, true);
    REQUIRE(bucket != nullptr);
    rune::actor::Location(bucket) = view.location + forward * 60.0f;
    rune::actor::DrawScale(bucket) = 3.0f;
    auto wired = [&](bool wireframe) {
        rune::SceneRenderer renderer(std::make_unique<rune::SoftRenderDevice>());
        renderer.beginFrame(64, 48);
        renderer.drawActorOverlay(bucket, view.location, view.rotation, view.fov, wireframe, true, 0, 0, 0, 0);
        std::vector<uint8_t> rgba;
        int width = 0;
        int height = 0;
        renderer.readPixels(rgba, width, height);
        int count = 0;
        for (size_t i = 0; i + 3 < rgba.size(); i += 4) {
            if (std::abs(rgba[i] - 153) <= 1 && std::abs(rgba[i + 1] - 102) <= 1 && std::abs(rgba[i + 2] - 25) <= 1) {
                count++;
            }
        }
        return count;
    };
    CHECK(wired(false) == 0);
    CHECK(wired(true) > 20);
}

TEST_CASE("Coliseum upper trim sheets cover their coplanar wall", "[data]")
{
    auto world = openMap("DM-Coliseum");
    auto* model = world->level->model;
    REQUIRE(model != nullptr);
    REQUIRE(model->nodes.size() > 76);
    REQUIRE(model->nodes[76].surf == 54);
    REQUIRE(model->surfs[54].polyFlags == 8);
    auto render = [&](const rune::Vector& eye) {
        rune::SceneRenderer renderer(std::make_unique<rune::SoftRenderDevice>());
        auto rotation = rune::vectorRotation(rune::Vector { 0, 512, 640 } - eye);
        renderer.beginFrame(160, 120);
        renderer.drawScene(world->level, world->actors, eye, rotation, 90, world->player());
        std::vector<uint8_t> rgba;
        int width = 0;
        int height = 0;
        renderer.readPixels(rgba, width, height);
        std::vector<uint8_t> trim;
        for (int y = 54; y < 66; y++) {
            for (int x = 70; x < 90; x++) {
                auto at = (y * width + x) * 4;
                trim.insert(trim.end(), rgba.begin() + at, rgba.begin() + at + 3);
            }
        }
        return trim;
    };
    for (const rune::Vector& eye : std::vector<rune::Vector> { { 0, 0, 80 }, { -100, 0, 80 }, { 100, 0, 80 }, { 0, 300, 640 } }) {
        auto full = render(eye);
        auto count = model->nodes[75].numVertices;
        model->nodes[75].numVertices = 0;
        auto sheet = render(eye);
        model->nodes[75].numVertices = count;
        INFO("eye: " << eye.x << ", " << eye.y << ", " << eye.z);
        CHECK(full == sheet);
    }
    closeMap(world, "DM-Coliseum");
}
