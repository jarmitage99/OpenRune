#include "engine/package.h"
#include "core/objects.h"
#include "engine/world.h"

#include <catch_amalgamated.hpp>

#include <vector>

TEST_CASE("compact index decodes single and multi byte values")
{
    std::vector<uint8_t> bytes = { 0x05, 0x85, 0x40, 0x01, 0x7f, 0x7f, 0x40, 0x80, 0x02 };
    rune::Reader reader(bytes, 0);
    CHECK(reader.index() == 5);
    CHECK(reader.index() == -5);
    CHECK(reader.index() == 64);
    CHECK(reader.index() == 8191);
    CHECK(reader.index() == 16384);
    CHECK(reader.tell() == bytes.size());
}

TEST_CASE("reader throws when reading past the end")
{
    std::vector<uint8_t> bytes = { 0x01, 0x02 };
    rune::Reader reader(bytes, 0);
    CHECK_THROWS_AS(reader.u32(), std::runtime_error);
}

TEST_CASE("package header and tables of Engine.u", "[data]")
{
    rune::Package package(RUNE_GAME_DATA "/System/Engine.u");
    CHECK(package.version() == 69);
    CHECK(package.licensee() == 0);
    CHECK(package.names().size() == 4616);
    CHECK(package.exports().size() == 7938);
    CHECK(package.imports().size() == 64);
}

TEST_CASE("tagged properties of a texture", "[data]")
{
    rune::Package package(RUNE_GAME_DATA "/Textures/Brckston.utx");
    int32_t ref = package.findExport("Texture", "BRCK6_c");
    REQUIRE(ref != 0);
    CHECK(package.groupOf(ref) == "");

    rune::Object object = package.load(ref);
    CHECK(object.objectValue("Palette") == 20);
    CHECK(object.byteValue("TextureMaterial") == 3);
    CHECK(object.byteValue("UBits") == 7);
    CHECK(object.byteValue("VBits") == 7);
    CHECK(object.intValue("USize") == 128);
    CHECK(object.intValue("VSize") == 128);
    REQUIRE(object.find("InternalTime", 1) != nullptr);
    CHECK(object.find("InternalTime", 2) == nullptr);
    CHECK(object.find("MipZero")->structName == "Color");
    CHECK(package.objectName(20) == "Palette24");
    CHECK(package.classOf(20) == "Palette");
}

namespace {
    int32_t findImport(rune::Linker& linker, const std::string& name)
    {
        const rune::Package& file = linker.file();
        for (size_t i = 0; i < file.imports().size(); i++) {
            int32_t ref = -static_cast<int32_t>(i + 1);
            if (file.objectName(ref) == name) {
                return ref;
            }
        }
        return 0;
    }
}

TEST_CASE("imports resolve to exports of other packages", "[data]")
{
    rune::engine::initialize(RUNE_GAME_DATA);
    rune::Linker* map = rune::objects::loadPackage(rune::Name("DM-Hel"));
    REQUIRE(map != nullptr);
    int32_t import = findImport(*map, "hwall15");
    REQUIRE(import != 0);
    CHECK(map->file().groupOf(import) == "");

    rune::UObject* resolved = map->object(import);
    REQUIRE(resolved != nullptr);
    CHECK(resolved->outermost()->name == rune::Name("Hell"));
    CHECK(resolved->name == rune::Name("hwall15"));
    CHECK(resolved->cls->name == rune::Name("Texture"));
}

TEST_CASE("imports from version 68 packages resolve", "[data]")
{
    rune::engine::initialize(RUNE_GAME_DATA);
    rune::Linker* map = rune::objects::loadPackage(rune::Name("DM-Hudson"));
    REQUIRE(map != nullptr);
    int32_t import = findImport(*map, "Hudsonremix");
    REQUIRE(import != 0);
    CHECK(map->file().groupOf(import) == "Ambient");
    rune::UObject* resolved = map->object(import);
    REQUIRE(resolved != nullptr);
    rune::UObject* outermost = resolved->outermost();
    CHECK(outermost->name == rune::Name("Addon"));
    CHECK(resolved->cls->name == rune::Name("Sound"));
    rune::Linker* addon = rune::objects::loadPackage(rune::Name("Addon"));
    REQUIRE(addon != nullptr);
    CHECK(addon->file().version() == 68);
}
