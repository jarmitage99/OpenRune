#include "core/objects.h"
#include "core/vm.h"
#include "engine/actor.h"
#include "engine/props_engine.h"
#include "engine/world.h"

#include <catch_amalgamated.hpp>

#include <string>
#include <vector>

namespace {
    rune::UObject* giveSword(rune::World& world, rune::UObject* player)
    {
        rune::UClass* cls = rune::objects::findClass("RuneI.VikingShortSword");
        rune::objects::flushLoads();
        if (!cls) {
            return nullptr;
        }
        rune::UObject* sword = world.spawn(cls, player, rune::Name(), rune::actor::Location(player), {}, nullptr, true);
        if (sword) {
            rune::pawn::Weapon(player) = sword;
        }
        return sword;
    }
}

TEST_CASE("ClientTravel sends PreClientTravel to the player and marks the travel pending", "[data]")
{
    rune::engine::initialize(RUNE_GAME_DATA);
    rune::objects::unloadPackage(rune::Name("RagnarVillage2"));
    rune::World world;
    REQUIRE(world.loadMap("RagnarVillage2"));
    for (int i = 0; i < 30; i++) {
        world.tick(1.0f / 30.0f);
    }
    rune::UObject* player = world.player();
    REQUIRE(player != nullptr);
    rune::UObject* sword = giveSword(world, player);
    REQUIRE(sword != nullptr);
    uint8_t* poweredUp = sword->propertyAddress(rune::Name("bPoweredUp"));
    REQUIRE(poweredUp != nullptr);
    *reinterpret_cast<uint32_t*>(poweredUp) = 1;
    CHECK_FALSE(world.travelPending);
    {
        rune::vm::Call call(player, rune::Name("ClientTravel"));
        call.s("Thor1").byte(0).b(true);
        call.run();
    }
    CHECK(*reinterpret_cast<uint32_t*>(poweredUp) == 0);
    CHECK(world.travelPending);
    CHECK(world.travelUrl == "Thor1");
    CHECK(world.travelType == 0);
    CHECK(world.travelItems);
    rune::objects::unloadPackage(rune::Name("RagnarVillage2"));
}

TEST_CASE("travel carries changed values and keeps defaults from overwriting the new player", "[data]")
{
    rune::engine::initialize(RUNE_GAME_DATA);
    rune::objects::unloadPackage(rune::Name("RagnarVillage2"));
    rune::objects::unloadPackage(rune::Name("Thor1"));
    std::vector<rune::TravelActor> travel;
    {
        rune::World world;
        REQUIRE(world.loadMap("RagnarVillage2"));
        rune::UObject* player = world.player();
        REQUIRE(player != nullptr);
        REQUIRE(player->cls->name == rune::Name("TownRagnar"));
        REQUIRE(rune::pawn::Health(player) == rune::pawn::Health(player->cls->defaultObject));
        REQUIRE(rune::pawn::Health(player) == 50);
        rune::pawn::Strength(player) = 37;
        REQUIRE(giveSword(world, player) != nullptr);
        travel = world.collectTravel();
        rune::objects::unloadPackage(rune::Name("RagnarVillage2"));
    }
    rune::World next;
    next.incomingTravel = std::move(travel);
    REQUIRE(next.loadMap("Thor1"));
    rune::UObject* player = next.player();
    REQUIRE(player != nullptr);
    REQUIRE(player->cls->name == rune::Name("RagnarSnow"));
    CHECK(rune::pawn::Health(player) == 100);
    CHECK(rune::pawn::Strength(player) == 37);
    rune::UObject* weapon = rune::pawn::Weapon(player);
    REQUIRE(weapon != nullptr);
    CHECK_FALSE(weapon->destroyed);
    CHECK(weapon->cls->name == rune::Name("VikingShortSword"));
    CHECK(rune::actor::Owner(weapon) == player);
    rune::objects::unloadPackage(rune::Name("Thor1"));
}

TEST_CASE("the travel string uses the original GetTravelString layout", "[data]")
{
    rune::engine::initialize(RUNE_GAME_DATA);
    rune::objects::unloadPackage(rune::Name("RagnarVillage2"));
    rune::World world;
    REQUIRE(world.loadMap("RagnarVillage2"));
    for (int i = 0; i < 30; i++) {
        world.tick(1.0f / 30.0f);
    }
    rune::UObject* player = world.player();
    REQUIRE(player != nullptr);
    rune::pawn::Strength(player) = 37;
    rune::UObject* sword = giveSword(world, player);
    REQUIRE(sword != nullptr);
    std::string text = world.travelString(player);
    std::string header = "Class=RuneI.TownRagnar Name=" + player->name.str() + " Ptr=";
    REQUIRE(text.rfind(header, 0) == 0);
    size_t open = text.find("\r\n{\r\n");
    size_t close = text.find("}\r\n");
    REQUIRE(open != std::string::npos);
    REQUIRE(close != std::string::npos);
    std::string block = text.substr(open, close - open);
    CHECK(block.find("\r\nStrength=37\r\n") != std::string::npos);
    CHECK(block.find("\r\nHealth=") == std::string::npos);
    size_t weapon = block.find("\r\nWeapon=");
    REQUIRE(weapon != std::string::npos);
    std::string weaponName = block.substr(weapon + 9 + 9, block.find("\r\n", weapon + 2) - weapon - 18);
    CHECK(weaponName == sword->name.str());
    CHECK(text.find("Class=RuneI.VikingShortSword Name=" + weaponName + " Ptr=") != std::string::npos);
    rune::objects::unloadPackage(rune::Name("RagnarVillage2"));
}
