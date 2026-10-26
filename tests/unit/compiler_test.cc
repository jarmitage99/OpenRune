#include "compiler/exporter.h"
#include "compiler/package_dump.h"
#include "core/objects.h"
#include "engine/world.h"

#include <catch_amalgamated.hpp>

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace {
    std::string lowered(std::string text)
    {
        for (char& c : text) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return text;
    }

    std::string normalized(const rune::compiler::DumpedExport& entry)
    {
        static const std::regex dependencyCrc(R"((\{[^}]*\}:[01]):[0-9]+)");
        std::string body = std::regex_replace(entry.body, dependencyCrc, "$1");
        if (entry.className == "TextBuffer") {
            while (!body.empty() && (body.back() == '\r' || body.back() == '\n' || body.back() == ' ')) {
                body.pop_back();
            }
        }
        return lowered(body);
    }

    std::map<std::string, rune::compiler::DumpedExport> dumpByPath(const std::filesystem::path& file)
    {
        std::map<std::string, rune::compiler::DumpedExport> result;
        for (auto& [path, entry] : rune::compiler::dumpPackage(file, std::filesystem::path(RUNE_GAME_DATA) / "System")) {
            result[lowered(path)] = entry;
        }
        return result;
    }

    int run(const std::string& command)
    {
        return std::system(command.c_str());
    }

    std::filesystem::path rebuild(const char* directory, const std::vector<std::string>& packages)
    {
        const char* xdg = std::getenv("XDG_DATA_HOME");
        REQUIRE(xdg != nullptr);
        std::filesystem::path home = std::filesystem::path(xdg) / directory;
        std::filesystem::remove_all(home);
        std::filesystem::path user = home / "rune";
        std::string environment = "XDG_DATA_HOME=\"" + home.string() + "\" ";
        for (const std::string& package : packages) {
            std::string command = environment + "\"" UCC_PATH "\" batchexport " + package + ".u class uc \"" + (user / package / "Classes").string() + "\" > /dev/null 2>&1";
            REQUIRE(run(command) == 0);
        }
        REQUIRE(run(environment + "\"" UCC_PATH "\" make > /dev/null 2>&1") == 0);
        for (const std::string& package : packages) {
            INFO(package);
            std::filesystem::path shipped = std::filesystem::path(RUNE_GAME_DATA) / "System" / (package + ".u");
            std::filesystem::path built = user / "System" / (package + ".u");
            REQUIRE(std::filesystem::exists(built));
            auto expected = dumpByPath(shipped);
            auto actual = dumpByPath(built);
            CHECK(actual.size() == expected.size());
            int mismatches = 0;
            std::string first;
            for (auto& [path, entry] : expected) {
                auto it = actual.find(path);
                bool same = it != actual.end() && it->second.className == entry.className && lowered(it->second.super) == lowered(entry.super) && it->second.flags == entry.flags && normalized(it->second) == normalized(entry);
                if (!same) {
                    mismatches++;
                    if (first.empty()) {
                        first = path;
                    }
                }
            }
            INFO("first mismatch " << first);
            CHECK(mismatches == 0);
        }
        return home;
    }
}

TEST_CASE("exported default properties use the UnrealEd format", "[data]")
{
    rune::engine::initialize(RUNE_GAME_DATA);
    rune::UClass* pawn = rune::objects::findClass("RuneI.ScriptPawn");
    REQUIRE(pawn != nullptr);
    rune::UClass* parent = pawn->superClass();
    std::string text = rune::compiler::exportProperties(pawn, pawn->defaultObject->data, parent, parent->defaultObject->data, 4);
    CHECK(text.find("     MinStopWait=0.500000\r\n") != std::string::npos);
    CHECK(text.find("     WeaponJoint=Weapon\r\n") != std::string::npos);
    CHECK(text.find("     DrawType=DT_SkeletalMesh\r\n") != std::string::npos);
}

TEST_CASE("exported floats that six decimals cannot represent keep full precision", "[data]")
{
    rune::engine::initialize(RUNE_GAME_DATA);
    rune::UClass* pawn = rune::objects::findClass("RuneI.ScriptPawn");
    REQUIRE(pawn != nullptr);
    rune::UObject* object = rune::objects::newObject(pawn, nullptr, rune::Name(), 0);
    float* walkingSpeed = reinterpret_cast<float*>(object->propertyAddress(rune::Name("WalkingSpeed")));
    REQUIRE(walkingSpeed != nullptr);
    *walkingSpeed = 1.0f / 9.0f;
    std::string text = rune::compiler::exportProperties(pawn, object->data, pawn, pawn->defaultObject->data, 4);
    std::string key = "     WalkingSpeed=";
    size_t at = text.find(key);
    REQUIRE(at != std::string::npos);
    std::string value = text.substr(at + key.size(), text.find("\r\n", at) - at - key.size());
    CHECK(value == "0.111111112");
    float parsed = static_cast<float>(std::atof(value.c_str()));
    uint32_t bits = 0;
    std::memcpy(&bits, &parsed, sizeof(bits));
    CHECK(bits == 0x3de38e39u);
}

TEST_CASE("ucc make rebuilds the shipped script packages from exported sources", "[data]")
{
    std::filesystem::path home = rebuild("ucc-make", { "Fire", "IpDrv", "UWeb", "IpServer", "RBrowser" });
    auto ipServer = dumpByPath(home / "rune" / "System" / "IpServer.u");
    CHECK(ipServer["udpserverquery"].body.find("{IpServer.UdpServerQuery}:1:1640303698,{IpDrv.UdpLink}:1:2233407030,") != std::string::npos);
    std::filesystem::remove_all(home);
}

TEST_CASE("ucc make rebuilds the native Core package from exported sources", "[data]")
{
    std::filesystem::remove_all(rebuild("ucc-make-core", { "Core" }));
}

TEST_CASE("ucc make rebuilds RuneServerAdmin, which casts to the intrinsic NetConnection class", "[data]")
{
    std::filesystem::remove_all(rebuild("ucc-make-admin", { "RuneServerAdmin" }));
}

TEST_CASE("ucc make reports script errors with file and line and writes no package", "[data]")
{
    struct BrokenScript {
        const char* source;
        const char* error;
    };

    const BrokenScript cases[] = {
        { "class Broken extends Actor;\r\n\r\nvar int Count\r\nvar int Other;\r\n", "Broken.uc(4) : Error, Missing ';' before 'var'" },
        { "class Broken extends Actor;\r\n\r\nfunction Touch(Actor Other)\r\n{\r\n\tUndefinedThing = 1;\r\n}\r\n", "Broken.uc(5) : Error, 'UndefinedThing': Bad command or expression" },
        { "class Broken extends Actor;\r\n\r\nfunction PostBeginPlay()\r\n{\r\n\tlocal int Value;\r\n\r\n\tValue = Self;\r\n}\r\n", "Broken.uc(7) : Error, Type mismatch in '='" },
    };
    const char* xdg = std::getenv("XDG_DATA_HOME");
    REQUIRE(xdg != nullptr);
    std::filesystem::path home = std::filesystem::path(xdg) / "ucc-errors";
    std::filesystem::path user = home / "rune";
    for (const BrokenScript& broken : cases) {
        INFO(broken.error);
        std::filesystem::remove_all(home);
        std::filesystem::create_directories(user / "BrokenMod" / "Classes");
        {
            std::ifstream in(std::filesystem::path(RUNE_GAME_DATA) / "System" / "Rune.ini", std::ios::binary);
            std::stringstream ini;
            ini << in.rdbuf();
            std::string text = ini.str();
            size_t last = text.rfind("EditPackages=");
            REQUIRE(last != std::string::npos);
            text.insert(text.find('\n', last) + 1, "EditPackages=BrokenMod\r\n");
            std::ofstream out(user / "Rune.ini", std::ios::binary);
            out << text;
        }
        {
            std::ofstream out(user / "BrokenMod" / "Classes" / "Broken.uc", std::ios::binary);
            out << broken.source;
        }
        std::filesystem::path log = home / "make.log";
        CHECK(run("XDG_DATA_HOME=\"" + home.string() + "\" \"" UCC_PATH "\" make > \"" + log.string() + "\" 2>&1") == 256);
        std::ifstream in(log);
        std::stringstream output;
        output << in.rdbuf();
        CHECK(output.str().find(broken.error) != std::string::npos);
        CHECK_FALSE(std::filesystem::exists(user / "System" / "BrokenMod.u"));
    }
    std::filesystem::remove_all(home);
}
