#include "core/objects.h"
#include "engine/canvas.h"
#include "engine/resources.h"
#include "engine/world.h"

#include <catch_amalgamated.hpp>

#include <algorithm>

namespace {
    rune::UTexture* loadTexture(const char* path)
    {
        rune::engine::initialize(RUNE_GAME_DATA);
        rune::UObject* object = rune::objects::loadObject(path, nullptr);
        return rune::cast<rune::UTexture>(object);
    }

    struct StyleRecorder : rune::Canvas2D {
        std::vector<rune::BlendStyle> styles;

        float width() const override
        {
            return 640;
        }

        float height() const override
        {
            return 480;
        }

        void setClip(float, float, float, float) override
        {
        }

        void clearClip() override
        {
        }

        void drawTile(rune::UTexture*, float, float, float, float, float, float, float, float, rune::LinearColor, rune::BlendStyle style, bool) override
        {
            styles.push_back(style);
        }

        void drawRect(float, float, float, float, rune::LinearColor, rune::BlendStyle) override
        {
        }

        void drawLine(float, float, float, float, rune::LinearColor) override
        {
        }

        void drawImage(const uint8_t*, int, int, float, float, float, float) override
        {
        }
    };
}

TEST_CASE("P8 texture loads mips and palette", "[data]")
{
    rune::UTexture* texture = loadTexture("Brckston.BRCK6_c");
    REQUIRE(texture != nullptr);
    CHECK(texture->format() == 0);
    REQUIRE(texture->mips.size() == 8);
    CHECK(texture->mips[0].width == 128);
    CHECK(texture->mips[0].height == 128);
    CHECK(texture->mips[0].data.size() == 16384);
    CHECK(texture->mips[0].data[0] == 0x36);
    CHECK(texture->mips[7].width == 1);
    REQUIRE(texture->palette() != nullptr);
    REQUIRE(texture->palette()->colors.size() == 256);

    std::vector<uint8_t> rgba = texture->rgba(0);
    REQUIRE(rgba.size() == 128 * 128 * 4);
    CHECK(rgba[0] == 0x1a);
    CHECK(rgba[1] == 0x1e);
    CHECK(rgba[2] == 0x1e);
    CHECK(rgba[3] == 0xff);
}

TEST_CASE("procedural textures have no stored pixels", "[data]")
{
    rune::UTexture* texture = loadTexture("Fluid.WAT5PROC");
    REQUIRE(texture != nullptr);
    CHECK(texture->cls->name == rune::Name("WetTexture"));
    CHECK_FALSE(texture->decodable());
}

TEST_CASE("masked font pages take their alpha from the palette", "[data]")
{
    rune::engine::initialize(RUNE_GAME_DATA);
    rune::UFont* font = rune::cast<rune::UFont>(rune::objects::loadObject("Engine.RuneButton", nullptr));
    REQUIRE(font != nullptr);
    REQUIRE_FALSE(font->pages.empty());
    rune::UTexture* texture = font->pages[0].texture;
    REQUIRE(texture != nullptr);
    rune::objects::ensureLoaded(texture);
    REQUIRE(texture->masked());

    const std::vector<uint8_t>& indices = texture->mips[0].data;
    std::vector<uint8_t> rgba = texture->rgba(0);
    size_t edge = std::find(indices.begin(), indices.end(), 0x0f) - indices.begin();
    size_t core = std::find(indices.begin(), indices.end(), 0xff) - indices.begin();
    REQUIRE(edge < indices.size());
    REQUIRE(core < indices.size());
    CHECK(indices[0] == 0);
    CHECK(rgba[3] == 0);
    CHECK(rgba[edge * 4 + 3] == 0x0f);
    CHECK(rgba[core * 4 + 3] == 0xff);
}

TEST_CASE("normal style text uses premultiplied blending for alpha fonts and masking for others", "[data]")
{
    rune::engine::initialize(RUNE_GAME_DATA);
    rune::UFont* alphaFont = rune::cast<rune::UFont>(rune::objects::loadObject("Engine.RuneButton", nullptr));
    rune::UFont* plainFont = rune::cast<rune::UFont>(rune::objects::loadObject("UWindowFonts.Tahoma10", nullptr));
    REQUIRE(alphaFont != nullptr);
    REQUIRE(plainFont != nullptr);
    StyleRecorder recorder;
    rune::drawText(recorder, alphaFont, "AB", 0, 0, {}, rune::BlendStyle::Normal, 0);
    rune::drawText(recorder, plainFont, "A", 0, 0, {}, rune::BlendStyle::Normal, 0);
    rune::drawText(recorder, alphaFont, "A", 0, 0, {}, rune::BlendStyle::Translucent, 0);
    REQUIRE(recorder.styles.size() == 4);
    CHECK(recorder.styles[0] == rune::BlendStyle::Highlighted);
    CHECK(recorder.styles[1] == rune::BlendStyle::Highlighted);
    CHECK(recorder.styles[2] == rune::BlendStyle::Masked);
    CHECK(recorder.styles[3] == rune::BlendStyle::Translucent);
}
