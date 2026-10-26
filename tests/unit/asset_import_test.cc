#include "core/objects.h"
#include "engine/resources.h"
#include "engine/world.h"

#include <catch_amalgamated.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {
    int run(const std::string& command)
    {
        return std::system(command.c_str());
    }

    std::vector<uint8_t> readBytes(const std::filesystem::path& file)
    {
        std::ifstream in(file, std::ios::binary);
        return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }

    void writeBytes(const std::filesystem::path& file, const std::vector<uint8_t>& bytes)
    {
        std::filesystem::create_directories(file.parent_path());
        std::ofstream out(file, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    void put16(std::vector<uint8_t>& bytes, uint32_t value)
    {
        bytes.push_back(static_cast<uint8_t>(value));
        bytes.push_back(static_cast<uint8_t>(value >> 8));
    }

    void put32(std::vector<uint8_t>& bytes, uint32_t value)
    {
        put16(bytes, value & 0xffff);
        put16(bytes, value >> 16);
    }

    uint8_t rampPixel(uint32_t x, uint32_t y)
    {
        return static_cast<uint8_t>((y * 8 + x) * 5 + 1);
    }

    uint32_t rampRgb(uint32_t index)
    {
        uint32_t red = index;
        uint32_t green = 255 - index;
        uint32_t blue = (index * 7) & 255;
        return red | green << 8 | blue << 16;
    }

    std::vector<uint8_t> rampBitmap()
    {
        std::vector<uint8_t> bytes = { 'B', 'M' };
        put32(bytes, 14 + 40 + 1024 + 32);
        put32(bytes, 0);
        put32(bytes, 14 + 40 + 1024);
        put32(bytes, 40);
        put32(bytes, 8);
        put32(bytes, 4);
        put16(bytes, 1);
        put16(bytes, 8);
        put32(bytes, 0);
        put32(bytes, 32);
        put32(bytes, 0);
        put32(bytes, 0);
        put32(bytes, 256);
        put32(bytes, 0);
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t color = rampRgb(i);
            bytes.push_back(static_cast<uint8_t>(color >> 16));
            bytes.push_back(static_cast<uint8_t>(color >> 8));
            bytes.push_back(static_cast<uint8_t>(color));
            bytes.push_back(0);
        }
        for (uint32_t row = 0; row < 4; row++) {
            for (uint32_t x = 0; x < 8; x++) {
                bytes.push_back(rampPixel(x, 3 - row));
            }
        }
        return bytes;
    }

    std::filesystem::path importHome(const char* directory)
    {
        const char* xdg = std::getenv("XDG_DATA_HOME");
        REQUIRE(xdg != nullptr);
        std::filesystem::path home = std::filesystem::path(xdg) / directory;
        std::filesystem::remove_all(home);
        std::filesystem::create_directories(home / "rune");
        std::ifstream in(std::filesystem::path(RUNE_GAME_DATA) / "System" / "Rune.ini");
        std::vector<std::string> lines;
        std::string line;
        size_t last = 0;
        while (std::getline(in, line)) {
            lines.push_back(line);
            if (line.rfind("EditPackages=", 0) == 0) {
                last = lines.size();
            }
        }
        REQUIRE(last > 0);
        lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(last), "EditPackages=ImportTest");
        std::ofstream out(home / "rune" / "Rune.ini", std::ios::binary);
        for (const std::string& text : lines) {
            out << text << "\n";
        }
        return home;
    }

    std::string environment(const std::filesystem::path& home)
    {
        return "XDG_DATA_HOME=\"" + home.string() + "\" ";
    }

    rune::Linker* buildImportTest(const std::filesystem::path& home, const std::string& directives)
    {
        std::filesystem::path classes = home / "rune" / "ImportTest" / "Classes";
        std::filesystem::create_directories(classes);
        std::ofstream(classes / "ImportTest.uc", std::ios::binary) << "class ImportTest extends Object;\r\n\r\n"
                                                                   << directives;
        REQUIRE(run(environment(home) + "\"" UCC_PATH "\" make > /dev/null 2>&1") == 0);
        std::filesystem::path built = home / "rune" / "System" / "ImportTest.u";
        REQUIRE(std::filesystem::exists(built));
        rune::engine::initialize(RUNE_GAME_DATA);
        rune::objects::unloadPackage(rune::Name("ImportTest"));
        rune::Linker* linker = rune::objects::loadPackageFile(rune::Name("ImportTest"), built, false);
        REQUIRE(linker != nullptr);
        return linker;
    }

    template <typename T>
    T* exported(rune::Linker* linker, const char* name)
    {
        for (size_t i = 0; i < linker->file().exports().size(); i++) {
            rune::UObject* object = linker->object(static_cast<int32_t>(i + 1));
            if (object && object->name == rune::Name(name) && rune::cast<T>(object)) {
                rune::objects::ensureLoaded(object);
                rune::objects::flushLoads();
                return rune::cast<T>(object);
            }
        }
        return nullptr;
    }

    template <typename T>
    T* shipped(const char* path)
    {
        rune::UObject* object = rune::objects::loadObject(path, nullptr);
        rune::objects::flushLoads();
        rune::objects::ensureLoaded(object);
        return rune::cast<T>(object);
    }

    void checkSameFont(rune::UFont* built, rune::UFont* original)
    {
        REQUIRE(built != nullptr);
        REQUIRE(original != nullptr);
        REQUIRE(built->pages.size() == original->pages.size());
        for (size_t p = 0; p < original->pages.size(); p++) {
            const rune::FontPage& a = built->pages[p];
            const rune::FontPage& b = original->pages[p];
            REQUIRE(a.characters.size() == b.characters.size());
            for (size_t c = 0; c < b.characters.size(); c++) {
                INFO("page " << p << " character " << c);
                CHECK(a.characters[c].u == b.characters[c].u);
                CHECK(a.characters[c].v == b.characters[c].v);
                CHECK(a.characters[c].width == b.characters[c].width);
                CHECK(a.characters[c].height == b.characters[c].height);
            }
            REQUIRE(a.texture != nullptr);
            REQUIRE(b.texture != nullptr);
            rune::objects::ensureLoaded(a.texture);
            rune::objects::ensureLoaded(b.texture);
            REQUIRE(!a.texture->mips.empty());
            CHECK(a.texture->mips[0].width == b.texture->mips[0].width);
            CHECK(a.texture->mips[0].height == b.texture->mips[0].height);
            CHECK(a.texture->mips[0].data == b.texture->mips[0].data);
            REQUIRE(a.texture->palette() != nullptr);
            CHECK(a.texture->palette()->colors == b.texture->palette()->colors);
        }
    }
}

TEST_CASE("ucc make imports #exec TEXTURE IMPORT bitmaps as top-down palettized textures with a full mip chain", "[data]")
{
    std::filesystem::path home = importHome("ucc-import-texture");
    writeBytes(home / "rune" / "ImportTest" / "Textures" / "Ramp.bmp", rampBitmap());
    rune::Linker* linker = buildImportTest(home, "#exec TEXTURE IMPORT NAME=Ramp FILE=Textures\\Ramp.bmp GROUP=\"Skins\" FLAGS=2 MIPS=ON\r\n#exec TEXTURE IMPORT NAME=Flat FILE=Textures\\Ramp.bmp MIPS=OFF\r\n");

    rune::UTexture* ramp = exported<rune::UTexture>(linker, "Ramp");
    REQUIRE(ramp != nullptr);
    REQUIRE(ramp->outer != nullptr);
    CHECK(ramp->outer->name == rune::Name("Skins"));
    CHECK(ramp->masked());
    REQUIRE(ramp->mips.size() == 4);
    CHECK(ramp->mips[0].width == 8);
    CHECK(ramp->mips[0].height == 4);
    CHECK(ramp->mips[1].width == 4);
    CHECK(ramp->mips[1].height == 2);
    CHECK(ramp->mips[2].width == 2);
    CHECK(ramp->mips[2].height == 1);
    CHECK(ramp->mips[3].width == 1);
    CHECK(ramp->mips[3].height == 1);
    std::vector<uint8_t> expected;
    for (uint32_t y = 0; y < 4; y++) {
        for (uint32_t x = 0; x < 8; x++) {
            expected.push_back(rampPixel(x, y));
        }
    }
    CHECK(ramp->mips[0].data == expected);
    rune::UPalette* palette = ramp->palette();
    REQUIRE(palette != nullptr);
    REQUIRE(palette->colors.size() == 256);
    for (uint32_t i = 0; i < 256; i++) {
        INFO("palette index " << i);
        CHECK(palette->colors[i] == (rampRgb(i) | 0xff000000u));
    }

    rune::UTexture* flat = exported<rune::UTexture>(linker, "Flat");
    REQUIRE(flat != nullptr);
    CHECK(flat->outer->name == rune::Name("ImportTest"));
    CHECK_FALSE(flat->masked());
    REQUIRE(flat->mips.size() == 1);
    CHECK(flat->mips[0].data == expected);
    REQUIRE(flat->palette() != nullptr);
    CHECK(flat->palette()->colors == palette->colors);
    rune::objects::unloadPackage(rune::Name("ImportTest"));
    std::filesystem::remove_all(home);
}

TEST_CASE("ucc make stores an #exec AUDIO IMPORT wave byte for byte like the shipped sound it was exported from", "[data]")
{
    std::filesystem::path home = importHome("ucc-import-audio");
    std::filesystem::path sounds = home / "rune" / "ImportTest" / "Sounds";
    REQUIRE(run(environment(home) + "\"" UCC_PATH "\" batchexport RMenu.u Sound wav \"" + sounds.string() + "\" > /dev/null 2>&1") == 0);
    REQUIRE(std::filesystem::exists(sounds / "BigSelect.wav"));
    rune::Linker* linker = buildImportTest(home, "#exec AUDIO IMPORT FILE=\"Sounds\\BigSelect.wav\" NAME=Beep GROUP=\"Menu\"\r\n");
    rune::USound* sound = exported<rune::USound>(linker, "Beep");
    rune::USound* original = shipped<rune::USound>("RMenu.BigSelect");
    REQUIRE(sound != nullptr);
    REQUIRE(original != nullptr);
    CHECK(sound->outer->name == rune::Name("Menu"));
    CHECK(sound->format == rune::Name("WAV"));
    CHECK(sound->bytes == readBytes(sounds / "BigSelect.wav"));
    CHECK(sound->bytes == original->bytes);
    rune::objects::unloadPackage(rune::Name("ImportTest"));
    std::filesystem::remove_all(home);
}

TEST_CASE("ucc exportsources and FONT IMPORT rebuild the shipped Engine fonts glyph for glyph", "[data]")
{
    std::filesystem::path home = importHome("ucc-import-font");
    std::filesystem::path sources = home / "sources";
    REQUIRE(run(environment(home) + "\"" UCC_PATH "\" exportsources Engine.u \"" + sources.string() + "\" > /dev/null 2>&1") == 0);
    std::filesystem::path target = home / "rune" / "ImportTest";
    std::filesystem::create_directories(target / "Textures");
    std::filesystem::copy(sources / "Engine" / "Textures" / "SmallFont.bmp", target / "Textures" / "SmallFont.bmp");
    std::filesystem::copy(sources / "Engine" / "Textures" / "MedFont.pcx", target / "Textures" / "MedFont.pcx");
    std::filesystem::copy(sources / "Engine" / "Fonts", target / "Fonts");
    rune::Linker* linker = buildImportTest(home, "#exec Font Import File=Textures\\SmallFont.bmp Name=SmallFont\r\n#exec Font Import File=Textures\\MedFont.pcx Name=MedFont\r\n#exec new TrueTypeFontFactory Name=Haettenschweiler16 FontName=\"Haettenschweiler\" Height=16 AntiAlias=1 CharactersPerPage=64\r\n");
    checkSameFont(exported<rune::UFont>(linker, "SmallFont"), shipped<rune::UFont>("Engine.SmallFont"));
    checkSameFont(exported<rune::UFont>(linker, "MedFont"), shipped<rune::UFont>("Engine.MedFont"));
    checkSameFont(exported<rune::UFont>(linker, "Haettenschweiler16"), shipped<rune::UFont>("Engine.Haettenschweiler16"));
    rune::objects::unloadPackage(rune::Name("ImportTest"));
    std::filesystem::remove_all(home);
}

TEST_CASE("ucc exportsources and make rebuild the UWindow textures with the shipped mips and palettes", "[data]")
{
    const char* xdg = std::getenv("XDG_DATA_HOME");
    REQUIRE(xdg != nullptr);
    std::filesystem::path home = std::filesystem::path(xdg) / "ucc-import-uwindow";
    std::filesystem::remove_all(home);
    REQUIRE(run(environment(home) + "\"" UCC_PATH "\" exportsources UWindow.u \"" + (home / "rune").string() + "\" > /dev/null 2>&1") == 0);
    REQUIRE(run(environment(home) + "\"" UCC_PATH "\" make > /dev/null 2>&1") == 0);
    std::filesystem::path built = home / "rune" / "System" / "UWindow.u";
    REQUIRE(std::filesystem::exists(built));
    rune::engine::initialize(RUNE_GAME_DATA);
    rune::Linker* original = rune::objects::loadPackage(rune::Name("UWindow"));
    REQUIRE(original != nullptr);
    std::vector<rune::UTexture*> textures;
    for (size_t i = 0; i < original->file().exports().size(); i++) {
        if (original->file().classOf(static_cast<int32_t>(i + 1)) == "Texture") {
            rune::UTexture* texture = rune::cast<rune::UTexture>(original->object(static_cast<int32_t>(i + 1)));
            REQUIRE(texture != nullptr);
            rune::objects::ensureLoaded(texture);
            textures.push_back(texture);
        }
    }
    rune::objects::flushLoads();
    REQUIRE(textures.size() == 50);
    rune::objects::unloadPackage(rune::Name("UWindowRebuilt"));
    rune::Linker* rebuilt = rune::objects::loadPackageFile(rune::Name("UWindowRebuilt"), built, false);
    REQUIRE(rebuilt != nullptr);
    for (rune::UTexture* texture : textures) {
        INFO(texture->pathName());
        rune::UTexture* other = exported<rune::UTexture>(rebuilt, texture->name.c_str());
        REQUIRE(other != nullptr);
        if (texture->outer != original->packageObject) {
            CHECK(other->outer->name == texture->outer->name);
        } else {
            CHECK(other->outer == rebuilt->packageObject);
        }
        CHECK(other->masked() == texture->masked());
        REQUIRE(other->mips.size() == texture->mips.size());
        for (size_t m = 0; m < texture->mips.size(); m++) {
            CHECK(other->mips[m].width == texture->mips[m].width);
            CHECK(other->mips[m].height == texture->mips[m].height);
            CHECK(other->mips[m].data == texture->mips[m].data);
        }
        REQUIRE(other->palette() != nullptr);
        CHECK(other->palette()->colors == texture->palette()->colors);
    }
    rune::objects::unloadPackage(rune::Name("UWindowRebuilt"));
    std::filesystem::remove_all(home);
}
