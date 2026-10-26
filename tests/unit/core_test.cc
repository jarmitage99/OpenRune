#include "core/config.h"
#include "core/objects.h"
#include "core/url.h"
#include "core/vm.h"
#include "engine/console.h"
#include "engine/input.h"
#include "engine/props_engine.h"
#include "engine/world.h"

#include <catch_amalgamated.hpp>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <string>
#include <vector>

namespace {
    void initEngine()
    {
        static bool initialized = false;
        if (!initialized) {
            rune::engine::initialize(RUNE_GAME_DATA);
            initialized = true;
        }
    }

    rune::UClass* testInfoClass()
    {
        rune::UClass* cls = rune::objects::findClass("Engine.TestInfo");
        rune::objects::flushLoads();
        REQUIRE(cls != nullptr);
        return cls;
    }

    rune::UFunction* coreFunction(const char* name)
    {
        rune::UFunction* function = rune::objects::objectClass()->findFunction(rune::Name(name));
        REQUIRE(function != nullptr);
        return function;
    }

    struct Bytes {
        std::vector<uint8_t> data;

        Bytes& op(std::initializer_list<uint8_t> values)
        {
            data.insert(data.end(), values);
            return *this;
        }

        Bytes& u16(uint16_t value)
        {
            data.push_back(static_cast<uint8_t>(value & 0xff));
            data.push_back(static_cast<uint8_t>(value >> 8));
            return *this;
        }

        Bytes& u32(uint32_t value)
        {
            for (int i = 0; i < 4; i++) {
                data.push_back(static_cast<uint8_t>(value >> (8 * i)));
            }
            return *this;
        }

        Bytes& f32(float value)
        {
            uint32_t bits;
            std::memcpy(&bits, &value, 4);
            return u32(bits);
        }

        Bytes& str(const char* text)
        {
            data.insert(data.end(), text, text + std::strlen(text) + 1);
            return *this;
        }

        Bytes& object(const rune::UObject* value)
        {
            return u32(value->handle);
        }

        Bytes& name(const char* text)
        {
            return u32(rune::Name(text).index());
        }
    };

    rune::UFunction* makeFunction(rune::UClass* owner, const char* name, const Bytes& code, uint32_t flags = 0, rune::UField* params = nullptr)
    {
        rune::UFunction* function = new rune::UFunction();
        function->name = rune::Name(name);
        function->outer = owner;
        function->functionFlags = flags;
        function->children = params;
        function->script = code.data;
        rune::objects::registerObject(function);
        function->link();
        return function;
    }

    rune::UProperty* makeParam(const char* name, uint32_t flags)
    {
        rune::UProperty* property = new rune::UProperty(rune::PropType::Int);
        property->name = rune::Name(name);
        property->propertyFlags = flags;
        rune::objects::registerObject(property);
        return property;
    }

    rune::UState* makeState(rune::UClass* owner, const char* name, const Bytes& code, uint16_t labelTable)
    {
        rune::UState* state = new rune::UState();
        state->name = rune::Name(name);
        state->outer = owner;
        state->script = code.data;
        state->labelTableOffset = labelTable;
        state->ignoreMask = ~0ull;
        rune::objects::registerObject(state);
        owner->states[state->name] = state;
        return state;
    }

    int32_t& xnum(rune::UObject* object)
    {
        uint8_t* address = object->propertyAddress(rune::Name("xnum"));
        REQUIRE(address != nullptr);
        return *reinterpret_cast<int32_t*>(address);
    }

    rune::UProperty* xnumProperty(rune::UClass* cls)
    {
        rune::UProperty* property = cls->findProperty(rune::Name("xnum"));
        REQUIRE(property != nullptr);
        return property;
    }

    Bytes incrementXnum(rune::UClass* cls)
    {
        Bytes code;
        code.op({ 0xA3, 0x01 }).object(xnumProperty(cls)).op({ 0x16 });
        return code;
    }

    template <typename T>
    T evaluate(rune::UObject* self, const Bytes& code)
    {
        rune::UStruct node;
        node.script = code.data;
        rune::Frame frame;
        frame.object = self;
        frame.node = &node;
        frame.code = node.script.data();
        T value {};
        frame.step(self, &value);
        return value;
    }
}

TEST_CASE("vector to string conversion prints six decimals like the original", "[data]")
{
    initEngine();
    Bytes code;
    code.op({ 0x58, 0x23 }).f32(1.5f).f32(-2.0f).f32(0.25f);
    CHECK(evaluate<std::string>(rune::objects::objectClass()->defaultObject, code) == "1.500000,-2.000000,0.250000");
}

TEST_CASE("string switch matches case labels case insensitively", "[data]")
{
    initEngine();
    Bytes code;
    code.op({ 0x05, 0x00, 0x1F }).str("abc");
    code.op({ 0x0A }).u16(20).op({ 0x1F }).str("ABC");
    code.op({ 0x0B, 0x0B, 0x0B, 0x0B, 0x0B });
    code.op({ 0x0A }).u16(0xffff);
    rune::UStruct node;
    node.script = code.data;
    rune::Frame frame;
    frame.object = rune::objects::objectClass()->defaultObject;
    frame.node = &node;
    frame.code = node.script.data();
    frame.step(frame.object, nullptr);
    CHECK(frame.code - node.script.data() == 15);
}

TEST_CASE("Mid clamps the start index as unsigned like FString Mid", "[data]")
{
    initEngine();
    {
        rune::vm::Call call(rune::objects::objectClass()->defaultObject, coreFunction("Mid"));
        call.s("abcdef").i(-1).i(3).run();
        CHECK(*reinterpret_cast<std::string*>(call.returnValue()) == "");
    }
    {
        rune::vm::Call call(rune::objects::objectClass()->defaultObject, coreFunction("Mid"));
        call.s("abcdef").i(4).i(10).run();
        CHECK(*reinterpret_cast<std::string*>(call.returnValue()) == "ef");
    }
}

TEST_CASE("MirrorVectorByNormal normalizes the normal first", "[data]")
{
    initEngine();
    rune::vm::Call call(rune::objects::objectClass()->defaultObject, coreFunction("MirrorVectorByNormal"));
    call.v({ 1, -1, 0 }).v({ 0, 2, 0 }).run();
    rune::Vector value = *reinterpret_cast<rune::Vector*>(call.returnValue());
    CHECK(value.x == 1.0f);
    CHECK(value.y == 1.0f);
    CHECK(value.z == 0.0f);
}

TEST_CASE("script calls to a disabled probe function are skipped", "[data]")
{
    initEngine();
    rune::UClass* cls = testInfoClass();
    rune::UObject* object = rune::objects::newObject(cls, nullptr, rune::Name(), 0);
    Bytes timerCode = incrementXnum(cls);
    timerCode.op({ 0x04, 0x0B });
    rune::UFunction* timer = makeFunction(cls, "Timer", timerCode);
    Bytes callerCode;
    callerCode.op({ 0x1C }).object(timer).op({ 0x16, 0x04, 0x0B });
    rune::UFunction* caller = makeFunction(cls, "CoreProbeCaller", callerCode);
    cls->probeMask = ~0ull;
    cls->ignoreMask = ~0ull;
    xnum(object) = 10;
    rune::vm::disable(object, rune::Name("Timer"));
    {
        rune::vm::Call call(object, caller);
        call.run();
    }
    CHECK(xnum(object) == 10);
    {
        rune::vm::Call call(object, timer);
        call.run();
    }
    CHECK(xnum(object) == 10);
    rune::vm::enable(object, rune::Name("Timer"));
    {
        rune::vm::Call call(object, caller);
        call.run();
    }
    CHECK(xnum(object) == 11);
}

TEST_CASE("singular functions do not reenter on the same object", "[data]")
{
    initEngine();
    rune::UClass* cls = testInfoClass();
    rune::UObject* object = rune::objects::newObject(cls, nullptr, rune::Name(), 0);
    rune::UFunction* recurse = makeFunction(cls, "CoreSingular", Bytes(), rune::FUNC_Singular);
    Bytes code = incrementXnum(cls);
    code.op({ 0x1C }).object(recurse).op({ 0x16, 0x04, 0x0B });
    recurse->script = code.data;
    std::vector<std::string> criticals;
    rune::vm::CriticalHandler previous = rune::vm::setCriticalHandler([&criticals](const std::string& message) {
        criticals.push_back(message);
    });
    xnum(object) = 0;
    {
        rune::vm::Call call(object, recurse);
        call.run();
    }
    CHECK(xnum(object) == 1);
    CHECK(criticals.empty());
    {
        rune::vm::Call call(object, recurse);
        call.run();
    }
    CHECK(xnum(object) == 2);
    rune::vm::setCriticalHandler(previous);
}

TEST_CASE("out parameters are copied back in reverse order", "[data]")
{
    initEngine();
    rune::UClass* cls = testInfoClass();
    rune::UObject* object = rune::objects::newObject(cls, nullptr, rune::Name(), 0);
    rune::UProperty* a = makeParam("A", rune::CPF_Parm | rune::CPF_OutParm);
    rune::UProperty* b = makeParam("B", rune::CPF_Parm | rune::CPF_OutParm);
    a->next = b;
    Bytes code;
    code.op({ 0x0F, 0x00 }).object(a).op({ 0x26 });
    code.op({ 0x0F, 0x00 }).object(b).op({ 0x2C, 0x02 });
    code.op({ 0x04, 0x0B });
    rune::UFunction* setBoth = makeFunction(cls, "CoreSetBoth", code, 0, a);
    Bytes callerCode;
    callerCode.op({ 0x1C }).object(setBoth).op({ 0x01 }).object(xnumProperty(cls)).op({ 0x01 }).object(xnumProperty(cls)).op({ 0x16, 0x04, 0x0B });
    rune::UFunction* caller = makeFunction(cls, "CoreSetBothCaller", callerCode);
    xnum(object) = 0;
    rune::vm::Call call(object, caller);
    call.run();
    CHECK(xnum(object) == 1);
}

TEST_CASE("GotoState from EndState preempts the outer GotoState", "[data]")
{
    initEngine();
    rune::UClass* cls = testInfoClass();
    rune::UObject* object = rune::objects::newObject(cls, nullptr, rune::Name(), 0);
    Bytes begin;
    begin.op({ 0x08 }).name("Begin").u32(0).u32(0).u32(0);
    rune::UState* first = makeState(cls, "CoreFirst", begin, 1);
    makeState(cls, "CoreSecond", begin, 1);
    makeState(cls, "CoreThird", begin, 1);
    Bytes endCode;
    endCode.op({ 0x71, 0x21 }).name("CoreSecond").op({ 0x16, 0x04, 0x0B });
    first->functions[rune::Name("EndState")] = makeFunction(cls, "EndState", endCode);
    first->probeMask |= 1ull << rune::vm::probeIndex(rune::Name("EndState"));
    std::vector<std::string> criticals;
    rune::vm::CriticalHandler previous = rune::vm::setCriticalHandler([&criticals](const std::string& message) {
        criticals.push_back(message);
    });
    REQUIRE(rune::vm::gotoState(object, rune::Name("CoreFirst")));
    rune::vm::gotoState(object, rune::Name("CoreThird"));
    rune::vm::setCriticalHandler(previous);
    CHECK(criticals.empty());
    CHECK(rune::vm::stateName(object) == rune::Name("CoreSecond"));
}

TEST_CASE("state code stops after four state changes in one update", "[data]")
{
    initEngine();
    rune::UClass* cls = testInfoClass();
    rune::UObject* object = rune::objects::newObject(cls, nullptr, rune::Name(), 0);
    Bytes ping;
    ping.op({ 0x71, 0x21 }).name("CorePong").op({ 0x16 }).name("Begin").u32(0).u32(0).u32(0);
    Bytes pong;
    pong.op({ 0x71, 0x21 }).name("CorePing").op({ 0x16 }).name("Begin").u32(0).u32(0).u32(0);
    rune::UState* pingState = makeState(cls, "CorePing", ping, 7);
    rune::UState* pongState = makeState(cls, "CorePong", pong, 7);
    Bytes count = incrementXnum(cls);
    count.op({ 0x04, 0x0B });
    rune::UFunction* beginState = makeFunction(cls, "BeginState", count);
    pingState->functions[rune::Name("BeginState")] = beginState;
    pongState->functions[rune::Name("BeginState")] = beginState;
    pingState->probeMask |= 1ull << rune::vm::probeIndex(rune::Name("BeginState"));
    pongState->probeMask |= 1ull << rune::vm::probeIndex(rune::Name("BeginState"));
    REQUIRE(rune::vm::gotoState(object, rune::Name("CorePing")));
    xnum(object) = 0;
    rune::vm::runStateCode(object);
    CHECK(xnum(object) == 5);
    CHECK(rune::vm::stateName(object) == rune::Name("CorePong"));
    rune::vm::runStateCode(object);
    CHECK(xnum(object) == 10);
    CHECK(rune::vm::stateName(object) == rune::Name("CorePing"));
}

TEST_CASE("globalconfig properties load from the declaring class section", "[data]")
{
    initEngine();
    rune::UClass* player = rune::objects::findClass("RuneI.RunePlayer");
    rune::objects::flushLoads();
    REQUIRE(player != nullptr);
    uint8_t* address = player->defaultObject->propertyAddress(rune::Name("bNoFlash"));
    REQUIRE(address != nullptr);
    CHECK(*reinterpret_cast<uint32_t*>(address) == 0);
}

TEST_CASE("SaveConfig writes globalconfig to the owner section and reloads class defaults", "[data]")
{
    initEngine();
    rune::UClass* gameInfo = rune::objects::findClass("Engine.GameInfo");
    rune::UClass* singlePlayer = rune::objects::findClass("RuneI.RuneSinglePlayer");
    rune::objects::flushLoads();
    REQUIRE(gameInfo != nullptr);
    REQUIRE(singlePlayer != nullptr);
    rune::UObject* object = rune::objects::newObject(singlePlayer, nullptr, rune::Name(), 0);
    int32_t& spectators = *reinterpret_cast<int32_t*>(object->propertyAddress(rune::Name("MaxSpectators")));
    int32_t original = spectators;
    spectators = original + 5;
    {
        rune::vm::Call call(object, coreFunction("SaveConfig"));
        call.run();
    }
    CHECK(rune::config::forName(rune::config::configNameOf(singlePlayer))->get("Engine.GameInfo", "MaxSpectators") == std::to_string(original + 5));
    CHECK(*reinterpret_cast<int32_t*>(gameInfo->defaultObject->propertyAddress(rune::Name("MaxSpectators"))) == original + 5);
    CHECK(*reinterpret_cast<int32_t*>(singlePlayer->defaultObject->propertyAddress(rune::Name("MaxSpectators"))) == original + 5);
    spectators = original;
    {
        rune::vm::Call call(object, coreFunction("SaveConfig"));
        call.run();
    }
    CHECK(*reinterpret_cast<int32_t*>(singlePlayer->defaultObject->propertyAddress(rune::Name("MaxSpectators"))) == original);
}

TEST_CASE("Normal returns zero below the SafeNormal size squared threshold", "[data]")
{
    initEngine();
    {
        rune::vm::Call call(rune::objects::objectClass()->defaultObject, coreFunction("Normal"));
        call.v({ 0.00005f, 0, 0 }).run();
        rune::Vector value = *reinterpret_cast<rune::Vector*>(call.returnValue());
        CHECK(value.x == 0.0f);
        CHECK(value.y == 0.0f);
        CHECK(value.z == 0.0f);
    }
    {
        rune::vm::Call call(rune::objects::objectClass()->defaultObject, coreFunction("Normal"));
        call.v({ 0, 0.5f, 0 }).run();
        rune::Vector value = *reinterpret_cast<rune::Vector*>(call.returnValue());
        CHECK(value.x == 0.0f);
        CHECK(value.y == 1.0f);
        CHECK(value.z == 0.0f);
    }
}

TEST_CASE("Chr keeps sixteen bits of the code and Caps only uppercases ASCII", "[data]")
{
    initEngine();
    {
        rune::vm::Call call(rune::objects::objectClass()->defaultObject, coreFunction("Chr"));
        call.i(321).run();
        CHECK(*reinterpret_cast<std::string*>(call.returnValue()) == "?");
    }
    {
        rune::vm::Call call(rune::objects::objectClass()->defaultObject, coreFunction("Chr"));
        call.i(65536).run();
        CHECK(reinterpret_cast<std::string*>(call.returnValue())->empty());
    }
    {
        rune::vm::Call call(rune::objects::objectClass()->defaultObject, coreFunction("Chr"));
        call.i(65601).run();
        CHECK(*reinterpret_cast<std::string*>(call.returnValue()) == "A");
    }
    {
        rune::vm::Call call(rune::objects::objectClass()->defaultObject, coreFunction("Caps"));
        call.s("a\xe9z").run();
        CHECK(*reinterpret_cast<std::string*>(call.returnValue()) == "A\xe9Z");
    }
}

TEST_CASE("new defaults the outer to self and masks the object flags", "[data]")
{
    initEngine();
    rune::UClass* cls = testInfoClass();
    rune::UObject* self = rune::objects::newObject(cls, nullptr, rune::Name(), 0);
    Bytes code;
    code.op({ 0x11, 0x0B, 0x21 }).u32(0).op({ 0x1D }).u32(0xffffffffu).op({ 0x20 }).object(cls);
    rune::UObject* created = evaluate<rune::UObject*>(self, code);
    REQUIRE(created != nullptr);
    CHECK(created->outer == self);
    CHECK(created->flags == 0x704005u);
}

TEST_CASE("a destroyed but not yet cleaned up object still runs calls made through it", "[data]")
{
    initEngine();
    rune::UClass* cls = testInfoClass();
    rune::UObject* caller = rune::objects::newObject(cls, nullptr, rune::Name(), 0);
    rune::UObject* target = rune::objects::newObject(cls, nullptr, rune::Name(), 0);
    Bytes incrementCode = incrementXnum(cls);
    incrementCode.op({ 0x04, 0x0B });
    rune::UFunction* increment = makeFunction(cls, "CoreContextIncrement", incrementCode);
    Bytes callerCode;
    callerCode.op({ 0x19, 0x20 }).object(target).u16(6).op({ 0x00, 0x1C }).object(increment).op({ 0x16, 0x04, 0x0B });
    rune::UFunction* through = makeFunction(cls, "CoreContextCaller", callerCode);
    xnum(target) = 0;
    rune::objects::destroyObject(target);
    rune::vm::Call call(caller, through);
    call.run();
    CHECK(xnum(target) == 1);
}

TEST_CASE("relative and partial travel inherit the current URL options like FURL")
{
    std::string base = "RagnarVillage?Name=Ragnar?Class=RuneI.Ragnar?load?Difficulty=2?push";
    CHECK(rune::travelUrl(base, "ragnarvillage2?difficulty=3?Quiet", rune::TRAVEL_Relative) == "ragnarvillage2?Name=Ragnar?Class=RuneI.Ragnar?difficulty=3?Quiet");
    CHECK(rune::travelUrl(base, "?Name=Sigurd", rune::TRAVEL_Relative) == "RagnarVillage?Name=Sigurd?Class=RuneI.Ragnar?Difficulty=2");
    CHECK(rune::travelUrl(base, "Goblin2", rune::TRAVEL_Partial) == "Goblin2?Name=Ragnar?Class=RuneI.Ragnar?Difficulty=2");
    CHECK(rune::travelUrl(base, "Goblin2?Name=X", rune::TRAVEL_Absolute) == "Goblin2?Name=X");
    CHECK(rune::travelUrl("intro?video=intro.ogv?Name=Ragnar", "RagnarVillage", rune::TRAVEL_Partial) == "RagnarVillage?Name=Ragnar");
}

TEST_CASE("URL options are looked up by key without case")
{
    CHECK(rune::urlOption("intro?Video=intro.ogv?port=7778", "video") == "intro.ogv");
    CHECK(rune::urlOption("intro?Video=intro.ogv?port=7778", "PORT") == "7778");
    CHECK(rune::urlOption("intro?Video=intro.ogv?quiet", "quiet").empty());
    CHECK(rune::urlOption("intro", "video").empty());
}

TEST_CASE("a subclass created at runtime inherits the parent's defaults, functions and states", "[data]")
{
    initEngine();
    rune::UClass* parent = testInfoClass();
    rune::UClass* sub = rune::objects::createSubclass(parent, rune::Name("RuntimeTestInfo"));
    CHECK(sub->superClass() == parent);
    CHECK(sub->isChildOf(parent));
    CHECK(sub->findFunction(rune::Name("TestStructBools")) == parent->findFunction(rune::Name("TestStructBools")));
    CHECK(sub->findState(rune::Name("AA")) == parent->findState(rune::Name("AA")));
    rune::UObject* object = rune::objects::newObject(sub, nullptr, rune::Name(), 0);
    CHECK(object->cls == sub);
    CHECK(object->isA(parent));
    CHECK(xnum(object) == 777);
    CHECK(*reinterpret_cast<std::string*>(object->propertyAddress(rune::Name("sxx"))) == "Tim");
}

TEST_CASE("the game root is found when RuneI.u is spelled Runei.u as in the Rune demo")
{
    std::filesystem::path root = std::filesystem::temp_directory_path() / "rune-demo-root-test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "System");
    std::ofstream(root / "System" / "Runei.u") << "package";
    const char* previous = std::getenv("RUNE_GAME_DATA");
    std::string saved = previous ? previous : "";
#ifdef _WIN32
    _putenv_s("RUNE_GAME_DATA", root.string().c_str());
#else
    setenv("RUNE_GAME_DATA", root.string().c_str(), 1);
#endif
    std::filesystem::path found = rune::config::findGameRoot("fallback");
#ifdef _WIN32
    _putenv_s("RUNE_GAME_DATA", saved.c_str());
#else
    if (previous) {
        setenv("RUNE_GAME_DATA", saved.c_str(), 1);
    } else {
        unsetenv("RUNE_GAME_DATA");
    }
#endif
    std::filesystem::remove_all(root);
    CHECK(found == root);
}

TEST_CASE("ResetConfig copies the class section from Default.ini and reloads the class defaults", "[data]")
{
    initEngine();
    rune::UClass* gameInfo = rune::objects::findClass("Engine.GameInfo");
    rune::objects::flushLoads();
    REQUIRE(gameInfo != nullptr);
    rune::Ini& system = rune::config::system();
    std::filesystem::path saved = system.path;
    system.path = std::filesystem::temp_directory_path() / "rune_reset_config_test.ini";
    system.set("Engine.GameInfo", "MaxSpectators", "77");
    rune::config::applyObjectConfig(gameInfo->defaultObject);
    int32_t& spectators = *reinterpret_cast<int32_t*>(gameInfo->defaultObject->propertyAddress(rune::Name("MaxSpectators")));
    REQUIRE(spectators == 77);
    {
        rune::vm::Call call(gameInfo->defaultObject, coreFunction("ResetConfig"));
        call.run();
    }
    CHECK(system.get("Engine.GameInfo", "MaxSpectators") == "2");
    CHECK(spectators == 2);
    std::filesystem::remove(system.path);
    system.path = saved;
}

TEST_CASE("ResetKeyboard restores the input bindings from DefUser.ini", "[data]")
{
    initEngine();
    rune::Ini& user = rune::config::user();
    std::filesystem::path saved = user.path;
    user.path = std::filesystem::temp_directory_path() / "rune_reset_keyboard_test.ini";
    user.set("Engine.Input", "Tab", "Jump");
    rune::console::loadBindings();
    uint8_t tab = rune::console::keyFromName("Tab");
    REQUIRE(rune::console::binding(tab) == "Jump");
    rune::UClass* playerPawn = rune::objects::findClass("Engine.PlayerPawn");
    rune::objects::flushLoads();
    REQUIRE(playerPawn != nullptr);
    {
        rune::vm::Call call(playerPawn->defaultObject, rune::Name("ResetKeyboard"));
        REQUIRE(call.valid());
        call.run();
    }
    CHECK(user.get("Engine.Input", "Tab") == "Type");
    CHECK(rune::console::binding(tab) == "Type");
    std::filesystem::remove(user.path);
    user.path = saved;
}

TEST_CASE("a button bound to two keys is released by either key", "[data]")
{
    initEngine();
    rune::objects::unloadPackage(rune::Name("RagnarVillage2"));
    {
        rune::World world;
        REQUIRE(world.loadMap("RagnarVillage2"));
        rune::UObject* player = world.player();
        REQUIRE(player != nullptr);
        rune::Ini& user = rune::config::user();
        rune::Ini saved = user;
        user.set("Engine.Input", "LeftMouse", "Button bFire");
        user.set("Engine.Input", "Enter", "Button bFire");
        rune::console::loadBindings();
        uint8_t mouse = rune::console::keyFromName("LeftMouse");
        uint8_t enter = rune::console::keyFromName("Enter");
        rune::pawn::bFire(player) = 0;
        rune::console::key(mouse, rune::IST_Press, 0);
        rune::console::key(enter, rune::IST_Press, 0);
        CHECK(rune::pawn::bFire(player) == 1);
        rune::console::key(enter, rune::IST_Release, 0);
        CHECK(rune::pawn::bFire(player) == 0);
        rune::console::key(mouse, rune::IST_Release, 0);
        CHECK(rune::pawn::bFire(player) == 0);
        user = saved;
        rune::console::loadBindings();
    }
    rune::objects::unloadPackage(rune::Name("RagnarVillage2"));
}
