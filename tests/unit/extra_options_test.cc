#include "core/objects.h"
#include "core/config.h"
#include "core/vm.h"
#include "engine/actor.h"
#include "engine/canvas.h"
#include "engine/canvas_hooks.h"
#include "engine/console.h"
#include "engine/props_engine.h"
#include "engine/package.h"
#include "engine/resources.h"
#include "engine/world.h"
#include "video_fixture.h"

#include <catch_amalgamated.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <tuple>
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
        float ul = 0;
        float vl = 0;
        rune::UTexture* texture = nullptr;
    };

    class RecordingCanvas : public rune::Canvas2D {
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

        void drawTile(rune::UTexture* texture, float x, float y, float w, float h, float u, float v, float ul, float vl, rune::LinearColor, rune::BlendStyle, bool) override
        {
            tiles.push_back({ x, y, w, h, u, v, ul, vl, texture });
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

    float& floatProperty(rune::UObject* object, const char* name)
    {
        uint8_t* address = object->propertyAddress(rune::Name(name));
        REQUIRE(address != nullptr);
        return *reinterpret_cast<float*>(address);
    }

    rune::UObject* objectProperty(rune::UObject* object, const char* name, int index = 0)
    {
        uint8_t* address = object->propertyAddress(rune::Name(name), index);
        REQUIRE(address != nullptr);
        return *reinterpret_cast<rune::UObject**>(address);
    }
}

TEST_CASE("large text draws every glyph at twice its size and measures twice as wide", "[data]")
{
    rune::engine::initialize(RUNE_GAME_DATA);
    rune::UFont* font = rune::cast<rune::UFont>(rune::objects::loadObject("Engine.SmallFont", nullptr));
    REQUIRE(font != nullptr);
    rune::objects::ensureLoaded(font);
    const rune::FontCharacter* a = font->glyph(static_cast<uint8_t>('A'), nullptr);
    const rune::FontCharacter* b = font->glyph(static_cast<uint8_t>('B'), nullptr);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    float aw = static_cast<float>(a->width);
    float ah = static_cast<float>(a->height);
    float bw = static_cast<float>(b->width);
    REQUIRE(aw == 6);
    REQUIRE(ah == 8);
    REQUIRE(bw == 6);

    CHECK(rune::textWidth(font, "AB", 1) == 14);
    CHECK(rune::textHeight(font) == 8);

    rune::setTextScale(2);
    CHECK(rune::textWidth(font, "AB", 1) == 28);
    CHECK(rune::textHeight(font) == 16);
    RecordingCanvas canvas;
    rune::drawText(canvas, font, "AB", 10, 20, {}, rune::BlendStyle::Normal, 1);
    rune::setTextScale(1);

    REQUIRE(canvas.tiles.size() == 2);
    CHECK(canvas.tiles[0].x == 10);
    CHECK(canvas.tiles[0].y == 20);
    CHECK(canvas.tiles[0].w == 12);
    CHECK(canvas.tiles[0].h == 16);
    CHECK(canvas.tiles[0].u == static_cast<float>(a->u));
    CHECK(canvas.tiles[0].v == static_cast<float>(a->v));
    CHECK(canvas.tiles[0].ul == 6);
    CHECK(canvas.tiles[0].vl == 8);
    CHECK(canvas.tiles[1].x == 24);
    CHECK(canvas.tiles[1].w == 12);
}

TEST_CASE("script New creates the replacement registered for a class", "[data]")
{
    rune::engine::initialize(RUNE_GAME_DATA);
    rune::UClass* window = rune::objects::findClass("UWindow.UWindowWindow");
    rune::UClass* client = rune::objects::findClass("UWindow.UWindowClientWindow");
    REQUIRE(window != nullptr);
    REQUIRE(client != nullptr);
    REQUIRE(client->isChildOf(window));
    rune::UObject* parent = rune::objects::newObject(window, rune::objects::packageObject(rune::Name("Transient")), rune::Name(), 0);
    REQUIRE(parent != nullptr);

    auto create = [&]() {
        rune::vm::Call call(parent, rune::Name("CreateWindow"));
        REQUIRE(call.valid());
        call.obj(window).f(0).f(0).f(100).f(50).run();
        return *reinterpret_cast<rune::UObject**>(call.returnValue());
    };

    rune::UObject* plain = create();
    REQUIRE(plain != nullptr);
    CHECK(plain->cls == window);

    rune::objects::replaceClass(window, client);
    rune::UObject* replaced = create();
    rune::objects::replaceClass(window, window);
    REQUIRE(replaced != nullptr);
    CHECK(replaced->cls == client);
    CHECK(floatProperty(replaced, "WinWidth") == 100);
}

TEST_CASE("compiled and bundled RuneExtra packages add an Extra button beside the options buttons", "[data]")
{
    const char* xdg = std::getenv("XDG_DATA_HOME");
    REQUIRE(xdg != nullptr);
    std::filesystem::path home = std::filesystem::path(xdg) / "rune-extra";
    std::filesystem::remove_all(home);
    std::filesystem::path output = home / "RuneExtra.u";
    SECTION("compile from sources")
    {
        std::string command = "XDG_DATA_HOME=\"" + home.string() + "\" \"" UCC_PATH "\" makepackage RuneExtra \"" RUNE_SOURCE_DIR "/src/script/RuneExtra/Classes\" \"" + output.string() + "\" > /dev/null 2>&1";
        REQUIRE(std::system(command.c_str()) == 0);
    }
    SECTION("use the release bundle")
    {
        output = RUNE_SOURCE_DIR "/src/script/RuneExtra/Prebuilt/RuneExtra.u";
    }
    REQUIRE(std::filesystem::exists(output));

    rune::engine::initialize(RUNE_GAME_DATA);
    rune::objects::unloadPackage(rune::Name("RuneExtra"));
    REQUIRE(rune::objects::loadPackageFile(rune::Name("RuneExtra"), output, false) != nullptr);
    rune::UClass* top = rune::objects::findClass("RuneExtra.RuneExtraOptionsTop");
    rune::UClass* original = rune::objects::findClass("RMenu.RuneMenuOptionsTop");
    rune::UClass* page = rune::objects::findClass("RuneExtra.RuneExtraOptionsClientWindow");
    REQUIRE(top != nullptr);
    REQUIRE(original != nullptr);
    REQUIRE(page != nullptr);
    CHECK(top->isChildOf(original));
    CHECK(page->findProperty(rune::Name("bLargeText")) != nullptr);

    rune::UObject* menu = rune::objects::newObject(top, rune::objects::packageObject(rune::Name("Transient")), rune::Name(), 0);
    REQUIRE(menu != nullptr);
    rune::vm::Call created(menu, rune::Name("Created"));
    REQUIRE(created.valid());
    created.run();
    rune::UObject* extra = objectProperty(menu, "ExtraButton");
    rune::UObject* controls = objectProperty(menu, "ControlsButton");
    rune::UObject* input = objectProperty(menu, "InputButton");
    REQUIRE(extra != nullptr);
    REQUIRE(controls != nullptr);
    REQUIRE(input != nullptr);
    CHECK(floatProperty(controls, "WinLeft") == 10);
    CHECK(floatProperty(input, "WinLeft") == 150);
    CHECK(floatProperty(extra, "WinLeft") == 290);
    CHECK(floatProperty(extra, "WinTop") == 60);

    rune::objects::replaceClass(original, top);
    REQUIRE(rune::objects::replacementClass(original) == top);
    rune::objects::unloadPackage(rune::Name("RuneExtra"));
    CHECK(rune::objects::replacementClass(original) == original);
    std::filesystem::remove_all(home);
}

TEST_CASE("the HUD draws its message text at the large text scale and leaves the scale at one afterwards", "[data]")
{
    rune::engine::initialize(RUNE_GAME_DATA);
    rune::objects::unloadPackage(rune::Name("RagnarVillage2"));
    auto world = std::make_unique<rune::World>();
    REQUIRE(world->loadMap("RagnarVillage2"));
    rune::UObject* player = world->player();
    REQUIRE(player != nullptr);
    rune::playerpawn::RendMap(player) = 5;
    rune::UClass* hudClass = rune::objects::findClass("RuneI.RuneHUD");
    REQUIRE(hudClass != nullptr);
    rune::UObject* hud = world->spawn(hudClass, player, rune::Name(), rune::actor::Location(player), {}, nullptr, true);
    REQUIRE(hud != nullptr);
    rune::playerpawn::myHUD(player) = hud;
    rune::vm::Call message(hud, rune::Name("Message"));
    REQUIRE(message.valid());
    message.obj(nullptr).s("Hello there").n(rune::Name("Event")).run();
    world->tick(0.1f);

    rune::UObject* canvas = rune::objects::newObject(rune::engineClasses().canvas, rune::objects::packageObject(rune::Name("Transient")), rune::Name(), 0);
    REQUIRE(canvas != nullptr);
    auto draw = [&](float scale) {
        RecordingCanvas target;
        rune::vm::event(canvas, rune::Name("Reset"));
        rune::canvas::SizeX(canvas) = 640;
        rune::canvas::SizeY(canvas) = 480;
        rune::canvas::ClipX(canvas) = 640;
        rune::canvas::ClipY(canvas) = 480;
        rune::canvasHooks().target = &target;
        rune::postRenderHud(player, canvas, scale);
        rune::canvasHooks().target = nullptr;
        return target.tiles;
    };

    std::vector<Tile> unscaled = draw(1);
    std::set<rune::UTexture*> fontPages;
    for (rune::UObject* object : rune::objects::all()) {
        if (rune::UFont* loaded = rune::cast<rune::UFont>(object)) {
            for (const rune::FontPage& page : loaded->pages) {
                fontPages.insert(page.texture);
            }
        }
    }
    std::map<std::tuple<rune::UTexture*, float, float, float, float>, Tile> glyphs;
    for (const Tile& tile : unscaled) {
        if (fontPages.count(tile.texture)) {
            REQUIRE(tile.w == tile.ul);
            glyphs[{ tile.texture, tile.u, tile.v, tile.ul, tile.vl }] = tile;
        }
    }
    REQUIRE(glyphs.size() >= 5);
    int doubled = 0;
    for (const Tile& tile : draw(2)) {
        auto it = glyphs.find({ tile.texture, tile.u, tile.v, tile.ul, tile.vl });
        if (it != glyphs.end()) {
            CHECK(tile.w == 2 * tile.ul);
            CHECK(tile.h == 2 * tile.vl);
            doubled++;
        }
    }
    CHECK(doubled >= 5);

    rune::UFont* font = rune::cast<rune::UFont>(rune::objects::loadObject("Engine.SmallFont", nullptr));
    REQUIRE(font != nullptr);
    rune::objects::ensureLoaded(font);
    CHECK(rune::textHeight(font) == static_cast<float>(font->glyph(static_cast<uint8_t>('A'), nullptr)->height));
    world.reset();
    rune::objects::unloadPackage(rune::Name("RagnarVillage2"));
}

TEST_CASE("the extra HUD keeps subtitles above the bloodlust bar only where the original would overlap it", "[data]")
{
    const char* xdg = std::getenv("XDG_DATA_HOME");
    REQUIRE(xdg != nullptr);
    std::filesystem::path home = std::filesystem::path(xdg) / "rune-extra-hud";
    std::filesystem::remove_all(home);
    std::filesystem::path output = home / "RuneExtra.u";
    std::string command = "XDG_DATA_HOME=\"" + home.string() + "\" \"" UCC_PATH "\" makepackage RuneExtra \"" RUNE_SOURCE_DIR "/src/script/RuneExtra/Classes\" \"" + output.string() + "\" > /dev/null 2>&1";
    REQUIRE(std::system(command.c_str()) == 0);

    rune::engine::initialize(RUNE_GAME_DATA);
    rune::objects::unloadPackage(rune::Name("RuneExtra"));
    REQUIRE(rune::objects::loadPackageFile(rune::Name("RuneExtra"), output, false) != nullptr);
    rune::objects::unloadPackage(rune::Name("RagnarVillage2"));
    auto world = std::make_unique<rune::World>();
    REQUIRE(world->loadMap("RagnarVillage2"));
    rune::UObject* player = world->player();
    REQUIRE(player != nullptr);
    rune::playerpawn::RendMap(player) = 5;
    rune::UClass* original = rune::objects::findClass("RuneI.RuneHUD");
    rune::UClass* extra = rune::objects::findClass("RuneExtra.RuneExtraHUD");
    REQUIRE(original != nullptr);
    REQUIRE(extra != nullptr);
    CHECK(extra->isChildOf(original));
    rune::UObject* canvas = rune::objects::newObject(rune::engineClasses().canvas, rune::objects::packageObject(rune::Name("Transient")), rune::Name(), 0);
    REQUIRE(canvas != nullptr);

    struct Layout {
        float textBottom = 0;
        float textTop = 1e9f;
        float barTop = 1e9f;
    };

    auto layout = [&](rune::UClass* hudClass, float width, float height) {
        rune::UObject* hud = world->spawn(hudClass, player, rune::Name(), rune::actor::Location(player), {}, nullptr, true);
        REQUIRE(hud != nullptr);
        rune::playerpawn::myHUD(player) = hud;
        rune::vm::Call message(hud, rune::Name("Message"));
        REQUIRE(message.valid());
        message.obj(nullptr).s("Ragnar...It's fair time the village added your sword to our ranks, friend.").n(rune::Name("Subtitle")).run();
        std::vector<Tile> tiles;
        for (int frame = 0; frame < 20; frame++) {
            RecordingCanvas target;
            rune::vm::event(canvas, rune::Name("Reset"));
            rune::canvas::SizeX(canvas) = static_cast<int32_t>(width);
            rune::canvas::SizeY(canvas) = static_cast<int32_t>(height);
            rune::canvas::ClipX(canvas) = width;
            rune::canvas::ClipY(canvas) = height;
            rune::canvasHooks().target = &target;
            rune::postRenderHud(player, canvas, 1);
            rune::canvasHooks().target = nullptr;
            tiles = target.tiles;
            world->tick(0.1f);
        }
        std::set<rune::UTexture*> fontPages;
        for (rune::UObject* object : rune::objects::all()) {
            if (rune::UFont* loaded = rune::cast<rune::UFont>(object)) {
                for (const rune::FontPage& page : loaded->pages) {
                    fontPages.insert(page.texture);
                }
            }
        }
        Layout result;
        for (const Tile& tile : tiles) {
            if (fontPages.count(tile.texture)) {
                result.textBottom = std::max(result.textBottom, tile.y + tile.h);
                result.textTop = std::min(result.textTop, tile.y);
            }
            if (tile.texture && (tile.texture->name == rune::Name("BloodEmpty") || tile.texture->name == rune::Name("BloodFull"))) {
                result.barTop = std::min(result.barTop, tile.y);
            }
        }
        world->destroy(hud);
        return result;
    };

    Layout wideOriginal = layout(original, 1920, 1080);
    Layout wideExtra = layout(extra, 1920, 1080);
    REQUIRE(wideOriginal.barTop < 1080);
    CHECK(wideOriginal.textTop == 1015);
    CHECK(wideOriginal.textBottom > wideOriginal.barTop - 12);
    CHECK(wideExtra.barTop == wideOriginal.barTop);
    CHECK(wideExtra.textBottom <= wideExtra.barTop - 12);
    CHECK(wideExtra.textBottom >= wideExtra.barTop - 13);

    Layout smallOriginal = layout(original, 1024, 768);
    Layout smallExtra = layout(extra, 1024, 768);
    CHECK(smallOriginal.textTop == 703);
    CHECK(smallExtra.textTop == smallOriginal.textTop);
    CHECK(smallExtra.textBottom == smallOriginal.textBottom);

    world.reset();
    rune::objects::unloadPackage(rune::Name("RagnarVillage2"));
    rune::objects::unloadPackage(rune::Name("RuneExtra"));
    std::filesystem::remove_all(home);
}

TEST_CASE("the bundled RuneExtra contains only our script classes and no game assets")
{
    rune::Package package(RUNE_SOURCE_DIR "/src/script/RuneExtra/Prebuilt/RuneExtra.u");
    std::set<std::string> classes;
    std::set<std::string> allowedTypes { "Class", "Function", "TextBuffer", "FloatProperty", "BoolProperty", "ObjectProperty", "IntProperty", "StrProperty", "ByteProperty" };
    for (size_t i = 0; i < package.exports().size(); i++) {
        int32_t ref = static_cast<int32_t>(i + 1);
        std::string type = package.classOf(ref);
        CAPTURE(type, package.objectName(ref));
        CHECK(allowedTypes.count(type) == 1);
        if (type == "Class") {
            classes.insert(package.objectName(ref));
        }
    }
    const std::set<std::string> expected { "RuneExtraHUD", "RuneExtraOptionsTop", "RuneExtraOptionsClientWindow", "RuneExtraOptionsScrollClient", "RuneExtraVideoOptionsClientWindow" };
    CHECK(classes == expected);
    REQUIRE(package.findExport("Class", "RuneExtraHUD") > 0);
    CHECK(package.exportAt(package.findExport("Class", "RuneExtraHUD")).superRef < 0);
    REQUIRE(package.findExport("Class", "RuneExtraOptionsTop") > 0);
    CHECK(package.exportAt(package.findExport("Class", "RuneExtraOptionsTop")).superRef < 0);
}

TEST_CASE("the video options selector changes window mode and restores the active choice after failure", "[data]")
{
    rune::engine::initialize(RUNE_GAME_DATA);
    rune::World world;
    REQUIRE(world.loadMap("DM-Hel"));
    rune::objects::unloadPackage(rune::Name("RuneExtra"));
    REQUIRE(rune::objects::loadPackageFile(rune::Name("RuneExtra"), RUNE_SOURCE_DIR "/src/script/RuneExtra/Prebuilt/RuneExtra.u", false) != nullptr);
    auto previousGet = rune::console::currentWindowModeHook;
    auto previousSet = rune::console::setWindowModeHook;
    auto previousRes = rune::console::currentResHook;
    auto previousResolutions = rune::console::resolutionsHook;
    rune::console::currentResHook = []() { return std::string("800x600"); };
    rune::console::resolutionsHook = []() { return std::string("640x480 800x600"); };
    VideoFixture fixture;
    bool fail = false;
    rune::console::currentWindowModeHook = [&]() { return std::string(rune::windowModeName(fixture.video.windowMode())); };
    rune::console::setWindowModeHook = [&](const std::string& mode) {
        if (fail) {
            return false;
        }
        rune::WindowMode requested = mode == "borderless" ? rune::WindowMode::Borderless : mode == "fullscreen" ? rune::WindowMode::Fullscreen
                                                                                                                : rune::WindowMode::Windowed;
        return fixture.video.setWindowMode(requested, 800, 600);
    };
    auto* rootClass = rune::objects::findClass("UWindow.UWindowRootWindow");
    REQUIRE(rootClass != nullptr);
    auto* root = rune::objects::newObject(rootClass, rune::objects::packageObject(rune::Name("Transient")), rune::Name(), 0);
    rune::vm::Call(root, rune::Name("BeginPlay")).run();
    *reinterpret_cast<rune::UObject**>(root->propertyAddress(rune::Name("Console"))) = rune::player::Console(world.viewport);
    rune::vm::Call look(root, rune::Name("GetLookAndFeel"));
    look.s("RMenu.RuneLookAndFeel").run();
    *reinterpret_cast<rune::UObject**>(root->propertyAddress(rune::Name("LookAndFeel"))) = *reinterpret_cast<rune::UObject**>(look.returnValue());
    auto* pageClass = rune::objects::findClass("RuneExtra.RuneExtraVideoOptionsClientWindow");
    REQUIRE(pageClass != nullptr);
    rune::vm::Call create(root, rune::Name("CreateWindow"));
    create.obj(pageClass).f(0).f(0).f(440).f(600).run();
    auto* page = *reinterpret_cast<rune::UObject**>(create.returnValue());
    REQUIRE(page != nullptr);
    auto* combo = objectProperty(page, "WindowModeCombo");
    REQUIRE(combo != nullptr);
    auto selected = [&]() {
        rune::vm::Call value(combo, rune::Name("GetValue2"));
        value.run();
        return *reinterpret_cast<std::string*>(value.returnValue());
    };
    auto resolutionCount = [&]() {
        auto* list = objectProperty(objectProperty(page, "ResolutionCombo"), "List");
        auto* items = objectProperty(list, "Items");
        rune::vm::Call count(items, rune::Name("Count"));
        count.run();
        return *reinterpret_cast<int32_t*>(count.returnValue());
    };
    CHECK(selected() == "windowed");
    CHECK(fixture.video.windowMode() == rune::WindowMode::Windowed);
    CHECK(resolutionCount() == 2);
    CHECK(floatProperty(combo, "WinTop") < floatProperty(objectProperty(page, "DriverLabel"), "WinTop"));
    rune::vm::Call(combo, rune::Name("SetSelectedIndex")).i(1).run();
    CHECK(selected() == "borderless");
    CHECK(fixture.video.windowMode() == rune::WindowMode::Borderless);
    CHECK(resolutionCount() == 1);
    rune::vm::Call(combo, rune::Name("SetSelectedIndex")).i(2).run();
    CHECK(selected() == "fullscreen");
    CHECK(fixture.video.windowMode() == rune::WindowMode::Fullscreen);
    CHECK(resolutionCount() == 2);
    rune::vm::Call(combo, rune::Name("SetSelectedIndex")).i(0).run();
    CHECK(selected() == "windowed");
    CHECK(fixture.video.windowMode() == rune::WindowMode::Windowed);
    fail = true;
    rune::vm::Call(combo, rune::Name("SetSelectedIndex")).i(1).run();
    CHECK(selected() == "windowed");
    CHECK(fixture.video.windowMode() == rune::WindowMode::Windowed);
    rune::console::currentResHook = previousRes;
    rune::console::resolutionsHook = previousResolutions;
    rune::console::currentWindowModeHook = previousGet;
    rune::console::setWindowModeHook = previousSet;
    rune::objects::unloadPackage(rune::Name("RuneExtra"));
}

namespace {
    struct GraphicsOptions {
        rune::World world;
        rune::Ini previous;
        rune::UClass* pageClass = nullptr;
        const std::string section = "RuneGraphicsTest.Client";

        GraphicsOptions()
        {
            rune::engine::initialize(RUNE_GAME_DATA);
            REQUIRE(world.loadMap("DM-Hel"));
            previous = rune::config::system();
            rune::config::system().set("Engine.Engine", "ViewportManager", section);
            rune::objects::unloadPackage(rune::Name("RuneExtra"));
            REQUIRE(rune::objects::loadPackageFile(rune::Name("RuneExtra"), RUNE_SOURCE_DIR "/src/script/RuneExtra/Prebuilt/RuneExtra.u", false) != nullptr);
            pageClass = rune::objects::findClass("RuneExtra.RuneExtraOptionsClientWindow");
            REQUIRE(pageClass != nullptr);
        }

        ~GraphicsOptions()
        {
            rune::config::system() = previous;
            rune::config::system().save();
            rune::objects::unloadPackage(rune::Name("RuneExtra"));
        }

        rune::UObject* create()
        {
            auto* rootClass = rune::objects::findClass("UWindow.UWindowRootWindow");
            REQUIRE(rootClass != nullptr);
            auto* root = rune::objects::newObject(rootClass, rune::objects::packageObject(rune::Name("Transient")), rune::Name(), 0);
            rune::vm::Call(root, rune::Name("BeginPlay")).run();
            *reinterpret_cast<rune::UObject**>(root->propertyAddress(rune::Name("Console"))) = rune::player::Console(world.viewport);
            rune::vm::Call look(root, rune::Name("GetLookAndFeel"));
            look.s("RMenu.RuneLookAndFeel").run();
            *reinterpret_cast<rune::UObject**>(root->propertyAddress(rune::Name("LookAndFeel"))) = *reinterpret_cast<rune::UObject**>(look.returnValue());
            rune::vm::Call call(root, rune::Name("CreateWindow"));
            call.obj(pageClass).f(0).f(0).f(440).f(360).run();
            auto* page = *reinterpret_cast<rune::UObject**>(call.returnValue());
            REQUIRE(page != nullptr);
            return page;
        }
    };

    std::string comboValue(rune::UObject* combo)
    {
        REQUIRE(combo != nullptr);
        rune::vm::Call value(combo, rune::Name("GetValue2"));
        value.run();
        return *reinterpret_cast<std::string*>(value.returnValue());
    }

    std::string checkboxValue(rune::UObject* check)
    {
        REQUIRE(check != nullptr);
        auto* property = check->cls->findProperty(rune::Name("bChecked"));
        REQUIRE(property != nullptr);
        return property->toText(check->data + property->offset);
    }
}

TEST_CASE("graphics controls load renderer defaults without changing settings", "[data]")
{
    GraphicsOptions fixture;
    REQUIRE(rune::config::system().section(fixture.section).empty());
    auto* page = fixture.create();
    const char* defaults[] = { "0", "0", "0", "1", "0" };
    for (int i = 0; i < 5; i++) {
        CAPTURE(i);
        CHECK(comboValue(objectProperty(page, "GraphicsCombo", i)) == defaults[i]);
    }
    for (int i = 0; i < 3; i++) {
        CHECK(checkboxValue(objectProperty(page, "GraphicsCheck", i)) == "False");
    }
    CHECK(objectProperty(page, "LargeTextCheck") != nullptr);
    CHECK(rune::config::system().section(fixture.section).empty());
}

TEST_CASE("all eight graphics controls save their settings and reload them when reopened", "[data]")
{
    GraphicsOptions fixture;
    auto* page = fixture.create();
    const char* comboKeys[] = { "Anisotropy", "Multisampling", "DetailDistance", "VSync", "FrameRateLimit" };
    const int choices[] = { 4, 3, 2, 2, 7 };
    const char* expected[] = { "4", "3", "2", "2", "7" };
    const char* checkKeys[] = { "UseTrilinear", "FXAA", "SmoothLightmaps" };
    for (int i = 0; i < 5; i++) {
        CAPTURE(comboKeys[i]);
        auto* combo = objectProperty(page, "GraphicsCombo", i);
        REQUIRE(combo != nullptr);
        rune::vm::Call(combo, rune::Name("SetSelectedIndex")).i(choices[i]).run();
        CHECK(rune::config::system().get(fixture.section, comboKeys[i]) == expected[i]);
    }
    for (int i = 0; i < 3; i++) {
        CAPTURE(checkKeys[i]);
        auto* check = objectProperty(page, "GraphicsCheck", i);
        REQUIRE(check != nullptr);
        rune::vm::Call(check, rune::Name("LMouseUp")).f(0).f(0).run();
        CHECK(rune::config::system().get(fixture.section, checkKeys[i]) == "True");
    }
    rune::Ini saved;
    REQUIRE(saved.load(rune::config::system().path));
    for (int i = 0; i < 5; i++) {
        CHECK(saved.get(fixture.section, comboKeys[i]) == expected[i]);
    }
    for (int i = 0; i < 3; i++) {
        CHECK(saved.get(fixture.section, checkKeys[i]) == "True");
    }
    auto* reopened = fixture.create();
    for (int i = 0; i < 5; i++) {
        CHECK(comboValue(objectProperty(reopened, "GraphicsCombo", i)) == expected[i]);
    }
    for (int i = 0; i < 3; i++) {
        CHECK(checkboxValue(objectProperty(reopened, "GraphicsCheck", i)) == "True");
    }
}

TEST_CASE("graphics choices clamp saved indices to the available menu choices", "[data]")
{
    GraphicsOptions fixture;
    rune::config::system().set(fixture.section, "Anisotropy", "999");
    rune::config::system().set(fixture.section, "Multisampling", "-5");
    auto* page = fixture.create();
    CHECK(comboValue(objectProperty(page, "GraphicsCombo", 0)) == "4");
    CHECK(comboValue(objectProperty(page, "GraphicsCombo", 1)) == "0");
    CHECK(rune::config::system().get(fixture.section, "Anisotropy") == "999");
}
