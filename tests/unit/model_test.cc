#include "core/objects.h"
#include "engine/resources.h"
#include "engine/world.h"

#include <catch_amalgamated.hpp>

#include <filesystem>

namespace {
    rune::ULevel* loadLevel(const std::string& map)
    {
        rune::engine::initialize(RUNE_GAME_DATA);
        rune::Linker* linker = rune::objects::loadPackage(rune::Name(map));
        if (!linker) {
            return nullptr;
        }
        int32_t ref = linker->findExport(rune::Name("MyLevel"), rune::Name("Level"), "");
        rune::ULevel* level = rune::cast<rune::ULevel>(linker->object(ref));
        rune::objects::ensureLoaded(level);
        return level;
    }
}

TEST_CASE("level actor list of Ragnar's village skips the editor-only builder brush", "[data]")
{
    rune::engine::initialize(RUNE_GAME_DATA);
    rune::objects::unloadPackage(rune::Name("RagnarVillage2"));
    rune::ULevel* level = loadLevel("RagnarVillage2");
    REQUIRE(level != nullptr);
    CHECK(level->actors.size() == 1759);
    REQUIRE(level->actors[0] != nullptr);
    CHECK(level->actors[0]->cls->name == rune::Name("LevelInfo"));
    CHECK(level->actors[1] == nullptr);
    REQUIRE(level->model != nullptr);
    CHECK(level->model->cls->name == rune::Name("Model"));
    rune::objects::unloadPackage(rune::Name("RagnarVillage2"));
}

TEST_CASE("every map loads without native serialization mismatches", "[data]")
{
    size_t maps = 0;
    for (const auto& entry : std::filesystem::directory_iterator(RUNE_GAME_DATA "/Maps")) {
        if (entry.path().extension() != ".run") {
            continue;
        }
        rune::ULevel* level = loadLevel(entry.path().stem().string());
        INFO(entry.path().filename().string());
        REQUIRE(level != nullptr);
        rune::objects::ensureLoaded(level->model);
        CHECK_FALSE(level->model->nodes.empty());
        rune::objects::unloadPackage(rune::Name(entry.path().stem().string()));
        maps++;
    }
    CHECK(maps == 94);
    const std::vector<std::string>& mismatches = rune::objects::loadMismatches();
    INFO("first mismatch: " << (mismatches.empty() ? std::string() : mismatches.front()) << " of " << mismatches.size());
    CHECK(mismatches.empty());
}
