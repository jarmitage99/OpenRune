#include "core/objects.h"
#include "engine/actor.h"
#include "engine/map_writer.h"
#include "engine/resources.h"
#include "engine/world.h"

#include <catch_amalgamated.hpp>

#include <filesystem>
#include <string>

namespace {
    rune::ULevel* loadEditorLevel(rune::Linker* linker)
    {
        int32_t ref = linker->findExport(rune::Name("MyLevel"), rune::Name("Level"), "*");
        rune::ULevel* level = rune::cast<rune::ULevel>(linker->object(ref));
        rune::objects::ensureLoaded(level);
        rune::objects::flushLoads();
        for (rune::UObject* a : level->actors) {
            rune::objects::ensureLoaded(a);
        }
        rune::objects::flushLoads();
        rune::objects::ensureLoaded(level->model);
        rune::objects::flushLoads();
        return level;
    }

    rune::UObject* findActor(rune::ULevel* level, const char* name)
    {
        for (rune::UObject* a : level->actors) {
            if (a && a->name == rune::Name(name)) {
                return a;
            }
        }
        return nullptr;
    }

    size_t liveActors(rune::ULevel* level)
    {
        size_t count = 0;
        for (rune::UObject* a : level->actors) {
            if (a) {
                count++;
            }
        }
        return count;
    }

    uint32_t exportFlags(rune::Linker* linker, const char* name)
    {
        int32_t ref = linker->findExport(rune::Name(name), rune::Name(), "*");
        if (ref <= 0) {
            return 0;
        }
        return linker->file().exportAt(ref).flags;
    }

    struct ModelCounts {
        size_t nodes = 0;
        size_t surfs = 0;
        size_t points = 0;
        size_t verts = 0;
        size_t lightBits = 0;
        size_t leaves = 0;
        size_t leafHulls = 0;
        size_t reachSpecs = 0;
    };

    ModelCounts countsOf(rune::ULevel* level)
    {
        ModelCounts counts;
        counts.nodes = level->model->nodes.size();
        counts.surfs = level->model->surfs.size();
        counts.points = level->model->points.size();
        counts.verts = level->model->verts.size();
        counts.lightBits = level->model->lightBits.size();
        counts.leaves = level->model->leaves.size();
        counts.leafHulls = level->model->leafHulls.size();
        counts.reachSpecs = level->reachSpecs.size();
        return counts;
    }
}

TEST_CASE("saved map reloads with the same level and a moved actor", "[data]")
{
    rune::engine::initialize(RUNE_GAME_DATA);
    rune::objects::unloadPackage(rune::Name("DM-Hel"));
    rune::objects::setEditorMode(true);
    std::filesystem::path file = std::filesystem::temp_directory_path() / "rune-editor-map-test" / "EditedHel.run";
    std::filesystem::remove_all(file.parent_path());
    ModelCounts original;
    {
        rune::Linker* linker = rune::objects::loadPackage(rune::Name("DM-Hel"));
        REQUIRE(linker != nullptr);
        rune::ULevel* level = loadEditorLevel(linker);
        REQUIRE(level->model != nullptr);
        original = countsOf(level);
        CHECK(level->actors.size() == 227);
        CHECK(liveActors(level) == 179);
        CHECK(original.nodes == 356);
        CHECK(original.reachSpecs == 61);
        rune::UObject* start = findActor(level, "PlayerStart1");
        REQUIRE(start != nullptr);
        rune::actor::Location(start) = { -2266.0f, 8294.0f, -256.0f };
        std::string error;
        REQUIRE(rune::mapwriter::saveMap(level, file, &error));
        CHECK(error.empty());
    }
    size_t mismatchesBefore = rune::objects::loadMismatches().size();
    rune::Linker* saved = rune::objects::loadPackageFile(rune::Name("EditedHel"), file, false);
    REQUIRE(saved != nullptr);
    rune::ULevel* level = loadEditorLevel(saved);
    REQUIRE(level != nullptr);
    CHECK(level->actors.size() == 179);
    CHECK(liveActors(level) == 179);
    REQUIRE(level->model != nullptr);
    ModelCounts reloaded = countsOf(level);
    CHECK(reloaded.nodes == original.nodes);
    CHECK(reloaded.surfs == original.surfs);
    CHECK(reloaded.points == original.points);
    CHECK(reloaded.verts == original.verts);
    CHECK(reloaded.lightBits == original.lightBits);
    CHECK(reloaded.leaves == original.leaves);
    CHECK(reloaded.leafHulls == original.leafHulls);
    CHECK(reloaded.reachSpecs == original.reachSpecs);
    rune::UObject* start = findActor(level, "PlayerStart1");
    REQUIRE(start != nullptr);
    CHECK(start->outermost()->name == rune::Name("EditedHel"));
    CHECK(rune::actor::Location(start).x == -2266.0f);
    CHECK(rune::actor::Location(start).y == 8294.0f);
    CHECK(rune::actor::Location(start).z == -256.0f);
    CHECK(level->actors[0]->cls->name == rune::Name("LevelInfo"));
    CHECK((exportFlags(saved, "Brush214") & (rune::RF_NotForClient | rune::RF_NotForServer)) == (rune::RF_NotForClient | rune::RF_NotForServer));
    CHECK((exportFlags(saved, "PlayerStart1") & (rune::RF_NotForClient | rune::RF_NotForServer)) == 0);
    for (rune::UObject* a : level->actors) {
        rune::objects::ensureLoaded(rune::actor::Brush(a));
    }
    rune::objects::flushLoads();
    const std::vector<std::string>& mismatches = rune::objects::loadMismatches();
    std::string first = mismatches.size() > mismatchesBefore ? mismatches[mismatchesBefore] : std::string();
    INFO("first mismatch: " << first);
    CHECK(mismatches.size() == mismatchesBefore);
    rune::objects::unloadPackage(rune::Name("EditedHel"));
    rune::objects::unloadPackage(rune::Name("DM-Hel"));
    rune::objects::setEditorMode(false);
}
