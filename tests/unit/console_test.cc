#include "core/config.h"
#include "core/objects.h"
#include "core/vm.h"
#include "engine/console.h"
#include "engine/net.h"
#include "engine/props_engine.h"
#include "engine/world.h"
#include "video_fixture.h"

#include <catch_amalgamated.hpp>

#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {
    std::string sectionValue(const std::filesystem::path& path, const std::string& section, const std::string& key)
    {
        std::ifstream file(path);
        std::string line;
        bool inSection = false;
        while (std::getline(file, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (!line.empty() && line.front() == '[') {
                inSection = line == "[" + section + "]";
                continue;
            }
            if (inSection && line.rfind(key + "=", 0) == 0) {
                return line.substr(key.size() + 1);
            }
        }
        return {};
    }
}

TEST_CASE("set ini: writes the audio device section and get reads it back", "[data]")
{
    rune::engine::initialize(RUNE_GAME_DATA);
    std::string original = rune::console::execute(nullptr, "get ini:Engine.Engine.AudioDevice MusicVolume");
    REQUIRE(original == "255");
    rune::console::execute(nullptr, "set ini:Engine.Engine.AudioDevice MusicVolume 100");
    CHECK(rune::console::execute(nullptr, "get ini:Engine.Engine.AudioDevice MusicVolume") == "100");
    CHECK(sectionValue(rune::config::system().path, "ALAudio.ALAudioSubsystem", "MusicVolume") == "100");
    rune::console::execute(nullptr, "set ini:Engine.Engine.AudioDevice MusicVolume " + original);
    CHECK(sectionValue(rune::config::system().path, "ALAudio.ALAudioSubsystem", "MusicVolume") == "255");
}

TEST_CASE("SET InputExt rebinds a key and saves it to the user ini", "[data]")
{
    rune::engine::initialize(RUNE_GAME_DATA);
    std::string original = rune::console::binding(rune::console::keyFromName("N"));
    rune::console::execute(nullptr, "SET InputExt N ShowGoalsWindow");
    CHECK(rune::console::binding(rune::console::keyFromName("N")) == "ShowGoalsWindow");
    CHECK(rune::console::execute(nullptr, "KEYBINDING N") == "ShowGoalsWindow");
    CHECK(sectionValue(rune::config::user().path, "Engine.Input", "N") == "ShowGoalsWindow");
    rune::console::execute(nullptr, "SET InputExt N " + original);
    CHECK(sectionValue(rune::config::user().path, "Engine.Input", "N") == original);
}

TEST_CASE("set of a config property updates the class default and persists it", "[data]")
{
    rune::engine::initialize(RUNE_GAME_DATA);
    std::string original = rune::console::execute(nullptr, "get RunePlayer bCheckPointSave");
    REQUIRE(original == "True");
    rune::console::execute(nullptr, "set RunePlayer bCheckPointSave False");
    CHECK(rune::console::execute(nullptr, "get RunePlayer bCheckPointSave") == "False");
    CHECK(sectionValue(rune::config::user().path, "RuneI.RunePlayer", "bCheckPointSave") == "False");
    rune::console::execute(nullptr, "set RunePlayer bCheckPointSave " + original);
    CHECK(sectionValue(rune::config::user().path, "RuneI.RunePlayer", "bCheckPointSave") == "True");
}

TEST_CASE("the video menu's color depth queries report 32 bit")
{
    CHECK(rune::console::execute(nullptr, "GetColorDepths") == "32");
    CHECK(rune::console::execute(nullptr, "GetCurrentColorDepth") == "32");
}

TEST_CASE("open and start pass partial and absolute travel types", "[data]")
{
    rune::engine::initialize(RUNE_GAME_DATA);
    std::vector<std::pair<std::string, int32_t>> requests;
    auto previous = rune::console::openMapHook;
    rune::console::openMapHook = [&](const std::string& url, int32_t travelType) {
        requests.emplace_back(url, travelType);
    };
    rune::console::execute(nullptr, "open Goblin2?Difficulty=2");
    rune::console::execute(nullptr, "start RagnarVillage");
    rune::console::openMapHook = previous;
    REQUIRE(requests.size() == 2);
    CHECK(requests[0].first == "Goblin2?Difficulty=2");
    CHECK(requests[0].second == 1);
    CHECK(requests[1].second == 0);
}

TEST_CASE("servertravel only travels on a dedicated server", "[data]")
{
    rune::engine::initialize(RUNE_GAME_DATA);
    rune::objects::unloadPackage(rune::Name("DwarfMap2"));
    {
        rune::World world;
        REQUIRE(world.loadMap("DwarfMap2"));
        std::vector<std::string> requests;
        auto previous = rune::console::openMapHook;
        rune::console::openMapHook = [&](const std::string& url, int32_t) {
            requests.push_back(url);
        };
        bool handled = true;
        rune::console::execute(nullptr, "servertravel RagnarVillage", &handled);
        CHECK_FALSE(handled);
        CHECK(rune::levelinfo::NextURL(world.levelInfo).empty());
        world.netMode = rune::net::NM_DedicatedServer;
        rune::console::execute(nullptr, "servertravel RagnarVillage", &handled);
        CHECK(handled);
        CHECK(rune::levelinfo::NextURL(world.levelInfo) == "RagnarVillage");
        rune::console::openMapHook = previous;
        CHECK(requests.empty());
    }
    rune::objects::unloadPackage(rune::Name("DwarfMap2"));
}

TEST_CASE("a Talk binding opens the script console with a Say prompt", "[data]")
{
    rune::engine::initialize(RUNE_GAME_DATA);
    rune::objects::unloadPackage(rune::Name("DM-Hel"));
    {
        rune::World world;
        REQUIRE(world.loadMap("DM-Hel"));
        rune::UObject* scriptConsole = rune::player::Console(world.viewport);
        REQUIRE(scriptConsole != nullptr);
        CHECK(rune::console::bTyping(scriptConsole) == 0);
        rune::console::execute(world.player(), "Talk");
        CHECK(rune::console::bTyping(scriptConsole) != 0);
        CHECK(rune::console::TypedStr(scriptConsole) == "Say ");
    }
    rune::objects::unloadPackage(rune::Name("DM-Hel"));
}

TEST_CASE("Console.ConsoleCommand prints the command output to the console", "[data]")
{
    rune::engine::initialize(RUNE_GAME_DATA);
    rune::objects::unloadPackage(rune::Name("DM-Hel"));
    {
        rune::World world;
        REQUIRE(world.loadMap("DM-Hel"));
        rune::UObject* scriptConsole = rune::player::Console(world.viewport);
        REQUIRE(scriptConsole != nullptr);
        rune::vm::Call call(scriptConsole, rune::Name("ConsoleCommand"));
        call.s("get RunePlayer bCheckPointSave").run();
        CHECK(rune::console::MsgText(scriptConsole, rune::console::TopLine(scriptConsole)) == "True");
    }
    rune::objects::unloadPackage(rune::Name("DM-Hel"));
}

TEST_CASE("console commands reach input aliases before script exec functions and actors only reach the engine", "[data]")
{
    rune::engine::initialize(RUNE_GAME_DATA);
    rune::objects::unloadPackage(rune::Name("DwarfMap2"));
    {
        rune::World world;
        REQUIRE(world.loadMap("DwarfMap2"));
        rune::UObject* player = world.player();
        REQUIRE(player != nullptr);
        world.timeSeconds = 100.0f;
        rune::levelinfo::TimeSeconds(world.levelInfo) = 100.0f;
        int32_t health = rune::pawn::Health(player);
        REQUIRE(health > 0);
        rune::UObject* goblin = nullptr;
        for (rune::UObject* a : world.actors) {
            if (a && !a->destroyed && a->name == rune::Name("GoblinWarrior0")) {
                goblin = a;
            }
        }
        REQUIRE(goblin != nullptr);
        {
            rune::vm::Call call(goblin, rune::Name("ConsoleCommand"));
            REQUIRE(call.valid());
            call.s("Suicide").run();
        }
        CHECK(rune::pawn::Health(player) == health);
        rune::config::user().set("Engine.Input", "Aliases[60]", "(Command=\"Button bFire\",Alias=Suicide)");
        rune::console::loadBindings();
        rune::console::execute(player, "Suicide");
        CHECK(rune::pawn::Health(player) == health);
        rune::config::user().set("Engine.Input", "Aliases[60]", "");
        rune::console::loadBindings();
        rune::console::execute(player, "Suicide");
        CHECK(rune::pawn::Health(player) == 0);
    }
    rune::objects::unloadPackage(rune::Name("DwarfMap2"));
}

TEST_CASE("window mode commands report the active mode and reject invalid choices")
{
    auto previousGet = rune::console::currentWindowModeHook;
    auto previousSet = rune::console::setWindowModeHook;
    VideoFixture fixture;
    rune::console::currentWindowModeHook = [&]() { return std::string(rune::windowModeName(fixture.video.windowMode())); };
    rune::console::setWindowModeHook = [&](const std::string& mode) {
        rune::WindowMode requested = mode == "borderless" ? rune::WindowMode::Borderless : mode == "fullscreen" ? rune::WindowMode::Fullscreen
                                                                                                                : rune::WindowMode::Windowed;
        return fixture.video.setWindowMode(requested, 800, 600);
    };
    CHECK(rune::console::execute(nullptr, "GetWindowMode") == "windowed");
    CHECK(rune::console::execute(nullptr, "SetWindowMode BORDERLESS").empty());
    CHECK(fixture.video.windowMode() == rune::WindowMode::Borderless);
    CHECK(rune::console::execute(nullptr, "GetWindowMode") == "borderless");
    CHECK(rune::console::execute(nullptr, "SetWindowMode fullscreen").empty());
    CHECK(fixture.video.windowMode() == rune::WindowMode::Fullscreen);
    CHECK(rune::console::execute(nullptr, "GetWindowMode") == "fullscreen");
    CHECK(rune::console::execute(nullptr, "SetWindowMode windowed").empty());
    CHECK(fixture.video.windowMode() == rune::WindowMode::Windowed);
    CHECK(rune::console::execute(nullptr, "GetWindowMode") == "windowed");
    CHECK(rune::console::execute(nullptr, "SetWindowMode invalid") == "Expected windowed, borderless or fullscreen");
    CHECK(fixture.video.windowMode() == rune::WindowMode::Windowed);
    rune::console::setWindowModeHook = [](const std::string&) { return false; };
    CHECK(rune::console::execute(nullptr, "SetWindowMode borderless") == "Cannot change window mode");
    CHECK(fixture.video.windowMode() == rune::WindowMode::Windowed);
    CHECK(rune::console::execute(nullptr, "GetWindowMode") == "windowed");
    rune::console::currentWindowModeHook = previousGet;
    rune::console::setWindowModeHook = previousSet;
}
