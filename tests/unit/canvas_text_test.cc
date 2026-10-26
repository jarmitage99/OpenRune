#include "core/config.h"
#include "core/objects.h"
#include "core/vm.h"
#include "engine/canvas.h"
#include "engine/actor.h"
#include "engine/canvas_hooks.h"
#include "engine/props_engine.h"
#include "engine/resources.h"
#include "engine/world.h"

#include <catch_amalgamated.hpp>

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

namespace {
    struct Tile {
        float x = 0;
        float y = 0;
        float w = 0;
        float h = 0;
        float u = 0;
        float v = 0;
    };

    class TileRecorder : public rune::Canvas2D {
    public:
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

        void drawTile(rune::UTexture*, float x, float y, float w, float h, float u, float v, float, float, rune::LinearColor, rune::BlendStyle, bool) override
        {
            tiles.push_back({ x, y, w, h, u, v });
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

        std::vector<Tile> tiles;
    };

    struct TextCanvas {
        rune::UObject* canvas = nullptr;
        rune::UFont* font = nullptr;
        TileRecorder recorder;

        TextCanvas()
        {
            rune::engine::initialize(RUNE_GAME_DATA);
            canvas = rune::objects::newObject(rune::engineClasses().canvas, rune::objects::packageObject(rune::Name("Transient")), rune::Name(), 0);
            REQUIRE(canvas != nullptr);
            font = rune::cast<rune::UFont>(rune::canvas::SmallFont(canvas));
            rune::objects::ensureLoaded(font);
            REQUIRE(font != nullptr);
            rune::canvas::Font(canvas) = font;
            rune::canvas::SizeX(canvas) = 640;
            rune::canvas::SizeY(canvas) = 480;
            rune::canvas::ClipX(canvas) = 640;
            rune::canvas::ClipY(canvas) = 480;
            rune::canvas::Style(canvas) = 1;
            rune::canvasHooks().target = &recorder;
        }

        ~TextCanvas()
        {
            rune::canvasHooks().target = nullptr;
        }

        int32_t glyphWidth(char c)
        {
            const rune::FontCharacter* glyph = font->glyph(static_cast<uint8_t>(c), nullptr);
            REQUIRE(glyph != nullptr);
            return glyph->width;
        }

        int32_t glyphHeight(char c)
        {
            const rune::FontCharacter* glyph = font->glyph(static_cast<uint8_t>(c), nullptr);
            REQUIRE(glyph != nullptr);
            return glyph->height;
        }

        void drawText(const char* text, bool carriageReturn)
        {
            rune::vm::Call call(canvas, rune::Name("DrawText"));
            REQUIRE(call.valid());
            call.s(text).b(carriageReturn).run();
        }
    };
}

TEST_CASE("DrawText advances CurY by the printed lines and again on a carriage return", "[data]")
{
    TextCanvas text;
    int32_t height = text.glyphHeight('A');
    int32_t width = text.glyphWidth('A') + text.glyphWidth('B');
    rune::canvas::CurX(text.canvas) = 10;
    rune::canvas::CurY(text.canvas) = 20;
    text.drawText("AB", false);
    CHECK(rune::canvas::CurX(text.canvas) == static_cast<float>(10 + width));
    CHECK(rune::canvas::CurY(text.canvas) == static_cast<float>(20 + height));
    CHECK(rune::canvas::CurYL(text.canvas) == static_cast<float>(height));
    REQUIRE(text.recorder.tiles.size() == 2);
    CHECK(text.recorder.tiles[0].x == 10.0f);
    CHECK(text.recorder.tiles[1].x == static_cast<float>(10 + text.glyphWidth('A')));
    rune::canvas::CurX(text.canvas) = 10;
    rune::canvas::CurY(text.canvas) = 20;
    rune::canvas::CurYL(text.canvas) = 0;
    text.drawText("AB", true);
    CHECK(rune::canvas::CurX(text.canvas) == 0.0f);
    CHECK(rune::canvas::CurY(text.canvas) == static_cast<float>(20 + 2 * height));
    CHECK(rune::canvas::CurYL(text.canvas) == 0.0f);
}

TEST_CASE("DrawText wraps at ClipX measured from CurX and starts later lines at zero", "[data]")
{
    TextCanvas text;
    int32_t first = text.glyphWidth('A') + text.glyphWidth('A');
    int32_t height = text.glyphHeight('A');
    rune::canvas::CurX(text.canvas) = 30;
    rune::canvas::CurY(text.canvas) = 0;
    rune::canvas::ClipX(text.canvas) = static_cast<float>(30 + first + text.glyphWidth(' '));
    text.drawText("AA BB", false);
    REQUIRE(text.recorder.tiles.size() == 4);
    CHECK(text.recorder.tiles[0].x == 30.0f);
    CHECK(text.recorder.tiles[0].y == 0.0f);
    CHECK(text.recorder.tiles[2].x == 0.0f);
    CHECK(text.recorder.tiles[2].y == static_cast<float>(height));
    CHECK(text.recorder.tiles[3].x == static_cast<float>(text.glyphWidth('B')));
    CHECK(rune::canvas::CurY(text.canvas) == static_cast<float>(2 * height));
}

TEST_CASE("DrawText centres a line in the space right of CurX", "[data]")
{
    TextCanvas text;
    REQUIRE(text.glyphWidth('A') == 6);
    rune::canvas::bCenter(text.canvas) = 1;
    rune::canvas::CurX(text.canvas) = 40;
    rune::canvas::ClipX(text.canvas) = 200;
    text.drawText("A", false);
    REQUIRE(text.recorder.tiles.size() == 1);
    CHECK(text.recorder.tiles[0].x == 117.0f);
}

TEST_CASE("DrawTextClipped underlines the character after an ampersand when checking hot keys", "[data]")
{
    TextCanvas text;
    rune::canvas::CurX(text.canvas) = 5;
    rune::canvas::CurY(text.canvas) = 7;
    {
        rune::vm::Call call(text.canvas, rune::Name("DrawTextClipped"));
        REQUIRE(call.valid());
        call.s("&Fa").b(true).run();
    }
    REQUIRE(text.recorder.tiles.size() == 3);
    const rune::FontCharacter* f = text.font->glyph('F', nullptr);
    const rune::FontCharacter* underline = text.font->glyph('_', nullptr);
    CHECK(text.recorder.tiles[0].x == 5.0f);
    CHECK(text.recorder.tiles[0].u == static_cast<float>(f->u));
    CHECK(text.recorder.tiles[1].x == 5.0f);
    CHECK(text.recorder.tiles[1].u == static_cast<float>(underline->u));
    CHECK(text.recorder.tiles[1].w == 6.0f);
    CHECK(text.recorder.tiles[2].x == static_cast<float>(5 + f->width));
    text.recorder.tiles.clear();
    {
        rune::vm::Call call(text.canvas, rune::Name("DrawTextClipped"));
        REQUIRE(call.valid());
        call.s("&Fa").run();
    }
    CHECK(text.recorder.tiles.size() == 3);
}

TEST_CASE("DrawTextClipped clips glyphs to ClipX", "[data]")
{
    TextCanvas text;
    int32_t width = text.glyphWidth('A');
    rune::canvas::CurX(text.canvas) = 0;
    rune::canvas::CurY(text.canvas) = 0;
    rune::canvas::ClipX(text.canvas) = static_cast<float>(width + 2);
    {
        rune::vm::Call call(text.canvas, rune::Name("DrawTextClipped"));
        REQUIRE(call.valid());
        call.s("AA").run();
    }
    REQUIRE(text.recorder.tiles.size() == 2);
    CHECK(text.recorder.tiles[0].w == static_cast<float>(width));
    CHECK(text.recorder.tiles[1].x == static_cast<float>(width));
    CHECK(text.recorder.tiles[1].w == 2.0f);
}

TEST_CASE("text natives swap the Rune fonts for BigFont outside the INT language", "[data]")
{
    TextCanvas text;
    rune::UObject* runeMed = rune::canvas::RuneMedFont(text.canvas);
    REQUIRE(runeMed != nullptr);
    rune::canvas::Font(text.canvas) = runeMed;
    text.drawText("A", false);
    CHECK(rune::canvas::Font(text.canvas) == runeMed);
    rune::Ini& system = rune::config::system();
    std::optional<std::string> saved = system.get("Engine.Engine", "Language");
    system.set("Engine.Engine", "Language", "det");
    text.drawText("A", false);
    CHECK(rune::canvas::Font(text.canvas) == rune::canvas::BigFont(text.canvas));
    system.set("Engine.Engine", "Language", saved.value_or("int"));
}
