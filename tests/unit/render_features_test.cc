#include "core/objects.h"
#include "core/vm.h"
#include "engine/decal.h"
#include "engine/mesh.h"
#include "engine/props_engine.h"
#include "engine/resources.h"
#include "engine/world.h"
#include "render/lighting.h"
#include "render/scene_renderer.h"

#include <catch_amalgamated.hpp>

TEST_CASE("volumetric fog integrates the light sphere along the view ray")
{
    rune::VolumetricLight light = rune::prepareVolumetric({ 0, 0, 100 }, 3, 1.0f, 8);
    CHECK(light.radius == 100.0f);
    CHECK(light.brightness == 0.125f);
    CHECK_FALSE(light.inside);
    CHECK(rune::volumetricFog(light, { 0, 0, 300 }) == Catch::Approx(0.5f).margin(0.002));
    CHECK(rune::volumetricFog(light, { 0, 0, 100 }) == Catch::Approx(0.25f).margin(0.002));
    CHECK(rune::volumetricFog(light, { 300, 0, 0 }) == 0.0f);
    CHECK(rune::volumetricFog(light, { 0, 0, -50 }) == 0.0f);
    rune::VolumetricLight bright = rune::prepareVolumetric({ 0, 0, 100 }, 3, 1.0f, 64);
    CHECK(rune::volumetricFog(bright, { 0, 0, 300 }) == 1.0f);
}

TEST_CASE("fog maps of several volumetric lights merge like a screen blend in 7 bits")
{
    CHECK(rune::fogMerge(100, 50) == 111);
    CHECK(rune::fogMerge(0, 64) == 64);
    CHECK(rune::fogMerge(127, 127) == 127);
}

TEST_CASE("version 61 texture packages load names and mip data", "[data]")
{
    rune::engine::initialize(RUNE_GAME_DATA);
    rune::Linker* linker = rune::objects::loadPackage(rune::Name("Palettes"));
    REQUIRE(linker != nullptr);
    CHECK(linker->file().version() == 61);
    int32_t ref = linker->findExport(rune::Name("Crayon"), rune::Name("Texture"), "Misc");
    REQUIRE(ref != 0);
    rune::UTexture* texture = rune::cast<rune::UTexture>(linker->object(ref));
    REQUIRE(texture != nullptr);
    rune::objects::ensureLoaded(texture);
    REQUIRE(texture->mips.size() == 1);
    CHECK(texture->mips[0].width == 64);
    CHECK(texture->mips[0].height == 64);
    CHECK(texture->mips[0].data.size() == 4096);
    CHECK(texture->mips[0].data[65] == 2);
    std::vector<uint8_t> rgba = texture->rgba(0);
    REQUIRE(rgba.size() == 16384);
    CHECK(rgba[0] == 0x45);
    CHECK(rgba[3] == 255);
    CHECK(rgba[65 * 4] == 0x47);
}

namespace {
    std::string pathOf(rune::UObject* object)
    {
        return object ? object->pathName() : "None";
    }

    rune::UObject* actorNamed(rune::World& world, const char* name)
    {
        for (rune::UObject* actor : world.actors) {
            if (actor && !actor->destroyed && actor->name == rune::Name(name)) {
                return actor;
            }
        }
        return nullptr;
    }
}

TEST_CASE("environment map coordinates come from the world space reflection vector")
{
    float u = -1;
    float v = -1;
    rune::environmentCoords({ 10, 0, 0 }, { -1, 0, 0 }, u, v);
    CHECK(u == Catch::Approx(0.0f).margin(1e-5));
    CHECK(v == Catch::Approx(0.5f).margin(1e-5));
    rune::environmentCoords({ 0, 0, -5 }, { 0, 0, 1 }, u, v);
    CHECK(u == Catch::Approx(0.5f).margin(1e-5));
    CHECK(v == Catch::Approx(0.5f).margin(1e-5));
    rune::environmentCoords({ 3, 3, 0 }, { 0, -1, 0 }, u, v);
    CHECK(u == Catch::Approx(0.853553f).margin(1e-5));
    CHECK(v == Catch::Approx(0.146447f).margin(1e-5));
}

TEST_CASE("the sky frame comes from the viewer zone's linked SkyZone, not the first sky actor", "[data]")
{
    rune::engine::initialize(RUNE_GAME_DATA);
    rune::objects::unloadPackage(rune::Name("DM-IceCavern"));
    {
        rune::World world;
        REQUIRE(world.loadMap("DM-IceCavern"));
        world.tick(1.0f / 30.0f);
        rune::UObject* levelInfo = actorNamed(world, "LevelInfo0");
        rune::UObject* zone = actorNamed(world, "ZoneInfo0");
        rune::UObject* firstSky = nullptr;
        for (rune::UObject* actor : world.actors) {
            if (!firstSky && actor && !actor->destroyed && actor->isA(rune::Name("SkyZoneInfo"))) {
                firstSky = actor;
            }
        }
        REQUIRE(levelInfo != nullptr);
        REQUIRE(zone != nullptr);
        REQUIRE(firstSky != nullptr);
        CHECK(firstSky->name == rune::Name("SkyZoneInfo1"));
        CHECK(pathOf(rune::frameSkyZone(levelInfo)) == "DM-IceCavern.SkyZoneInfo0");
        CHECK(pathOf(rune::frameSkyZone(zone)) == "DM-IceCavern.SkyZoneInfo0");
        CHECK(rune::frameSkyZone(nullptr) == nullptr);
    }
    rune::objects::unloadPackage(rune::Name("DM-IceCavern"));
}

TEST_CASE("only DT_Sprite and DT_SpriteAnimOnce actors are drawn as sprites")
{
    CHECK(rune::drawnAsSprite(1));
    CHECK(rune::drawnAsSprite(7));
    CHECK_FALSE(rune::drawnAsSprite(0));
    CHECK_FALSE(rune::drawnAsSprite(2));
    CHECK_FALSE(rune::drawnAsSprite(3));
    CHECK_FALSE(rune::drawnAsSprite(4));
    CHECK_FALSE(rune::drawnAsSprite(5));
    CHECK_FALSE(rune::drawnAsSprite(6));
}

TEST_CASE("sprite anim once frames follow the original renderer", "[data]")
{
    rune::engine::initialize(RUNE_GAME_DATA);
    rune::objects::unloadPackage(rune::Name("DM-Hel"));
    {
        rune::World world;
        REQUIRE(world.loadMap("DM-Hel"));
        world.tick(1.0f / 30.0f);
        rune::UTexture* first = rune::cast<rune::UTexture>(rune::objects::loadObject("RuneFX3.haxeFa", nullptr));
        REQUIRE(first != nullptr);
        rune::UObject* player = world.player();
        REQUIRE(player != nullptr);
        rune::UObject* glob = world.spawn(rune::objects::findClass("RuneI.MudGlob"), nullptr, rune::Name(), rune::actor::Location(player), {}, nullptr, true);
        REQUIRE(glob != nullptr);
        REQUIRE(rune::actor::LifeSpan(glob->cls->defaultObject) == 4.0f);
        auto frameAt = [&](float lifeSpan) {
            rune::actor::LifeSpan(glob) = lifeSpan;
            return rune::spriteAnimOnceFrame(glob, first)->name.str();
        };
        CHECK(frameAt(4.0f) == "haxeFa");
        CHECK(frameAt(2.0f) == "haxeFc");
        CHECK(frameAt(1.56f) == "haxeFd");
        CHECK(frameAt(1.6f) == "haxeFc");
        CHECK(frameAt(0.0f) == "haxeFe");
        CHECK(frameAt(8.0f) == "haxeFa");
    }
    rune::objects::unloadPackage(rune::Name("DM-Hel"));
}

TEST_CASE("player shadows attach to the floor below the owner through the AttachDecal native", "[data]")
{
    rune::engine::initialize(RUNE_GAME_DATA);
    rune::objects::unloadPackage(rune::Name("DM-Hel"));
    {
        rune::World world;
        REQUIRE(world.loadMap("DM-Hel"));
        world.tick(1.0f / 30.0f);
        rune::UObject* player = world.player();
        REQUIRE(player != nullptr);
        rune::Vector feet = rune::actor::Location(player);
        rune::UObject* shadow = world.spawn(rune::objects::findClass("RuneI.PlayerShadow"), player, rune::Name(), feet, {}, nullptr, true);
        REQUIRE(shadow != nullptr);
        REQUIRE(rune::actor::DrawScale(shadow) == 1.5f);
        CHECK(world.decals.pieces.count(shadow) == 0);
        rune::vm::Call update(shadow, rune::Name("Update"));
        REQUIRE(update.valid());
        update.obj(nullptr).run();

        auto found = world.decals.pieces.find(shadow);
        REQUIRE(found != world.decals.pieces.end());
        REQUIRE_FALSE(found->second.empty());
        const rune::DecalPiece& piece = found->second[0];
        const rune::BspSurf& surf = world.level->model->surfs[static_cast<size_t>(piece.surf)];
        CHECK(world.level->model->vectors[static_cast<size_t>(surf.vNormal)].z == Catch::Approx(1.0f));
        float floorZ = world.level->model->points[static_cast<size_t>(surf.pBase)].z;
        float offset = rune::actor::CollisionRadius(player) * 0.1f;
        rune::Vector spot { feet.x + offset, feet.y + offset, floorZ };
        float radius = 33.9411255f;
        rune::Vector expected[4] = { { 0, radius, 0 }, { radius, 0, 0 }, { 0, -radius, 0 }, { -radius, 0, 0 } };
        for (int i = 0; i < 4; i++) {
            CHECK(piece.vertices[static_cast<size_t>(i)].x == Catch::Approx(spot.x + expected[i].x).margin(0.01));
            CHECK(piece.vertices[static_cast<size_t>(i)].y == Catch::Approx(spot.y + expected[i].y).margin(0.01));
            CHECK(piece.vertices[static_cast<size_t>(i)].z == Catch::Approx(floorZ).margin(0.01));
        }

        rune::CheckResult floor;
        REQUIRE(world.bspLineCheck(floor, spot + rune::Vector { 0, 0, 8 }, spot - rune::Vector { 0, 0, 8 }, {}));
        CHECK(rune::decalFindSurf(world.level->model, floor.item, spot) == piece.surf);
        std::vector<rune::Vector> outside { spot + rune::Vector { 100000, 0, 0 }, spot + rune::Vector { 100010, 0, 0 }, spot + rune::Vector { 100010, 10, 0 } };
        rune::clipDecalToNode(world.level->model, floor.item, outside);
        CHECK(outside.empty());

        rune::vm::Call detach(shadow, rune::Name("DetachDecal"));
        REQUIRE(detach.valid());
        detach.run();
        CHECK(world.decals.pieces.count(shadow) == 0);
    }
    rune::objects::unloadPackage(rune::Name("DM-Hel"));
}
