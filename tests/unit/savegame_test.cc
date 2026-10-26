#include "core/objects.h"
#include "core/package_writer.h"
#include "core/vm.h"
#include "engine/actor.h"
#include "engine/console.h"
#include "engine/props_engine.h"
#include "engine/resources.h"
#include "engine/savegame.h"
#include "engine/world.h"

#include <catch_amalgamated.hpp>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace {
    const char* MapName = "Goblin2";
    const char* TouchMap = "TrialPit";

    std::filesystem::path freshSaveRoot(const char* name)
    {
        std::filesystem::path root = std::filesystem::temp_directory_path() / name;
        std::filesystem::remove_all(root);
        rune::savegame::setSaveRoot(root);
        return root;
    }

    rune::UObject* findActor(rune::World& world, const char* name)
    {
        for (rune::UObject* a : world.actors) {
            if (a && !a->destroyed && a->name == rune::Name(name)) {
                return a;
            }
        }
        return nullptr;
    }

    rune::UObject* findInventory(rune::UObject* owner, const char* className)
    {
        int guard = 0;
        for (rune::UObject* item = rune::actor::Inventory(owner); item && guard < 64; item = rune::actor::Inventory(item), guard++) {
            if (item->isA(rune::Name(className))) {
                return item;
            }
        }
        return nullptr;
    }

    void unloadMaps(int32_t slot, const char* map = MapName)
    {
        rune::objects::unloadPackage(rune::Name(map));
        rune::objects::unloadPackage(rune::Name("SaveGame_Save" + std::to_string(slot)));
    }

    template <typename T>
    T& member(rune::UObject* object, const char* name)
    {
        uint8_t* address = object->propertyAddress(rune::Name(name));
        REQUIRE(address != nullptr);
        return *reinterpret_cast<T*>(address);
    }

    std::string valueText(rune::UObject* object, const char* name)
    {
        rune::UProperty* property = object->cls->findProperty(rune::Name(name));
        REQUIRE(property != nullptr);
        return property->toText(object->data + property->offset);
    }

    void setValueText(rune::UObject* object, const char* name, const char* text)
    {
        rune::UProperty* property = object->cls->findProperty(rune::Name(name));
        REQUIRE(property != nullptr);
        REQUIRE(property->fromText(object->data + property->offset, text));
    }
}

TEST_CASE("saved game restores actor, inventory, reference and state values", "[data]")
{
    rune::engine::initialize(RUNE_GAME_DATA);
    std::filesystem::path root = freshSaveRoot("rune-savegame-test-restore");
    unloadMaps(7);
    std::string playerName;
    {
        rune::World world;
        REQUIRE(world.loadMap(MapName));
        for (int i = 0; i < 30; i++) {
            world.tick(1.0f / 30.0f);
        }
        rune::UObject* player = world.player();
        REQUIRE(player != nullptr);
        playerName = player->name.str();
        rune::UObject* goblin = findActor(world, "GoblinCrazy0");
        REQUIRE(goblin != nullptr);
        rune::actor::Location(goblin) = { -1234.0f, 567.0f, -99.0f };
        rune::pawn::Health(goblin) = 42;
        REQUIRE(rune::vm::gotoState(goblin, rune::Name("Wandering")));
        rune::pawn::Enemy(goblin) = player;

        rune::UClass* axeClass = rune::objects::findClass("RuneI.handaxe");
        rune::objects::flushLoads();
        REQUIRE(axeClass != nullptr);
        rune::UObject* axe = world.spawn(axeClass, nullptr, rune::Name(), rune::actor::Location(player), {}, nullptr, true);
        REQUIRE(axe != nullptr);
        rune::vm::Call give(axe, rune::Name("GiveTo"));
        give.obj(player);
        give.run();
        REQUIRE(findInventory(player, "handaxe") == axe);

        REQUIRE(rune::savegame::saveGame(world, 7));
        rune::pawn::Health(goblin) = 5;
        rune::actor::Location(goblin) = { 0.0f, 0.0f, 0.0f };
        unloadMaps(7);
    }
    CHECK(std::filesystem::is_regular_file(root / "Save7.rsa"));

    int32_t slot = -1;
    std::string url = std::string(MapName) + "?load=7";
    REQUIRE(rune::savegame::loadRequested(url, slot));
    CHECK(slot == 7);
    REQUIRE(rune::savegame::prepareLoad(slot));
    rune::World loaded;
    REQUIRE(loaded.loadMap(url));
    CHECK(loaded.level->outermost()->name == rune::Name("SaveGame_Save7"));
    rune::UObject* player = loaded.player();
    REQUIRE(player != nullptr);
    CHECK(player->name.str() == playerName);
    rune::UObject* goblin = findActor(loaded, "GoblinCrazy0");
    REQUIRE(goblin != nullptr);
    rune::Vector location = rune::actor::Location(goblin);
    CHECK(location.x == -1234.0f);
    CHECK(location.y == 567.0f);
    CHECK(location.z == -99.0f);
    CHECK(rune::pawn::Health(goblin) == 42);
    CHECK(rune::pawn::Enemy(goblin) == player);
    CHECK(rune::vm::stateName(goblin) == rune::Name("Wandering"));
    rune::UObject* restoredAxe = findInventory(player, "handaxe");
    REQUIRE(restoredAxe != nullptr);
    CHECK(rune::actor::Owner(restoredAxe) == player);
    unloadMaps(7);
    std::filesystem::remove_all(root);
}

TEST_CASE("package writer output is read back by the linker", "[data]")
{
    rune::engine::initialize(RUNE_GAME_DATA);
    rune::UClass* itemClass = rune::objects::findClass("UWindow.UWindowPulldownMenuItem");
    rune::UObject* texture = rune::objects::loadObject("Rock.Rock29_d", nullptr);
    rune::objects::flushLoads();
    REQUIRE(itemClass != nullptr);
    REQUIRE(texture != nullptr);
    rune::UObject* transient = rune::objects::packageObject(rune::Name("Transient"));
    rune::UObject* first = rune::objects::newObject(itemClass, transient, rune::Name("RoundTripFirst"), 0);
    rune::UObject* second = rune::objects::newObject(itemClass, transient, rune::Name("RoundTripSecond"), 0);
    member<std::string>(first, "Caption") = "round trip";
    member<rune::UObject*>(first, "Graphic") = texture;
    member<uint8_t>(first, "HotKey") = 71;
    member<int32_t>(first, "InternalCount") = 31;
    setValueText(first, "bChecked", "True");
    member<rune::UObject*>(first, "Next") = second;
    member<std::string>(second, "Caption") = "second";

    rune::PackageWriter writer(rune::Name("RoundTrip"));
    writer.addExport(first, nullptr);
    writer.addExport(second, first);
    writer.setFilter([](rune::UObject* object) {
        return object->linker != nullptr || rune::cast<rune::UClass>(object) != nullptr || !object->outer;
    });
    std::filesystem::path file = std::filesystem::temp_directory_path() / "rune-package-writer-round-trip.rsa";
    std::string error;
    REQUIRE(writer.save(file, &error));

    rune::Linker* linker = rune::objects::loadPackageFile(rune::Name("RoundTripPackage"), file, true);
    REQUIRE(linker != nullptr);
    int32_t firstRef = linker->findExport(rune::Name("RoundTripFirst"), rune::Name("UWindowPulldownMenuItem"), "");
    int32_t secondRef = linker->findExport(rune::Name("RoundTripSecond"), rune::Name("UWindowPulldownMenuItem"), "RoundTripFirst");
    REQUIRE(firstRef == 1);
    REQUIRE(secondRef == 2);
    rune::UObject* loadedFirst = linker->object(firstRef);
    rune::objects::flushLoads();
    REQUIRE(loadedFirst != nullptr);
    REQUIRE(loadedFirst != first);
    CHECK(member<std::string>(loadedFirst, "Caption") == "round trip");
    CHECK(member<rune::UObject*>(loadedFirst, "Graphic") == texture);
    CHECK(member<uint8_t>(loadedFirst, "HotKey") == 71);
    CHECK(member<int32_t>(loadedFirst, "InternalCount") == 31);
    CHECK(valueText(loadedFirst, "bChecked") == "True");
    CHECK(valueText(loadedFirst, "bDisabled") == "False");
    rune::UObject* loadedSecond = member<rune::UObject*>(loadedFirst, "Next");
    REQUIRE(loadedSecond != nullptr);
    CHECK(loadedSecond != second);
    CHECK(loadedSecond->name == rune::Name("RoundTripSecond"));
    CHECK(loadedSecond->outer == loadedFirst);
    CHECK(member<std::string>(loadedSecond, "Caption") == "second");
    rune::objects::unloadPackage(rune::Name("RoundTripPackage"));
    std::filesystem::remove(file);
}

TEST_CASE("a loaded saved game keeps the touching lists of its actors", "[data]")
{
    rune::engine::initialize(RUNE_GAME_DATA);
    std::filesystem::path root = freshSaveRoot("rune-savegame-test-touching");
    unloadMaps(5, TouchMap);
    std::string toucherName;
    std::string touchedName;
    {
        rune::World world;
        REQUIRE(world.loadMap(TouchMap));
        rune::console::execute(world.player(), "teleport -519 -2671 140 0");
        for (int i = 0; i < 60; i++) {
            world.tick(1.0f / 30.0f);
        }
        for (rune::UObject* a : world.actors) {
            if (!toucherName.empty() || !a || a->destroyed) {
                continue;
            }
            for (int i = 0; i < 4; i++) {
                rune::UObject* other = rune::actor::Touching(a, i);
                if (other && toucherName.empty()) {
                    toucherName = a->name.str();
                    touchedName = other->name.str();
                }
            }
        }
        REQUIRE_FALSE(toucherName.empty());
        REQUIRE(rune::savegame::saveGame(world, 5));
        unloadMaps(5, TouchMap);
    }
    REQUIRE(rune::savegame::prepareLoad(5));
    rune::World loaded;
    REQUIRE(loaded.loadMap(std::string(TouchMap) + "?load=5"));
    rune::UObject* toucher = findActor(loaded, toucherName.c_str());
    REQUIRE(toucher != nullptr);
    bool kept = false;
    for (int i = 0; i < 4; i++) {
        rune::UObject* other = rune::actor::Touching(toucher, i);
        kept = kept || (other && other->name.str() == touchedName);
    }
    CHECK(kept);
    unloadMaps(5, TouchMap);
    std::filesystem::remove_all(root);
}

TEST_CASE("a loaded saved level keeps the options of its saved URL", "[data]")
{
    rune::engine::initialize(RUNE_GAME_DATA);
    std::filesystem::path root = freshSaveRoot("rune-savegame-test-url");
    unloadMaps(6);
    std::string saved = std::string(MapName) + "?Name=Tester?Class=RuneI.Ragnar";
    {
        rune::World world;
        REQUIRE(world.loadMap(saved));
        REQUIRE(world.url == saved);
        REQUIRE(rune::savegame::saveGame(world, 6));
        unloadMaps(6);
    }
    REQUIRE(rune::savegame::prepareLoad(6));
    rune::World loaded;
    REQUIRE(loaded.loadMap(std::string(MapName) + "?load=6"));
    CHECK(loaded.level->url == saved);
    unloadMaps(6);
    std::filesystem::remove_all(root);
}

TEST_CASE("plane struct values are written and read with their own member W before the inherited X Y Z", "[data]")
{
    rune::engine::initialize(RUNE_GAME_DATA);
    rune::UClass* actorClass = rune::objects::findClass("Engine.Actor");
    REQUIRE(actorClass != nullptr);
    rune::UProperty* simAnim = actorClass->findProperty(rune::Name("SimAnim"));
    REQUIRE(simAnim != nullptr);
    REQUIRE(simAnim->structType->name == rune::Name("Plane"));
    rune::UObject* transient = rune::objects::packageObject(rune::Name("Transient"));
    rune::UObject* actor = rune::objects::newObject(actorClass, transient, rune::Name("PlaneOrderActor"), 0);
    const float values[4] = { 1.0f, 2.0f, 3.0f, 4.0f };
    for (int i = 0; i < 4; i++) {
        rune::UProperty* member = simAnim->structType->findProperty(rune::Name(std::string(1, "XYZW"[i])));
        REQUIRE(member != nullptr);
        *reinterpret_cast<float*>(actor->data + simAnim->offset + member->offset) = values[i];
    }

    rune::PackageWriter writer(rune::Name("PlaneOrder"));
    rune::Writer out;
    writer.writeTaggedProperties(out, actorClass, actor->data, actorClass->defaultObject->data);
    const std::vector<uint8_t>& bytes = out.data();
    rune::Writer tag;
    tag.index(writer.nameIndex(rune::Name("SimAnim")));
    tag.u8(0x4a);
    tag.index(writer.nameIndex(rune::Name("Plane")));
    auto at = std::search(bytes.begin(), bytes.end(), tag.data().begin(), tag.data().end());
    REQUIRE(at != bytes.end());
    size_t payload = static_cast<size_t>(at - bytes.begin()) + tag.size();
    REQUIRE(payload + 16 <= bytes.size());
    float written[4] = {};
    std::memcpy(written, bytes.data() + payload, sizeof(written));
    CHECK(written[0] == 4.0f);
    CHECK(written[1] == 1.0f);
    CHECK(written[2] == 2.0f);
    CHECK(written[3] == 3.0f);

    rune::PackageWriter package(rune::Name("PlaneOrder"));
    package.addExport(actor, nullptr);
    package.setFilter([](rune::UObject* object) {
        return object->linker != nullptr || rune::cast<rune::UClass>(object) != nullptr || !object->outer;
    });
    std::filesystem::path file = std::filesystem::temp_directory_path() / "rune-plane-order.rsa";
    std::string error;
    REQUIRE(package.save(file, &error));
    rune::Linker* linker = rune::objects::loadPackageFile(rune::Name("PlaneOrderPackage"), file, true);
    REQUIRE(linker != nullptr);
    rune::UObject* loaded = linker->object(linker->findExport(rune::Name("PlaneOrderActor"), rune::Name("Actor"), ""));
    rune::objects::flushLoads();
    REQUIRE(loaded != nullptr);
    for (int i = 0; i < 4; i++) {
        rune::UProperty* member = simAnim->structType->findProperty(rune::Name(std::string(1, "XYZW"[i])));
        CHECK(*reinterpret_cast<const float*>(loaded->data + simAnim->offset + member->offset) == values[i]);
    }
    rune::objects::unloadPackage(rune::Name("PlaneOrderPackage"));
    std::filesystem::remove(file);
}

TEST_CASE("the SaveGame command needs a slot digit", "[data]")
{
    rune::engine::initialize(RUNE_GAME_DATA);
    std::filesystem::path root = freshSaveRoot("rune-savegame-test-command");
    unloadMaps(0);
    {
        rune::World world;
        REQUIRE(world.loadMap(MapName));
        rune::console::execute(nullptr, "SaveGame slot");
        CHECK_FALSE(std::filesystem::exists(root / "Save0.rsa"));
        rune::console::execute(nullptr, "SaveGame 3");
        CHECK(std::filesystem::is_regular_file(root / "Save3.rsa"));
        unloadMaps(0);
    }
    std::filesystem::remove_all(root);
}

TEST_CASE("loading a saved game ignores incoming travel", "[data]")
{
    rune::engine::initialize(RUNE_GAME_DATA);
    std::filesystem::path root = freshSaveRoot("rune-savegame-test-travel");
    unloadMaps(5);
    std::vector<rune::TravelActor> travel;
    {
        rune::World world;
        REQUIRE(world.loadMap(MapName));
        rune::UObject* player = world.player();
        REQUIRE(player != nullptr);
        rune::pawn::Health(player) = 77;
        REQUIRE(rune::savegame::saveGame(world, 5));
        rune::pawn::Health(player) = 33;
        travel = world.collectTravel();
        unloadMaps(5);
    }
    REQUIRE(rune::savegame::prepareLoad(5));
    {
        rune::World loaded;
        loaded.incomingTravel = std::move(travel);
        REQUIRE(loaded.loadMap(std::string(MapName) + "?load=5"));
        rune::UObject* player = loaded.player();
        REQUIRE(player != nullptr);
        CHECK(rune::pawn::Health(player) == 77);
        rune::World::releaseTravel(loaded.incomingTravel);
        unloadMaps(5);
    }
    std::filesystem::remove_all(root);
}

TEST_CASE("a restart hands the arrival travel to the reloaded level", "[data]")
{
    rune::engine::initialize(RUNE_GAME_DATA);
    unloadMaps(0);
    std::vector<rune::TravelActor> travel;
    {
        rune::World first;
        REQUIRE(first.loadMap(MapName));
        rune::UObject* player = first.player();
        REQUIRE(player != nullptr);
        rune::pawn::Health(player) = 50;
        travel = first.collectTravel();
        unloadMaps(0);
    }
    std::vector<rune::TravelActor> arrival;
    {
        rune::World second;
        second.incomingTravel = std::move(travel);
        REQUIRE(second.loadMap(MapName));
        REQUIRE(second.player() != nullptr);
        CHECK(rune::pawn::Health(second.player()) == 50);
        rune::pawn::Health(second.player()) = 10;
        arrival = std::move(second.arrivalTravel);
        second.arrivalTravel.clear();
        unloadMaps(0);
    }
    {
        rune::World restarted;
        restarted.incomingTravel = std::move(arrival);
        REQUIRE(restarted.loadMap(std::string(MapName) + "?restart"));
        REQUIRE(restarted.player() != nullptr);
        CHECK(rune::pawn::Health(restarted.player()) == 50);
        unloadMaps(0);
    }
}

TEST_CASE("saving paints the saving progress except for the slot 10 checkpoint", "[data]")
{
    rune::engine::initialize(RUNE_GAME_DATA);
    std::filesystem::path root = freshSaveRoot("rune-savegame-test-progress");
    unloadMaps(0);
    {
        rune::World world;
        REQUIRE(world.loadMap(MapName));
        std::vector<int> painted;
        auto previous = rune::console::paintProgressHook;
        rune::console::paintProgressHook = [&]() {
            painted.push_back(rune::levelinfo::LevelAction(world.levelInfo));
        };
        REQUIRE(rune::savegame::saveGame(world, 4));
        REQUIRE(rune::savegame::saveGame(world, 10));
        rune::console::paintProgressHook = previous;
        REQUIRE(painted.size() == 1);
        CHECK(painted[0] == 2);
        CHECK(rune::levelinfo::LevelAction(world.levelInfo) == 0);
        unloadMaps(0);
    }
    std::filesystem::remove_all(root);
}

TEST_CASE("with a persistent entry level the arrival checkpoint is saved and entry spawns stay in the entry level", "[data]")
{
    rune::engine::initialize(RUNE_GAME_DATA);
    std::filesystem::path root = freshSaveRoot("rune-savegame-test-entry");
    unloadMaps(10);
    rune::objects::unloadPackage(rune::Name("Entry"));
    {
        rune::World entry;
        entry.loginPlayer = false;
        REQUIRE(entry.loadMap("Entry"));
        REQUIRE(entry.player() == nullptr);
        rune::World::entryLevel = &entry;
        {
            rune::World world;
            REQUIRE(world.loadMap(MapName));
            rune::UObject* player = world.player();
            REQUIRE(player != nullptr);
            CHECK(std::filesystem::is_regular_file(root / "Save10.rsa"));
            rune::vm::Call call(player, rune::Name("GetEntryLevel"));
            REQUIRE(call.valid());
            call.run();
            rune::UObject* entryInfo = *reinterpret_cast<rune::UObject**>(call.returnValue());
            CHECK(entryInfo == entry.levelInfo);
            CHECK(entryInfo != world.levelInfo);
            rune::vm::Call spawn(entryInfo, rune::Name("Spawn"));
            REQUIRE(spawn.valid());
            spawn.obj(rune::objects::findClass("Engine.Trigger")).run();
            rune::UObject* spawned = *reinterpret_cast<rune::UObject**>(spawn.returnValue());
            REQUIRE(spawned != nullptr);
            CHECK(rune::World::current == &world);
            CHECK(std::find(entry.actors.begin(), entry.actors.end(), spawned) != entry.actors.end());
            CHECK(std::find(world.actors.begin(), world.actors.end(), spawned) == world.actors.end());
            unloadMaps(10);
        }
        rune::World::entryLevel = nullptr;
    }
    rune::objects::unloadPackage(rune::Name("Entry"));
    std::filesystem::remove_all(root);
}
