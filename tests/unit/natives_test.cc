#include "audio/audio.h"
#include "core/config.h"
#include "core/objects.h"
#include "core/random.h"
#include "core/vm.h"
#include "engine/actor.h"
#include "engine/net_socket.h"
#include "engine/net.h"
#include "engine/canvas_hooks.h"
#include "engine/props_engine.h"
#include "engine/resources.h"
#include "engine/world.h"

#include <catch_amalgamated.hpp>

#include <algorithm>
#include <chrono>
#include <thread>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {
    struct MapWorld {
        rune::World world;
        rune::Name package;

        explicit MapWorld(const char* map = "DM-Hel")
            : package(map)
        {
            rune::engine::initialize(RUNE_GAME_DATA);
            rune::objects::unloadPackage(package);
            REQUIRE(world.loadMap(map));
        }

        ~MapWorld()
        {
            rune::objects::unloadPackage(package);
        }

        rune::UObject* placedPlayer()
        {
            rune::UObject* player = world.player();
            REQUIRE(player != nullptr);
            for (rune::UObject* a : world.actors) {
                if (a && !a->destroyed && a->name == rune::Name("PlayerStart1")) {
                    rune::actor::Location(player) = rune::actor::Location(a) + rune::Vector { 0, 0, rune::actor::CollisionHeight(player) - rune::actor::CollisionHeight(a) };
                    world.collisionHash.update(player);
                    return player;
                }
            }
            FAIL("PlayerStart1 not found");
            return nullptr;
        }
    };

    rune::UObject* spawnClass(rune::World& world, const char* path, const rune::Vector& location)
    {
        rune::UClass* cls = rune::objects::findClass(path);
        rune::objects::flushLoads();
        REQUIRE(cls != nullptr);
        rune::UObject* spawned = world.spawn(cls, nullptr, rune::Name(), location, {}, nullptr, true);
        REQUIRE(spawned != nullptr);
        rune::actor::Location(spawned) = location;
        return spawned;
    }

    rune::USound* loadSound(const char* path)
    {
        rune::UObject* sound = rune::objects::loadObject(path, nullptr);
        rune::objects::flushLoads();
        REQUIRE(rune::cast<rune::USound>(sound) != nullptr);
        return rune::cast<rune::USound>(sound);
    }

    std::vector<rune::audio::VoiceStatus> voicesOf(const rune::UObject* actor)
    {
        std::vector<rune::audio::VoiceStatus> result;
        for (const rune::audio::VoiceStatus& voice : rune::audio::voices()) {
            if (voice.actor == actor) {
                result.push_back(voice);
            }
        }
        return result;
    }

    struct Bytecode {
        std::vector<uint8_t> code;

        Bytecode& byte(uint8_t value)
        {
            code.push_back(value);
            return *this;
        }

        Bytecode& word(uint32_t value, int bytes)
        {
            for (int i = 0; i < bytes; i++) {
                code.push_back(static_cast<uint8_t>(value >> (8 * i)));
            }
            return *this;
        }

        Bytecode& native(int32_t index)
        {
            if (index < 256) {
                return byte(static_cast<uint8_t>(index));
            }
            return byte(static_cast<uint8_t>(0x60 + (index >> 8))).byte(static_cast<uint8_t>(index & 255));
        }

        Bytecode& object(const rune::UObject* value)
        {
            return byte(0x20).word(value ? value->handle : 0, 4);
        }

        Bytecode& f(float value)
        {
            uint32_t bits;
            std::memcpy(&bits, &value, 4);
            return byte(0x1E).word(bits, 4);
        }

        Bytecode& v(const rune::Vector& value)
        {
            byte(0x23);
            for (float component : { value.x, value.y, value.z }) {
                uint32_t bits;
                std::memcpy(&bits, &component, 4);
                word(bits, 4);
            }
            return *this;
        }

        Bytecode& member(const rune::UObject* self, const char* name)
        {
            rune::UProperty* property = self->cls->findProperty(rune::Name(name));
            REQUIRE(property != nullptr);
            return byte(0x01).word(property->handle, 4);
        }

        Bytecode& omit()
        {
            return byte(0x0B);
        }

        Bytecode& end()
        {
            return byte(0x16);
        }
    };

    std::vector<rune::UObject*> recorded;
    rune::UObject* recordSpawnClass = nullptr;

    void recordTarget(rune::UObject* self, rune::Frame& stack, void*)
    {
        rune::vm::finish(stack);
        recorded.push_back(rune::actor::Target(self));
        if (recordSpawnClass) {
            rune::UClass* cls = rune::cast<rune::UClass>(recordSpawnClass);
            recordSpawnClass = nullptr;
            rune::World::current->spawn(cls, nullptr, rune::Name(), rune::actor::Location(self), {}, nullptr, true);
        }
    }

    template <typename T>
    T runNative(rune::UObject* self, Bytecode& call)
    {
        rune::UStruct node;
        node.script = call.code;
        node.script.push_back(0x0B);
        rune::Frame frame;
        frame.object = self;
        frame.node = &node;
        frame.code = node.script.data();
        T result {};
        frame.step(self, &result);
        return result;
    }

    std::vector<rune::UObject*> runIterator(rune::UObject* self, Bytecode& call)
    {
        rune::vm::registerNative(4095, recordTarget);
        recorded.clear();
        Bytecode script;
        script.byte(0x2F);
        script.code.insert(script.code.end(), call.code.begin(), call.code.end());
        size_t endAt = script.code.size() + 2 + 2 + 1 + 1;
        script.word(static_cast<uint32_t>(endAt), 2);
        script.native(4095).end().byte(0x31).byte(0x30);
        runNative<uint32_t>(self, script);
        return recorded;
    }

    bool contains(const std::vector<rune::UObject*>& list, const rune::UObject* item)
    {
        return std::find(list.begin(), list.end(), item) != list.end();
    }

    void playSound(rune::UObject* source, rune::USound* sound, float radius)
    {
        Bytecode call;
        call.native(264).object(sound);
        if (radius != 0) {
            call.omit().omit().omit().f(radius);
        }
        call.end();
        runNative<int32_t>(source, call);
    }
}

TEST_CASE("PlaySound defaults to SLOT_Misc and is heard within the radius over root 1.3", "[data]")
{
    MapWorld hel;
    REQUIRE(rune::audio::initializeOffline(44100));
    rune::UObject* player = hel.placedPlayer();
    rune::Vector origin = rune::actor::Location(player);
    rune::Vector near = origin + rune::Vector { 690, 0, 0 };
    rune::Vector far = origin + rune::Vector { 712, 0, 0 };
    REQUIRE(hel.world.fastLineCheck(far, origin));
    rune::USound* sound = loadSound("WeaponsSnd.ImpCrashes.crashxbucket01");
    rune::UObject* nearBucket = spawnClass(hel.world, "RuneI.Bucket", near);
    rune::UObject* farBucket = spawnClass(hel.world, "RuneI.Bucket", far);
    rune::audio::update(origin, {}, 0.0f);
    playSound(nearBucket, sound, 800.0f);
    playSound(farBucket, sound, 800.0f);
    std::vector<rune::audio::VoiceStatus> heard = voicesOf(nearBucket);
    REQUIRE(heard.size() == 1);
    CHECK(heard[0].slot == 1);
    CHECK(voicesOf(farBucket).empty());
    rune::audio::shutdown();
}

TEST_CASE("PlaySound with no radius and no TransientSoundRadius uses 1600", "[data]")
{
    MapWorld hel;
    REQUIRE(rune::audio::initializeOffline(44100));
    rune::UObject* player = hel.placedPlayer();
    rune::Vector origin = rune::actor::Location(player);
    rune::Vector near = origin + rune::Vector { 1390, 0, 0 };
    rune::Vector far = origin + rune::Vector { 1420, 0, 0 };
    REQUIRE(hel.world.fastLineCheck(far, origin));
    rune::USound* sound = loadSound("WeaponsSnd.ImpCrashes.crashxbucket01");
    rune::UObject* nearBucket = spawnClass(hel.world, "RuneI.Bucket", near);
    rune::UObject* farBucket = spawnClass(hel.world, "RuneI.Bucket", far);
    REQUIRE(rune::actor::TransientSoundRadius(nearBucket) == 0.0f);
    rune::audio::update(origin, {}, 0.0f);
    playSound(nearBucket, sound, 0.0f);
    playSound(farBucket, sound, 0.0f);
    CHECK(voicesOf(nearBucket).size() == 1);
    CHECK(voicesOf(farBucket).empty());
    rune::audio::shutdown();
}

TEST_CASE("an obstructed PlaySound is heard within 0.6 of the hearing range at 0.35 volume", "[data]")
{
    MapWorld hel;
    REQUIRE(rune::audio::initializeOffline(44100));
    rune::UObject* player = hel.placedPlayer();
    rune::Vector origin = rune::actor::Location(player);
    rune::Vector inside = origin + rune::Vector { -660, 0, 0 };
    rune::Vector outside = origin + rune::Vector { -700, 0, 0 };
    REQUIRE_FALSE(hel.world.fastLineCheck(inside, origin));
    REQUIRE_FALSE(hel.world.fastLineCheck(outside, origin));
    rune::USound* sound = loadSound("WeaponsSnd.ImpCrashes.crashxbucket01");
    rune::UObject* insideBucket = spawnClass(hel.world, "RuneI.Bucket", inside);
    rune::UObject* outsideBucket = spawnClass(hel.world, "RuneI.Bucket", outside);
    rune::UObject* reference = spawnClass(hel.world, "RuneI.Bucket", inside);
    REQUIRE(rune::actor::Instigator(insideBucket) != player);
    rune::audio::update(origin, {}, 0.0f);
    playSound(insideBucket, sound, 1000.0f);
    playSound(outsideBucket, sound, 1000.0f);
    CHECK(voicesOf(outsideBucket).empty());
    std::vector<rune::audio::VoiceStatus> heard = voicesOf(insideBucket);
    REQUIRE(heard.size() == 1);
    REQUIRE(rune::audio::playSound(sound, reference, 1, inside, 0.35f, 1000.0f, 1.0f, true) != 0);
    std::vector<rune::audio::VoiceStatus> expected = voicesOf(reference);
    REQUIRE(expected.size() == 1);
    CHECK(heard[0].volume == expected[0].volume);
    CHECK(heard[0].volume > 0);
    rune::audio::shutdown();
}

TEST_CASE("ClientHearSound scales volume and pitch by a hundredth and takes slot and no override from the id", "[data]")
{
    MapWorld hel;
    REQUIRE(rune::audio::initializeOffline(44100));
    rune::UObject* player = hel.placedPlayer();
    rune::Vector origin = rune::actor::Location(player);
    rune::Vector location = origin + rune::Vector { 300, 0, 0 };
    rune::UObject* source = spawnClass(hel.world, "RuneI.Bucket", location);
    rune::UObject* reference = spawnClass(hel.world, "RuneI.Bucket", location);
    rune::USound* first = loadSound("WeaponsSnd.ImpCrashes.crashxbucket01");
    rune::USound* second = loadSound("WeaponsSnd.ImpCrashes.crashxkeg01");
    rune::audio::update(origin, {}, 0.0f);
    const int32_t id = 7 * 16 + 3 * 2;
    auto hear = [&](int32_t soundId, rune::USound* sound) {
        rune::vm::Call call(player, rune::Name("ClientHearSound"));
        REQUIRE(call.valid());
        call.obj(source).i(soundId).obj(sound).v(location).v({ 50, 500, 150 }).run();
    };
    hear(id, first);
    REQUIRE(rune::audio::playSound(first, reference, 3, location, 0.5f, 500.0f, 1.5f, true) != 0);
    std::vector<rune::audio::VoiceStatus> heard = voicesOf(source);
    std::vector<rune::audio::VoiceStatus> expected = voicesOf(reference);
    REQUIRE(heard.size() == 1);
    REQUIRE(expected.size() == 1);
    CHECK(heard[0].slot == 3);
    CHECK(heard[0].volume == expected[0].volume);
    CHECK(heard[0].step == expected[0].step);
    hear(id + 1, second);
    std::vector<rune::audio::VoiceStatus> kept = voicesOf(source);
    REQUIRE(kept.size() == 1);
    CHECK(kept[0].sound == first);
    hear(id, second);
    std::vector<rune::audio::VoiceStatus> replaced = voicesOf(source);
    REQUIRE(replaced.size() == 1);
    CHECK(replaced[0].sound == second);
    rune::audio::shutdown();
}

TEST_CASE("RadiusActors adds the collision radius and ChildActors walks the whole owner chain including itself", "[data]")
{
    MapWorld hel;
    rune::UObject* player = hel.placedPlayer();
    rune::Vector origin = rune::actor::Location(player) + rune::Vector { 0, 0, 2000 };
    rune::UObject* center = spawnClass(hel.world, "RuneI.Bucket", origin);
    rune::UObject* inside = spawnClass(hel.world, "RuneI.Bucket", origin + rune::Vector { 125, 0, 0 });
    rune::UObject* outside = spawnClass(hel.world, "RuneI.Bucket", origin + rune::Vector { 0, 135, 0 });
    rune::actor::CollisionRadius(inside) = 30;
    rune::actor::CollisionRadius(outside) = 30;
    rune::UClass* bucketClass = center->cls;
    Bytecode radius;
    radius.native(310).object(bucketClass).member(center, "Target").f(100.0f).end();
    std::vector<rune::UObject*> found = runIterator(center, radius);
    CHECK(contains(found, center));
    CHECK(contains(found, inside));
    CHECK_FALSE(contains(found, outside));

    rune::actor::Owner(inside) = center;
    rune::actor::Owner(outside) = inside;
    Bytecode children;
    children.native(305).object(bucketClass).member(center, "Target").end();
    std::vector<rune::UObject*> owned = runIterator(center, children);
    REQUIRE(owned.size() == 3);
    CHECK(contains(owned, center));
    CHECK(contains(owned, inside));
    CHECK(contains(owned, outside));
}

TEST_CASE("VisibleActors includes the caller and VisibleCollidingActors limits an unset radius to 1000", "[data]")
{
    MapWorld hel;
    rune::UObject* player = hel.placedPlayer();
    rune::Vector origin = rune::actor::Location(player);
    rune::Vector nearLocation = origin + rune::Vector { 900, 0, 0 };
    rune::Vector farLocation = origin + rune::Vector { 1100, 0, 0 };
    REQUIRE(hel.world.fastLineCheck(farLocation, origin));
    rune::UObject* center = spawnClass(hel.world, "RuneI.Bucket", origin);
    rune::UObject* nearBucket = spawnClass(hel.world, "RuneI.Bucket", nearLocation);
    rune::UObject* farBucket = spawnClass(hel.world, "RuneI.Bucket", farLocation);
    for (rune::UObject* a : { center, nearBucket, farBucket }) {
        REQUIRE(rune::actor::bCollideActors(a) != 0);
        hel.world.collisionHash.update(a);
    }
    Bytecode visible;
    visible.native(311).object(center->cls).member(center, "Target").f(100.0f).end();
    CHECK(contains(runIterator(center, visible), center));

    Bytecode colliding;
    colliding.native(312).object(center->cls).member(center, "Target").end();
    std::vector<rune::UObject*> found = runIterator(center, colliding);
    CHECK(contains(found, center));
    CHECK(contains(found, nearBucket));
    CHECK_FALSE(contains(found, farBucket));
}

TEST_CASE("AllActors visits actors spawned inside the loop", "[data]")
{
    MapWorld hel;
    rune::UObject* player = hel.placedPlayer();
    rune::UObject* first = spawnClass(hel.world, "RuneI.Bucket", rune::actor::Location(player) + rune::Vector { 0, 0, 2000 });
    size_t before = 0;
    for (rune::UObject* a : hel.world.actors) {
        if (a && !a->destroyed && a->cls == first->cls) {
            before++;
        }
    }
    recordSpawnClass = first->cls;
    Bytecode all;
    all.native(304).object(first->cls).member(first, "Target").end();
    CHECK(runIterator(first, all).size() == before + 1);
}

TEST_CASE("Trace defaults to tracing actors when the caller collides and a miss reports a zero hit location", "[data]")
{
    MapWorld hel;
    rune::UObject* player = hel.placedPlayer();
    rune::Vector origin = rune::actor::Location(player);
    rune::Vector end = origin + rune::Vector { 400, 0, 0 };
    REQUIRE(hel.world.fastLineCheck(end, origin));
    rune::UObject* tracer = spawnClass(hel.world, "RuneI.Bucket", origin);
    REQUIRE(rune::actor::bCollideActors(tracer) != 0);
    Bytecode miss;
    miss.native(277).member(tracer, "Velocity").member(tracer, "Acceleration").v(end).end();
    rune::actor::Velocity(tracer) = { 5, 5, 5 };
    CHECK(runNative<rune::UObject*>(tracer, miss) == nullptr);
    CHECK(rune::actor::Velocity(tracer).x == 0.0f);
    CHECK(rune::actor::Velocity(tracer).y == 0.0f);
    CHECK(rune::actor::Velocity(tracer).z == 0.0f);

    rune::UObject* keg = spawnClass(hel.world, "RuneI.Keg", origin + rune::Vector { 200, 0, 0 });
    hel.world.collisionHash.update(keg);
    Bytecode hit;
    hit.native(277).member(tracer, "Velocity").member(tracer, "Acceleration").v(end).end();
    CHECK(runNative<rune::UObject*>(tracer, hit) == keg);
}

TEST_CASE("TraceActors ignores the class argument and reports level hits unless bTraceLevel is false", "[data]")
{
    MapWorld hel;
    rune::UObject* player = hel.placedPlayer();
    rune::Vector start = rune::actor::Location(player) + rune::Vector { 300, 0, 0 };
    rune::Vector below = start + rune::Vector { 0, 0, -400 };
    REQUIRE_FALSE(hel.world.fastLineCheck(below, start));
    rune::UObject* tracer = spawnClass(hel.world, "RuneI.Bucket", start + rune::Vector { 0, 0, 2000 });
    rune::UObject* keg = spawnClass(hel.world, "RuneI.Keg", start + rune::Vector { 0, 0, -22 });
    hel.world.collisionHash.update(keg);
    rune::UObject* pawnClass = rune::engineClasses().pawn;
    Bytecode withLevel;
    withLevel.native(309).object(pawnClass).member(tracer, "Target").member(tracer, "Velocity").member(tracer, "Acceleration").v(below).v(start).end();
    std::vector<rune::UObject*> defaulted = runIterator(tracer, withLevel);
    CHECK(contains(defaulted, keg));
    CHECK(contains(defaulted, hel.world.levelInfo));
    Bytecode withoutLevel;
    withoutLevel.native(309).object(pawnClass).member(tracer, "Target").member(tracer, "Velocity").member(tracer, "Acceleration").v(below).v(start).omit().byte(0x28).end();
    std::vector<rune::UObject*> actorsOnly = runIterator(tracer, withoutLevel);
    CHECK(contains(actorsOnly, keg));
    CHECK_FALSE(contains(actorsOnly, hel.world.levelInfo));
}

TEST_CASE("colour operators wrap each channel to a byte and keep the alpha of the colour operand", "[data]")
{
    MapWorld hel;
    rune::UObject* player = hel.placedPlayer();
    auto colorOp = [&](const char* name, auto&& fill) {
        rune::vm::Call call(player, rune::Name(name));
        REQUIRE(call.valid());
        fill(call);
        call.run();
        return *reinterpret_cast<rune::Color*>(call.returnValue());
    };
    auto setColor = [](rune::vm::Call& call, size_t parameter, rune::Color value) {
        *reinterpret_cast<rune::Color*>(call.parameter(parameter)) = value;
    };
    rune::Color scaled = colorOp("Multiply_ColorFloat", [&](rune::vm::Call& call) {
        setColor(call, 0, { 200, 100, 50, 77 });
        *reinterpret_cast<float*>(call.parameter(1)) = 2.0f;
    });
    CHECK(scaled.r == 144);
    CHECK(scaled.g == 200);
    CHECK(scaled.b == 100);
    CHECK(scaled.a == 77);
    rune::Color negative = colorOp("Multiply_FloatColor", [&](rune::vm::Call& call) {
        *reinterpret_cast<float*>(call.parameter(0)) = -1.0f;
        setColor(call, 1, { 10, 10, 10, 5 });
    });
    CHECK(negative.r == 246);
    CHECK(negative.a == 5);
    rune::Color sum = colorOp("Add_ColorColor", [&](rune::vm::Call& call) {
        setColor(call, 0, { 200, 10, 0, 100 });
        setColor(call, 1, { 100, 10, 0, 200 });
    });
    CHECK(sum.r == 44);
    CHECK(sum.g == 20);
    CHECK(sum.a == 100);
    rune::Color difference = colorOp("Subtract_ColorColor", [&](rune::vm::Call& call) {
        setColor(call, 0, { 10, 20, 30, 40 });
        setColor(call, 1, { 20, 10, 0, 50 });
    });
    CHECK(difference.r == 246);
    CHECK(difference.g == 10);
    CHECK(difference.b == 30);
    CHECK(difference.a == 40);
}

TEST_CASE("MoveSmooth clears bJustTeleported and Spawn without a tag leaves Tag None", "[data]")
{
    MapWorld hel;
    rune::UObject* player = hel.placedPlayer();
    rune::actor::bJustTeleported(player) = 1;
    rune::vm::Call move(player, rune::Name("MoveSmooth"));
    REQUIRE(move.valid());
    move.v({ 0, 0, 0 }).run();
    CHECK(rune::actor::bJustTeleported(player) == 0);

    rune::UClass* bucketClass = rune::objects::findClass("RuneI.Bucket");
    rune::objects::flushLoads();
    REQUIRE(bucketClass != nullptr);
    Bytecode spawn;
    spawn.native(278).object(bucketClass).object(nullptr).omit().v(rune::actor::Location(player) + rune::Vector { 300, 0, 0 }).end();
    rune::UObject* spawned = runNative<rune::UObject*>(player, spawn);
    REQUIRE(spawned != nullptr);
    CHECK(rune::actor::Tag(spawned).none());
}

TEST_CASE("MakeNoise needs an instigator, remembers the noise on it and sets the stimulus of pawns that hear it", "[data]")
{
    MapWorld village("RagnarVillage2");
    rune::UObject* player = village.world.player();
    REQUIRE(player != nullptr);
    rune::UObject* listener = nullptr;
    for (rune::UObject* p = rune::levelinfo::PawnList(village.world.levelInfo); p; p = rune::pawn::nextPawn(p)) {
        if (p != player && !rune::isPlayerPawn(p) && p->name == rune::Name("Sigurd0")) {
            listener = p;
        }
    }
    REQUIRE(listener != nullptr);
    REQUIRE(rune::vm::isProbing(listener, rune::Name("HearNoise")));
    rune::Vector origin = rune::actor::Location(player);
    rune::Vector listenAt = origin + rune::Vector { 0, -300, 0 };
    REQUIRE(village.world.fastLineCheck(listenAt, origin));
    rune::actor::Location(listener) = listenAt;
    rune::pawn::Alertness(listener) = 0.4f;
    rune::pawn::HearingThreshold(listener) = 0.1f;
    rune::pawn::Stimulus(listener) = -1.0f;
    rune::UObject* silent = spawnClass(village.world, "RuneI.Bucket", origin);
    rune::actor::Instigator(silent) = nullptr;
    rune::vm::Call(silent, rune::Name("MakeNoise")).f(1.0f).run();
    CHECK(rune::pawn::Stimulus(listener) == -1.0f);

    rune::UObject* noisy = spawnClass(village.world, "RuneI.Bucket", origin);
    rune::actor::Instigator(noisy) = player;
    rune::pawn::noise1time(player) = -10.0f;
    rune::pawn::noise2time(player) = -10.0f;
    rune::vm::Call(noisy, rune::Name("MakeNoise")).f(1.0f).run();
    CHECK(rune::pawn::Stimulus(listener) == Catch::Approx(2.2f));
    CHECK(rune::pawn::noise1time(player) == village.world.timeSeconds);
    CHECK(rune::pawn::noise1loudness(player) == 1.0f);
    CHECK(rune::pawn::noise1spot(player).x == origin.x);

    rune::pawn::Stimulus(listener) = -1.0f;
    rune::vm::Call(noisy, rune::Name("MakeNoise")).f(1.0f).run();
    CHECK(rune::pawn::Stimulus(listener) == -1.0f);
}

TEST_CASE("FastTrace only tests world geometry and ignores movers", "[data]")
{
    MapWorld hel;
    rune::UObject* player = hel.placedPlayer();
    rune::Vector start;
    rune::Vector end;
    bool found = false;
    for (rune::UObject* a : hel.world.actors) {
        if (found || !a || a->destroyed || !rune::isMover(a)) {
            continue;
        }
        rune::Vector center = rune::actor::Location(a);
        for (const rune::Vector& axis : { rune::Vector { 96, 0, 0 }, rune::Vector { 0, 96, 0 } }) {
            for (float lift : { 0.0f, 32.0f, 64.0f, -32.0f, -64.0f }) {
                rune::Vector from = center - axis + rune::Vector { 0, 0, lift };
                rune::Vector to = center + axis + rune::Vector { 0, 0, lift };
                rune::CheckResult hit;
                if (!found && hel.world.fastLineCheck(to, from) && hel.world.lineCheck(hit, from, to, {}, nullptr, rune::TRACE_World) && hit.actor == a) {
                    start = from;
                    end = to;
                    found = true;
                }
            }
        }
    }
    REQUIRE(found);
    rune::vm::Call call(player, rune::Name("FastTrace"));
    REQUIRE(call.valid());
    call.v(end).v(start).run();
    CHECK(*reinterpret_cast<uint32_t*>(call.returnValue()) != 0);
}

TEST_CASE("PlayerCanSeeMe needs the actor within range and inside the player's view cone", "[data]")
{
    MapWorld hel;
    rune::UObject* player = hel.placedPlayer();
    rune::Vector origin = rune::actor::Location(player);
    REQUIRE(hel.world.fastLineCheck(origin + rune::Vector { 1300, 0, 0 }, origin));
    rune::pawn::ViewRotation(player) = {};
    rune::pawn::ViewLocation(player) = origin;
    rune::pawn::bBehindView(player) = 0;
    auto seen = [&](const rune::Vector& offset) {
        rune::UObject* target = spawnClass(hel.world, "RuneI.Bucket", origin + offset);
        rune::actor::CollisionRadius(target) = 10;
        rune::vm::Call call(target, rune::Name("PlayerCanSeeMe"));
        call.run();
        return *reinterpret_cast<uint32_t*>(call.returnValue()) != 0;
    };
    CHECK(seen({ 300, 0, 0 }));
    CHECK_FALSE(seen({ 1200, 0, 0 }));
    CHECK_FALSE(seen({ 0, 200, 0 }));
}

TEST_CASE("PlayerCanSeeMe measures from the cached ViewLocation and keeps the view cone in behind view", "[data]")
{
    MapWorld hel;
    rune::UObject* player = hel.placedPlayer();
    rune::Vector origin = rune::actor::Location(player);
    REQUIRE(hel.world.fastLineCheck(origin + rune::Vector { 1300, 0, 0 }, origin));
    rune::pawn::ViewRotation(player) = {};
    rune::pawn::bBehindView(player) = 1;
    auto seen = [&](const rune::Vector& offset) {
        rune::UObject* target = spawnClass(hel.world, "RuneI.Bucket", origin + offset);
        rune::actor::CollisionRadius(target) = 10;
        rune::vm::Call call(target, rune::Name("PlayerCanSeeMe"));
        call.run();
        return *reinterpret_cast<uint32_t*>(call.returnValue()) != 0;
    };
    rune::pawn::ViewLocation(player) = origin;
    CHECK(seen({ 300, 0, 0 }));
    CHECK_FALSE(seen({ 0, 200, 0 }));
    rune::pawn::ViewLocation(player) = origin + rune::Vector { 1000, 0, 0 };
    CHECK(seen({ 1300, 0, 0 }));
    rune::pawn::ViewLocation(player) = origin + rune::Vector { -1000, 0, 0 };
    CHECK_FALSE(seen({ 300, 0, 0 }));
}

TEST_CASE("Canvas natives follow the original cursor, size and hiding rules", "[data]")
{
    MapWorld hel;
    rune::UClass* canvasClass = rune::objects::findClass("Engine.Canvas");
    rune::UObject* font = rune::objects::loadObject("Engine.SmallFont", nullptr);
    rune::objects::flushLoads();
    REQUIRE(canvasClass != nullptr);
    REQUIRE(rune::cast<rune::UFont>(font) != nullptr);
    rune::UObject* canvas = rune::objects::newObject(canvasClass, rune::objects::packageObject(rune::Name("Transient")), rune::Name(), 0);
    rune::canvas::Font(canvas) = font;
    rune::canvas::ClipX(canvas) = 640;
    rune::canvas::ClipY(canvas) = 480;
    rune::canvas::SpaceX(canvas) = 3;
    rune::UObject* texture = hel.world.level->model->surfs[0].texture;
    REQUIRE(texture != nullptr);

    rune::canvas::Style(canvas) = 0;
    rune::canvas::CurX(canvas) = 5;
    rune::canvas::CurYL(canvas) = 0;
    rune::vm::Call(canvas, rune::Name("DrawText")).s("Hello").b(false).run();
    CHECK(rune::canvas::CurX(canvas) == 5.0f);
    CHECK(rune::canvas::CurYL(canvas) == 0.0f);

    rune::canvas::Style(canvas) = 1;
    rune::vm::Call(canvas, rune::Name("DrawTile")).obj(nullptr).f(32).f(16).f(0).f(0).f(32).f(16).run();
    CHECK(rune::canvas::CurX(canvas) == 5.0f);

    rune::canvas::CurX(canvas) = -10;
    rune::canvas::CurY(canvas) = 4;
    rune::vm::Call(canvas, rune::Name("DrawTileClipped")).obj(texture).f(50).f(20).f(0).f(0).f(50).f(20).run();
    CHECK(rune::canvas::CurX(canvas) == 43.0f);
    CHECK(rune::canvas::CurYL(canvas) == 20.0f);

    auto textSize = [&](const char* text) {
        rune::vm::Call call(canvas, rune::Name("TextSize"));
        call.s(text).f(-1).f(-1).run();
        return std::make_pair(*reinterpret_cast<float*>(call.parameter(1)), *reinterpret_cast<float*>(call.parameter(2)));
    };
    auto empty = textSize("");
    CHECK(empty.first == 0.0f);
    CHECK(empty.second == 0.0f);
    const rune::FontCharacter* a = rune::cast<rune::UFont>(font)->glyph('A', nullptr);
    const rune::FontCharacter* b = rune::cast<rune::UFont>(font)->glyph('B', nullptr);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    auto ab = textSize("AB");
    CHECK(ab.first == static_cast<float>(a->width + b->width));

    rune::canvas::CurX(canvas) = 10.7f;
    rune::canvas::CurY(canvas) = 3.9f;
    rune::vm::Call(canvas, rune::Name("StrLen")).s("Hi").f(0).f(0).run();
    CHECK(rune::canvas::CurX(canvas) == 10.0f);
    CHECK(rune::canvas::CurY(canvas) == 3.0f);

    rune::UObject* player = hel.world.player();
    REQUIRE(player != nullptr);
    rune::actor::bHidden(player) = 0;
    rune::vm::Call(canvas, rune::Name("DrawActor")).obj(player).b(false).b(false).run();
    CHECK(rune::actor::bHidden(player) != 0);
}

namespace {
    struct RecordedLine {
        float x1, y1, x2, y2;
        rune::LinearColor color;
    };

    struct LineRecorder : rune::Canvas2D {
        std::vector<RecordedLine> lines;

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

        void drawTile(rune::UTexture*, float, float, float, float, float, float, float, float, rune::LinearColor, rune::BlendStyle, bool) override
        {
        }

        void drawRect(float, float, float, float, rune::LinearColor, rune::BlendStyle) override
        {
        }

        void drawLine(float x1, float y1, float x2, float y2, rune::LinearColor color) override
        {
            lines.push_back({ x1, y1, x2, y2, color });
        }

        void drawImage(const uint8_t*, int, int, float, float, float, float) override
        {
        }
    };

    struct LineCanvas {
        LineRecorder recorder;
        rune::Canvas2D* previous = nullptr;
        rune::UObject* canvas = nullptr;

        LineCanvas()
        {
            rune::UClass* canvasClass = rune::objects::findClass("Engine.Canvas");
            REQUIRE(canvasClass != nullptr);
            canvas = rune::objects::newObject(canvasClass, rune::objects::packageObject(rune::Name("Transient")), rune::Name(), 0);
            rune::canvas::SizeX(canvas) = 640;
            rune::canvas::SizeY(canvas) = 480;
            rune::canvas::OrgX(canvas) = 50;
            rune::canvas::OrgY(canvas) = 60;
            rune::canvas::Style(canvas) = 1;
            rune::CanvasHooks& hooks = rune::canvasHooks();
            previous = hooks.target;
            hooks.target = &recorder;
            hooks.hasView = true;
            hooks.viewLocation = { 100, 200, 300 };
            hooks.viewRotation = {};
            hooks.viewFov = 90;
        }

        ~LineCanvas()
        {
            rune::canvasHooks().target = previous;
            rune::canvasHooks().hasView = false;
        }

        std::pair<int32_t, int32_t> transform(const rune::Vector& point)
        {
            rune::vm::Call call(canvas, rune::Name("TransformPoint"));
            REQUIRE(call.valid());
            call.v(point).i(7).i(9).run();
            return { *reinterpret_cast<int32_t*>(call.parameter(1)), *reinterpret_cast<int32_t*>(call.parameter(2)) };
        }
    };
}

TEST_CASE("DrawLine draws in viewport pixels ignoring the canvas origin and skips STY_None", "[data]")
{
    MapWorld hel;
    LineCanvas line;
    rune::vm::Call(line.canvas, rune::Name("DrawLine")).f(10).f(20).f(30).f(40).f(255).f(0).f(0.5f).run();
    REQUIRE(line.recorder.lines.size() == 1);
    CHECK(line.recorder.lines[0].x1 == 10.0f);
    CHECK(line.recorder.lines[0].y1 == 20.0f);
    CHECK(line.recorder.lines[0].x2 == 30.0f);
    CHECK(line.recorder.lines[0].y2 == 40.0f);
    CHECK(line.recorder.lines[0].color.r == 1.0f);
    CHECK(line.recorder.lines[0].color.g == 0.0f);
    CHECK(line.recorder.lines[0].color.b == 0.5f);
    rune::canvas::Style(line.canvas) = 0;
    rune::vm::Call(line.canvas, rune::Name("DrawLine")).f(10).f(20).f(30).f(40).f(255).f(0).f(0).run();
    CHECK(line.recorder.lines.size() == 1);
}

TEST_CASE("DrawLine3D projects through the view and clips at one unit in front of the camera", "[data]")
{
    MapWorld hel;
    LineCanvas line;
    rune::vm::Call(line.canvas, rune::Name("DrawLine3D")).v({ 200, 200, 300 }).v({ 200, 300, 200 }).f(0).f(255).f(0).run();
    REQUIRE(line.recorder.lines.size() == 1);
    CHECK(line.recorder.lines[0].x1 == Catch::Approx(320.0));
    CHECK(line.recorder.lines[0].y1 == Catch::Approx(240.0));
    CHECK(line.recorder.lines[0].x2 == Catch::Approx(640.0));
    CHECK(line.recorder.lines[0].y2 == Catch::Approx(560.0));
    CHECK(line.recorder.lines[0].color.g == 1.0f);
    line.recorder.lines.clear();
    rune::vm::Call(line.canvas, rune::Name("DrawLine3D")).v({ 0, 200, 300 }).v({ 50, 210, 300 }).f(1).f(1).f(1).run();
    CHECK(line.recorder.lines.empty());
    rune::vm::Call(line.canvas, rune::Name("DrawLine3D")).v({ 99, 200, 300 }).v({ 201, 200, 300 }).f(1).f(1).f(1).run();
    REQUIRE(line.recorder.lines.size() == 1);
    CHECK(line.recorder.lines[0].x1 == Catch::Approx(320.0));
    CHECK(line.recorder.lines[0].x2 == Catch::Approx(320.0));
    line.recorder.lines.clear();
    rune::canvasHooks().hasView = false;
    rune::vm::Call(line.canvas, rune::Name("DrawLine3D")).v({ 200, 200, 300 }).v({ 200, 300, 200 }).f(1).f(1).f(1).run();
    CHECK(line.recorder.lines.empty());
}

TEST_CASE("TransformPoint projects to pixels and reports -1000 for points behind the near plane", "[data]")
{
    MapWorld hel;
    LineCanvas line;
    CHECK(line.transform({ 200, 200, 300 }) == std::make_pair(320, 240));
    CHECK(line.transform({ 200, 250, 250 }) == std::make_pair(480, 400));
    CHECK(line.transform({ 200, 149.9f, 300 }) == std::make_pair(159, 240));
    CHECK(line.transform({ 101, 200, 300 }) == std::make_pair(-1000, -1000));
    rune::canvasHooks().hasView = false;
    CHECK(line.transform({ 200, 200, 300 }) == std::make_pair(7, 9));
}

TEST_CASE("ZoneActors includes actors in the level pseudo zone", "[data]")
{
    MapWorld hel;
    rune::UObject* zone = nullptr;
    for (rune::UObject* a : hel.world.actors) {
        if (!zone && a && !a->destroyed && a != hel.world.levelInfo && a->isA(rune::Name("ZoneInfo"))) {
            zone = a;
        }
    }
    REQUIRE(zone != nullptr);
    rune::UObject* player = hel.world.player();
    REQUIRE(player != nullptr);
    rune::UObject* loose = spawnClass(hel.world, "RuneI.Bucket", rune::actor::Location(player) + rune::Vector { 0, 0, 2000 });
    rune::UObject* elsewhere = spawnClass(hel.world, "RuneI.Bucket", rune::actor::Location(player) + rune::Vector { 0, 0, 2100 });
    rune::region(loose).zone = hel.world.levelInfo;
    rune::region(elsewhere).zone = nullptr;
    Bytecode zoneActors;
    zoneActors.native(308).object(loose->cls).member(zone, "Target").end();
    std::vector<rune::UObject*> found = runIterator(zone, zoneActors);
    CHECK(contains(found, loose));
    CHECK_FALSE(contains(found, elsewhere));
}

TEST_CASE("UpdateURL edits the current URL, GetAddressURL reports host and port and ClientTravel needs a Player", "[data]")
{
    MapWorld hel;
    rune::UObject* player = hel.placedPlayer();
    rune::vm::Call(player, rune::Name("UpdateURL")).s("Probe").s("One").b(false).run();
    rune::vm::Call(player, rune::Name("UpdateURL")).s("Probe").s("Two").b(false).run();
    const std::string& url = hel.world.url;
    CHECK(url.find("?Probe=Two") != std::string::npos);
    CHECK(url.find("Probe=One") == std::string::npos);
    CHECK(url.find("Probe=") == url.rfind("Probe="));

    rune::vm::Call address(hel.world.levelInfo, rune::Name("GetAddressURL"));
    REQUIRE(address.valid());
    address.run();
    CHECK(*reinterpret_cast<std::string*>(address.returnValue()) == ":7777");

    rune::UObject* viewport = rune::playerpawn::Player(player);
    REQUIRE(viewport != nullptr);
    rune::playerpawn::Player(player) = nullptr;
    rune::vm::Call(player, rune::Name("ClientTravel")).s("DM-Gates").byte(0).b(true).run();
    CHECK_FALSE(hel.world.travelPending);
    rune::playerpawn::Player(player) = viewport;
    rune::vm::Call(player, rune::Name("ClientTravel")).s("DM-Gates").byte(0).b(true).run();
    CHECK(hel.world.travelPending);
}

TEST_CASE("Warp and UnWarp transform location, velocity and rotation by the warp coordinates", "[data]")
{
    MapWorld hel;
    rune::UClass* warpClass = rune::objects::findClass("Engine.WarpZoneInfo");
    rune::objects::flushLoads();
    REQUIRE(warpClass != nullptr);
    rune::UObject* warp = rune::objects::newObject(warpClass, rune::objects::packageObject(rune::Name("Transient")), rune::Name(), 0);
    rune::warpzoneinfo::WarpCoords(warp) = { { 100, 0, 0 }, { 0, 1, 0 }, { -1, 0, 0 }, { 0, 0, 1 } };
    rune::actor::Location(warp) = { 10, 0, 0 };
    rune::actor::Velocity(warp) = { 1, 0, 0 };
    rune::actor::Rotation(warp) = {};
    Bytecode warpCall;
    warpCall.native(314).member(warp, "Location").member(warp, "Velocity").member(warp, "Rotation").end();
    runNative<uint32_t>(warp, warpCall);
    CHECK(rune::actor::Location(warp).x == Catch::Approx(100.0f));
    CHECK(rune::actor::Location(warp).y == Catch::Approx(10.0f));
    CHECK(rune::actor::Velocity(warp).x == Catch::Approx(0.0f).margin(1e-5));
    CHECK(rune::actor::Velocity(warp).y == Catch::Approx(1.0f));
    CHECK(std::abs(rune::actor::Rotation(warp).yaw - 16384) <= 4);
    Bytecode unwarpCall;
    unwarpCall.native(315).member(warp, "Location").member(warp, "Velocity").member(warp, "Rotation").end();
    runNative<uint32_t>(warp, unwarpCall);
    CHECK(rune::actor::Location(warp).x == Catch::Approx(10.0f));
    CHECK(rune::actor::Location(warp).y == Catch::Approx(0.0f).margin(1e-4));
    CHECK(rune::actor::Velocity(warp).x == Catch::Approx(1.0f));
    CHECK(std::abs(rune::actor::Rotation(warp).yaw) <= 4);
}

TEST_CASE("AutonomousPhysics truncates the acceleration to tenths and skips physics when there is none", "[data]")
{
    MapWorld hel;
    rune::UObject* player = hel.placedPlayer();
    rune::Vector spot = rune::actor::Location(player);
    rune::actor::Location(player) = spot + rune::Vector { 0, 0, 3000 };
    hel.world.collisionHash.update(player);
    rune::UObject* bucket = spawnClass(hel.world, "RuneI.Bucket", spot);
    rune::actor::Physics(bucket) = rune::PHYS_None;
    rune::actor::Velocity(bucket) = { 0, 0, -500 };
    rune::actor::Acceleration(bucket) = { 1.27f, -3.456f, 0.05f };
    rune::Vector before = rune::actor::Location(bucket);
    rune::vm::Call(bucket, rune::Name("AutonomousPhysics")).f(0.1f).run();
    CHECK(rune::actor::Acceleration(bucket).x == 1.2f);
    CHECK(rune::actor::Acceleration(bucket).y == -3.4f);
    CHECK(rune::actor::Acceleration(bucket).z == 0.0f);
    CHECK(rune::actor::Location(bucket).z == before.z);
    rune::actor::Physics(bucket) = rune::PHYS_Projectile;
    rune::vm::Call(bucket, rune::Name("AutonomousPhysics")).f(0.1f).run();
    REQUIRE_FALSE(bucket->destroyed);
    CHECK(rune::actor::Location(bucket).z < before.z);
}

TEST_CASE("Actor.Error logs and destroys the actor", "[data]")
{
    MapWorld hel;
    rune::UObject* player = hel.placedPlayer();
    rune::UObject* bucket = spawnClass(hel.world, "RuneI.Bucket", rune::actor::Location(player) + rune::Vector { 0, 0, 2000 });
    rune::vm::Call(bucket, rune::Name("Error")).s("test").run();
    CHECK(bucket->destroyed);
}

TEST_CASE("GetMapName lists only run files in case insensitive order and wraps at both ends", "[data]")
{
    MapWorld hel;
    rune::UObject* player = hel.placedPlayer();
    auto next = [&](const char* current, int32_t direction) {
        rune::vm::Call call(player, rune::Name("GetMapName"));
        REQUIRE(call.valid());
        call.s("DM-").s(current).i(direction).run();
        return *reinterpret_cast<std::string*>(call.returnValue());
    };
    CHECK(next("DM-Gates.run", 1) == "DM-geothermal.run");
    CHECK(next("DM-geothermal.run", 1) == "DM-Havlamahl.run");
    CHECK(next("DM-Sanctum.run", 1) == "DM-seaside.run");
    CHECK(next("DM-Thorstadt.run", 1) == "DM-Zorn.run");
    CHECK(next("DM-Zorn.run", 1) == "DM-AcidChamber.run");
    CHECK(next("DM-AcidChamber.run", -1) == "DM-Zorn.run");
    CHECK(next("dm-hel.run", 1) == "DM-AcidChamber.run");
    CHECK(next("DM-Team_chasm.run", 1) == "DM-TEAM_MadKing.run");
}

TEST_CASE("GetNextSkin walks the registered textures with the prefix and wraps forward to the first", "[data]")
{
    MapWorld hel;
    rune::UObject* player = hel.placedPlayer();
    std::filesystem::path file = std::filesystem::temp_directory_path() / "rune_skin_registry.int";
    {
        std::ofstream out(file);
        out << "[Public]\n";
        out << "Object=(Name=Skins.RagnarRed,Class=Texture,Description=\"Red\")\n";
        out << "Object=(Name=Other.Thing,Class=Texture,Description=\"Thing\")\n";
        out << "Object=(Name=Skins.RagnarBlue,Class=Texture,Description=\"Blue\")\n";
        out << "Object=(Name=Skins.RagnarGold,Class=Sound,Description=\"Gold\")\n";
        out << "Object=(Name=Skins.RagnarBlack,Class=Texture,Description=\"Black\")\n";
    }
    rune::Ini registry;
    REQUIRE(registry.load(file));
    rune::Ini* window = rune::config::localization("Window");
    REQUIRE(window != nullptr);
    rune::Ini saved = *window;
    *window = registry;
    auto skin = [&](const char* current, int32_t direction) {
        rune::vm::Call call(player, rune::Name("GetNextSkin"));
        REQUIRE(call.valid());
        call.s("Skins.Ragnar").s(current).i(direction);
        std::string* name = reinterpret_cast<std::string*>(call.parameter(3));
        std::string* description = reinterpret_cast<std::string*>(call.parameter(4));
        call.skip().skip().run();
        return *name + "|" + *description;
    };
    CHECK(skin("Skins.RagnarRed", 1) == "Skins.RagnarBlue|Blue");
    CHECK(skin("Skins.RagnarBlue", 1) == "Skins.RagnarBlack|Black");
    CHECK(skin("Skins.RagnarBlack", 1) == "Skins.RagnarRed|Red");
    CHECK(skin("Skins.RagnarBlue", -1) == "Skins.RagnarRed|Red");
    CHECK(skin("Skins.RagnarRed", -1) == "|");
    CHECK(skin("skins.ragnarblue", 0) == "Skins.RagnarBlue|Blue");
    CHECK(skin("Skins.RagnarGold", 0) == "Skins.RagnarBlack|Black");
    *window = saved;
    std::filesystem::remove(file);
}

namespace {
    struct SweepRecord {
        rune::UObject* actor = nullptr;
        int32_t low = 0;
        int32_t high = 0;
    };

    std::vector<SweepRecord> sweepRecords;
    rune::UObject* sweepSpawnClass = nullptr;
    rune::Vector sweepSpawnAt;

    void recordSweep(rune::UObject* self, rune::Frame& stack, void*)
    {
        rune::vm::finish(stack);
        sweepRecords.push_back({ rune::actor::Target(self), rune::pawn::Health(self), *reinterpret_cast<int32_t*>(self->propertyAddress(rune::Name("MaxHealth"))) });
        if (sweepSpawnClass) {
            rune::UClass* cls = rune::cast<rune::UClass>(sweepSpawnClass);
            sweepSpawnClass = nullptr;
            rune::UObject* spawned = rune::World::current->spawn(cls, nullptr, rune::Name(), sweepSpawnAt, {}, nullptr, true);
            REQUIRE(spawned != nullptr);
            rune::actor::CollisionRadius(spawned) = 10.0f;
            rune::actor::CollisionHeight(spawned) = 5.0f;
            rune::World::current->collisionHash.update(spawned);
        }
    }
}

TEST_CASE("SweepActors traces each step when the previous hits run out and reports level hits with no high joint mask", "[data]")
{
    MapWorld hel;
    rune::UObject* player = hel.placedPlayer();
    rune::Vector feet = rune::actor::Location(player) - rune::Vector { 0, 0, rune::actor::CollisionHeight(player) };
    rune::Vector start1 = feet + rune::Vector { 200, 0, 30 };
    rune::Vector stop1 = feet + rune::Vector { 200, 0, -30 };
    rune::Vector start2 = start1 + rune::Vector { 100, 0, 0 };
    rune::Vector stop2 = stop1 + rune::Vector { 100, 0, 0 };
    rune::UClass* trigger = rune::objects::findClass("Engine.Trigger");
    rune::objects::flushLoads();
    REQUIRE(trigger != nullptr);
    rune::vm::registerNative(4094, recordSweep);
    sweepRecords.clear();
    sweepSpawnClass = trigger;
    sweepSpawnAt = start1 + rune::Vector { 70, 0, -15 };
    rune::UObject* endpoint = hel.world.spawn(trigger, nullptr, rune::Name(), start2 + rune::Vector { 3.9f, 0, -15 }, {}, nullptr, true);
    REQUIRE(endpoint != nullptr);
    rune::actor::CollisionRadius(endpoint) = 0.1f;
    rune::actor::CollisionHeight(endpoint) = 1.0f;
    hel.world.collisionHash.update(endpoint);
    Bytecode call;
    call.native(313).object(trigger).member(player, "Target").v(start1).v(stop1).v(start2).v(stop2).f(4.0f);
    call.member(player, "OldLocation").member(player, "Acceleration").member(player, "Health").member(player, "MaxHealth").end();
    Bytecode script;
    script.byte(0x2F);
    script.code.insert(script.code.end(), call.code.begin(), call.code.end());
    size_t endAt = script.code.size() + 2 + 2 + 1 + 1;
    script.word(static_cast<uint32_t>(endAt), 2);
    script.native(4094).end().byte(0x31).byte(0x30);
    runNative<uint32_t>(player, script);
    REQUIRE_FALSE(sweepRecords.empty());
    CHECK(sweepRecords.front().actor == hel.world.levelInfo);
    CHECK(sweepRecords.front().low == 199);
    CHECK(sweepRecords.front().high == -1);
    int triggers = 0;
    for (const SweepRecord& record : sweepRecords) {
        if (record.actor && record.actor->cls == trigger) {
            triggers++;
            CHECK(record.low == 0);
            CHECK(record.high == 0);
        }
    }
    CHECK(triggers == 8);
    CHECK(std::any_of(sweepRecords.begin(), sweepRecords.end(), [&](const SweepRecord& record) { return record.actor == endpoint; }));
}

TEST_CASE("Enable cannot turn on an event the current state ignores", "[data]")
{
    MapWorld hel;
    rune::UObject* player = hel.placedPlayer();
    rune::UObject* cuttle = spawnClass(hel.world, "RuneI.Cuttle", rune::actor::Location(player) + rune::Vector { 0, 0, 300 });
    REQUIRE(rune::vm::gotoState(cuttle, rune::Name("FreeRoaming")));
    CHECK_FALSE(rune::vm::isProbing(cuttle, rune::Name("SeePlayer")));
    rune::vm::Call(cuttle, rune::Name("Enable")).n(rune::Name("SeePlayer")).run();
    CHECK_FALSE(rune::vm::isProbing(cuttle, rune::Name("SeePlayer")));
    rune::vm::Call(cuttle, rune::Name("Disable")).n(rune::Name("Timer")).run();
    CHECK_FALSE(rune::vm::isProbing(cuttle, rune::Name("Timer")));
    rune::vm::Call(cuttle, rune::Name("Enable")).n(rune::Name("Timer")).run();
    CHECK(rune::vm::isProbing(cuttle, rune::Name("Timer")));
}

TEST_CASE("GetStateName and IsInState use the class when no state is active", "[data]")
{
    MapWorld hel;
    rune::UObject* player = hel.placedPlayer();
    rune::UObject* bucket = spawnClass(hel.world, "RuneI.Bucket", rune::actor::Location(player) + rune::Vector { 0, 0, 300 });
    rune::vm::gotoState(bucket, rune::Name());
    rune::vm::Call name(bucket, rune::Name("GetStateName"));
    REQUIRE(name.valid());
    name.run();
    CHECK(*reinterpret_cast<rune::Name*>(name.returnValue()) == rune::Name("Bucket"));
    auto inState = [&](const char* state) {
        rune::vm::Call call(bucket, rune::Name("IsInState"));
        REQUIRE(call.valid());
        call.n(rune::Name(state)).run();
        return *reinterpret_cast<uint32_t*>(call.returnValue()) != 0;
    };
    CHECK(inState("Bucket"));
    CHECK(inState("Actor"));
    CHECK_FALSE(inState("Pawn"));
}

TEST_CASE("the vector to rotator cast truncates a signed angle scaled by 65535 over two pi", "[data]")
{
    MapWorld hel;
    rune::UObject* player = hel.placedPlayer();
    auto rotation = [&](const rune::Vector& value) {
        Bytecode cast;
        cast.byte(0x50).v(value);
        return runNative<rune::Rotator>(player, cast);
    };
    rune::Rotator diagonal = rotation({ 1, 1, 0 });
    CHECK(diagonal.yaw == 8191);
    CHECK(diagonal.pitch == 0);
    rune::Rotator back = rotation({ -1, -1, 0 });
    CHECK(back.yaw == -24575);
    rune::Rotator down = rotation({ 1, 0, -1 });
    CHECK(down.yaw == 0);
    CHECK(down.pitch == -8191);
    CHECK(down.roll == 0);
}

TEST_CASE("GotoLabel always clears the latent action and stops state code when the label is missing", "[data]")
{
    MapWorld hel;
    rune::UObject* player = hel.placedPlayer();
    rune::UObject* cuttle = spawnClass(hel.world, "RuneI.Cuttle", rune::actor::Location(player) + rune::Vector { 0, 0, 300 });
    REQUIRE(rune::vm::gotoState(cuttle, rune::Name("FreeRoaming")));
    rune::StateFrame& frame = rune::vm::stateFrame(cuttle);
    REQUIRE(frame.code != nullptr);
    frame.latentAction = 384;
    CHECK(rune::vm::gotoLabel(cuttle, rune::Name("Begin")));
    CHECK(frame.latentAction == 0);
    CHECK(frame.code != nullptr);
    frame.latentAction = 384;
    CHECK_FALSE(rune::vm::gotoLabel(cuttle, rune::Name("NoSuchLabel")));
    CHECK(frame.latentAction == 0);
    CHECK(frame.code == nullptr);
}

TEST_CASE("Rand, FRand and RotRand draw from the C runtime rand sequence", "[data]")
{
    MapWorld hel;
    rune::UObject* player = hel.placedPlayer();
    rune::random::seed(1);
    Bytecode rand;
    rand.native(167).byte(0x1D).word(100, 4).end();
    CHECK(runNative<int32_t>(player, rand) == 41);
    Bytecode frand;
    frand.native(195).end();
    CHECK(runNative<float>(player, frand) == static_cast<float>(18467.0 / 32767.0));
    Bytecode rotRand;
    rotRand.native(320).end();
    rune::Rotator rotation = runNative<rune::Rotator>(player, rotRand);
    CHECK(rotation.yaw == (2 * 6334) % 65535);
    CHECK(rotation.pitch == (2 * 26500) % 65535);
    CHECK(rotation.roll == 0);
}

TEST_CASE("division and modulo by zero, square roots of negatives and unknown names behave like the original", "[data]")
{
    MapWorld hel;
    rune::UObject* player = hel.placedPlayer();
    rune::pawn::Health(player) = 100;
    Bytecode intDivide;
    intDivide.native(160).member(player, "Health").f(0.0f).end();
    CHECK(runNative<int32_t>(player, intDivide) == 0);
    CHECK(rune::pawn::Health(player) == 0);
    rune::pawn::Health(player) = 7;
    Bytecode intHalf;
    intHalf.native(160).member(player, "Health").f(2.0f).end();
    CHECK(runNative<int32_t>(player, intHalf) == 3);
    rune::actor::AmbientGlow(player) = 9;
    Bytecode byteDivide;
    byteDivide.native(134).member(player, "AmbientGlow").byte(0x24).byte(0).end();
    CHECK(runNative<uint8_t>(player, byteDivide) == 0);
    CHECK(rune::actor::AmbientGlow(player) == 9);
    Bytecode modulo;
    modulo.native(173).f(5.0f).f(0.0f).end();
    CHECK(std::isnan(runNative<float>(player, modulo)));
    Bytecode root;
    root.native(193).f(-4.0f).end();
    CHECK(std::isnan(runNative<float>(player, root)));
    auto toName = [&](const std::string& text) {
        Bytecode cast;
        cast.byte(0x5A).byte(0x1F);
        for (char c : text) {
            cast.byte(static_cast<uint8_t>(c));
        }
        cast.byte(0);
        return runNative<rune::Name>(player, cast);
    };
    CHECK(toName("Begin") == rune::Name("Begin"));
    CHECK(toName("NoSuchNameAnywhere7731").none());
}

TEST_CASE("GetPropertyText and SetPropertyText skip private and const properties", "[data]")
{
    MapWorld hel;
    rune::UObject* player = hel.placedPlayer();
    auto setText = [&](rune::UObject* target, const char* name, const char* value) {
        rune::vm::Call call(target, rune::Name("SetPropertyText"));
        REQUIRE(call.valid());
        call.s(name).s(value).run();
    };
    auto getText = [&](rune::UObject* target, const char* name) {
        rune::vm::Call call(target, rune::Name("GetPropertyText"));
        REQUIRE(call.valid());
        call.s(name).run();
        return *reinterpret_cast<std::string*>(call.returnValue());
    };
    rune::actor::LifeSpan(player) = 0;
    setText(player, "LifeSpan", "5.5");
    CHECK(rune::actor::LifeSpan(player) == 5.5f);
    float* counter = reinterpret_cast<float*>(player->propertyAddress(rune::Name("TimerCounter")));
    REQUIRE(counter != nullptr);
    *counter = 1.0f;
    setText(player, "TimerCounter", "9");
    CHECK(*counter == 1.0f);
    CHECK(getText(player, "TimerCounter") == "1.000000");
    rune::UObject* console = rune::objects::newObject(rune::objects::findClass("Engine.Console"), rune::objects::packageObject(rune::Name("Transient")), rune::Name(), 0);
    REQUIRE(console != nullptr);
    *reinterpret_cast<int32_t*>(console->propertyAddress(rune::Name("vtblOut"))) = 42;
    CHECK(getText(console, "vtblOut").empty());
}

TEST_CASE("Invert returns the general inverse of a non orthogonal basis", "[data]")
{
    MapWorld hel;
    rune::UObject* player = hel.placedPlayer();
    rune::actor::Location(player) = { 2, 0, 0 };
    rune::actor::Velocity(player) = { 0, 4, 0 };
    rune::actor::Acceleration(player) = { 1, 0, 1 };
    Bytecode invert;
    invert.native(227).member(player, "Location").member(player, "Velocity").member(player, "Acceleration").end();
    runNative<uint32_t>(player, invert);
    CHECK(rune::actor::Location(player).x == 0.5f);
    CHECK(rune::actor::Location(player).y == 0.0f);
    CHECK(rune::actor::Location(player).z == 0.0f);
    CHECK(rune::actor::Velocity(player).x == 0.0f);
    CHECK(rune::actor::Velocity(player).y == 0.25f);
    CHECK(rune::actor::Velocity(player).z == 0.0f);
    CHECK(rune::actor::Acceleration(player).x == -0.5f);
    CHECK(rune::actor::Acceleration(player).y == 0.0f);
    CHECK(rune::actor::Acceleration(player).z == 1.0f);
}

TEST_CASE("Console.ConsoleCommand reports unrecognized commands as unhandled", "[data]")
{
    MapWorld hel;
    hel.placedPlayer();
    rune::UObject* console = rune::objects::newObject(rune::objects::findClass("Engine.Console"), rune::objects::packageObject(rune::Name("Transient")), rune::Name(), 0);
    REQUIRE(console != nullptr);
    auto command = [&](const char* text) {
        rune::vm::Call call(console, rune::Name("ConsoleCommand"));
        REQUIRE(call.valid());
        call.s(text).run();
        return *reinterpret_cast<uint32_t*>(call.returnValue()) != 0;
    };
    CHECK_FALSE(command("NoSuchCommandAnywhere"));
    CHECK(command("GetCurrentRes"));
}

TEST_CASE("ParseKillMessage replaces only the first of each token", "[data]")
{
    MapWorld hel;
    hel.placedPlayer();
    rune::UObject* game = hel.world.gameInfo;
    REQUIRE(game != nullptr);
    rune::vm::Call call(game, rune::Name("ParseKillMessage"));
    REQUIRE(call.valid());
    call.s("Ragnar").s("Sark").s("Axe").s("%k split %o with %w, %k again").run();
    CHECK(*reinterpret_cast<std::string*>(call.returnValue()) == "Ragnar split Sark with Axe, %k again");
}

TEST_CASE("DrawClippedActor renders into the given frame with the current FOV and hides the actor again", "[data]")
{
    MapWorld hel;
    rune::UObject* player = hel.placedPlayer();
    rune::UObject* bucket = spawnClass(hel.world, "RuneI.Bucket", rune::actor::Location(player) + rune::Vector { 0, 0, 300 });
    rune::UObject* canvas = rune::objects::newObject(rune::objects::findClass("Engine.Canvas"), rune::objects::packageObject(rune::Name("Transient")), rune::Name(), 0);
    REQUIRE(canvas != nullptr);
    rune::pawn::FovAngle(player) = 30.0f;
    rune::ActorFrame seen;
    bool hiddenDuringDraw = true;
    bool wire = true;
    bool clear = false;
    auto saved = rune::canvasHooks().drawActor;
    rune::canvasHooks().drawActor = [&](rune::UObject* actor, bool wireframe, bool clearZ, const rune::ActorFrame& frame) {
        seen = frame;
        wire = wireframe;
        clear = clearZ;
        hiddenDuringDraw = rune::actor::bHidden(actor) != 0;
    };
    rune::vm::Call call(canvas, rune::Name("DrawClippedActor"));
    REQUIRE(call.valid());
    call.obj(bucket).b(false).i(320).i(736).i(12).i(34).b(true).run();
    rune::canvasHooks().drawActor = saved;
    CHECK(seen.clipped);
    CHECK(seen.width == 320);
    CHECK(seen.height == 736);
    CHECK(seen.x == 12);
    CHECK(seen.y == 34);
    CHECK(seen.fov == 30.0f);
    CHECK_FALSE(wire);
    CHECK(clear);
    CHECK_FALSE(hiddenDuringDraw);
    CHECK(rune::actor::bHidden(bucket) != 0);
}

TEST_CASE("teammates hear noises by range alone and clients make no noise", "[data]")
{
    MapWorld village("RagnarVillage2");
    rune::UObject* player = village.world.player();
    REQUIRE(player != nullptr);
    rune::UObject* listener = nullptr;
    for (rune::UObject* p = rune::levelinfo::PawnList(village.world.levelInfo); p; p = rune::pawn::nextPawn(p)) {
        if (p != player && !rune::isPlayerPawn(p) && p->name == rune::Name("Sigurd0")) {
            listener = p;
        }
    }
    REQUIRE(listener != nullptr);
    rune::Vector origin = rune::actor::Location(player);
    rune::Vector listenAt = origin + rune::Vector { 0, -300, 0 };
    REQUIRE(village.world.fastLineCheck(listenAt, origin));
    rune::actor::Location(listener) = listenAt;
    rune::pawn::Alertness(listener) = 0.4f;
    rune::pawn::HearingThreshold(listener) = 0.1f;
    rune::UObject* game = rune::levelinfo::Game(village.world.levelInfo);
    REQUIRE(game != nullptr);
    rune::UObject* playerReplication = rune::pawn::PlayerReplicationInfo(player);
    REQUIRE(playerReplication != nullptr);
    rune::UObject* listenerReplication = spawnClass(village.world, "Engine.PlayerReplicationInfo", listenAt);
    REQUIRE(listenerReplication != nullptr);
    rune::pawn::PlayerReplicationInfo(listener) = listenerReplication;
    rune::pawn::bIsPlayer(listener) = 1;
    rune::gameinfo::bTeamGame(game) = 1;
    rune::playerreplicationinfo::Team(listenerReplication) = rune::playerreplicationinfo::Team(playerReplication);

    rune::UObject* noisy = spawnClass(village.world, "RuneI.Bucket", origin);
    rune::actor::Instigator(noisy) = player;
    rune::pawn::noise1time(player) = -10.0f;
    rune::pawn::noise2time(player) = -10.0f;
    rune::pawn::Stimulus(listener) = -1.0f;
    rune::vm::Call(noisy, rune::Name("MakeNoise")).f(1.0f).run();
    CHECK(rune::pawn::Stimulus(listener) == -1.0f);
    CHECK(rune::pawn::noise1time(player) == village.world.timeSeconds);

    rune::gameinfo::bTeamGame(game) = 0;
    rune::pawn::noise1time(player) = -10.0f;
    rune::pawn::noise2time(player) = -10.0f;
    rune::levelinfo::NetMode(village.world.levelInfo) = rune::net::NM_Client;
    rune::vm::Call(noisy, rune::Name("MakeNoise")).f(1.0f).run();
    CHECK(rune::pawn::Stimulus(listener) == -1.0f);
    CHECK(rune::pawn::noise1time(player) == -10.0f);
    rune::levelinfo::NetMode(village.world.levelInfo) = rune::net::NM_Standalone;
    rune::vm::Call(noisy, rune::Name("MakeNoise")).f(1.0f).run();
    CHECK(rune::pawn::Stimulus(listener) == Catch::Approx(2.2f));
}

TEST_CASE("PlaySound returns the actor index times 16 plus twice the slot plus bNoOverride even when nobody hears it", "[data]")
{
    MapWorld hel;
    REQUIRE(rune::audio::initializeOffline(44100));
    rune::UObject* player = hel.placedPlayer();
    rune::Vector origin = rune::actor::Location(player);
    rune::USound* sound = loadSound("WeaponsSnd.ImpCrashes.crashxbucket01");
    rune::UObject* bucket = spawnClass(hel.world, "RuneI.Bucket", origin + rune::Vector { 5000, 0, 0 });
    rune::audio::update(origin, {}, 0.0f);
    Bytecode call;
    call.native(264).object(sound).byte(0x24).byte(3).omit().byte(0x27).f(10.0f).end();
    int32_t id = runNative<int32_t>(bucket, call);
    CHECK(voicesOf(bucket).empty());
    CHECK(id == static_cast<int32_t>(bucket->handle * 16 + 3 * 2 + 1));
    rune::audio::shutdown();
}

namespace {
    std::vector<rune::UObject*> clientHearers;

    void recordClientHearSound(rune::UObject* self, rune::Frame& stack, void*)
    {
        rune::vm::get<rune::UObject*>(stack);
        rune::vm::get<int32_t>(stack);
        rune::vm::get<rune::UObject*>(stack);
        rune::vm::get<rune::Vector>(stack);
        rune::vm::get<rune::Vector>(stack);
        rune::vm::finish(stack);
        clientHearers.push_back(self);
    }
}

TEST_CASE("PlayOwnedSound on a server skips the remote player that owns the sound", "[data]")
{
    MapWorld hel;
    rune::UObject* player = hel.placedPlayer();
    rune::Vector origin = rune::actor::Location(player);
    rune::USound* sound = loadSound("WeaponsSnd.ImpCrashes.crashxbucket01");
    rune::Vector spot = origin + rune::Vector { 100, 0, 0 };
    REQUIRE(hel.world.fastLineCheck(spot, origin));
    rune::UObject* remote = spawnClass(hel.world, "RuneI.Ragnar", spot);
    rune::UObject* connection = rune::objects::newObject(rune::objects::intrinsicClass(rune::Name("NetConnection")), rune::objects::packageObject(rune::Name("Transient")), rune::Name(), 0);
    REQUIRE(connection != nullptr);
    rune::playerpawn::Player(remote) = connection;
    rune::pawn::bIsPlayer(remote) = 1;
    rune::UObject* ownedItem = spawnClass(hel.world, "RuneI.Bucket", spot);
    rune::actor::Owner(ownedItem) = remote;
    rune::UFunction* function = rune::vm::findFunction(remote, rune::Name("ClientHearSound"));
    REQUIRE(function != nullptr);
    auto previous = function->native;
    function->native = recordClientHearSound;
    hel.world.netMode = rune::net::NM_ListenServer;
    clientHearers.clear();
    rune::vm::Call(ownedItem, rune::Name("PlaySound")).obj(sound).run();
    CHECK(std::count(clientHearers.begin(), clientHearers.end(), remote) == 1);
    clientHearers.clear();
    rune::vm::Call(ownedItem, rune::Name("PlayOwnedSound")).obj(sound).run();
    CHECK(std::count(clientHearers.begin(), clientHearers.end(), remote) == 0);
    clientHearers.clear();
    rune::vm::Call(remote, rune::Name("PlayOwnedSound")).obj(sound).run();
    CHECK(std::count(clientHearers.begin(), clientHearers.end(), remote) == 0);
    hel.world.netMode = rune::net::NM_Standalone;
    function->native = previous;
    rune::playerpawn::Player(remote) = nullptr;
    rune::objects::destroyObject(connection);
}

namespace {
    std::string webSent;

    void recordWebSend(rune::UObject*, rune::Frame& stack, void* result)
    {
        webSent += rune::vm::get<std::string>(stack);
        rune::vm::finish(stack);
        rune::vm::ret<int32_t>(result, 0);
    }
}

TEST_CASE("UWeb keeps the newest variable first, Subst with bClear empties the map and includes refuse parent paths", "[data]")
{
    MapWorld hel;
    rune::UClass* requestClass = rune::objects::findClass("UWeb.WebRequest");
    rune::UClass* responseClass = rune::objects::findClass("UWeb.WebResponse");
    rune::objects::flushLoads();
    REQUIRE(requestClass != nullptr);
    REQUIRE(responseClass != nullptr);
    rune::UObject* request = rune::objects::newObject(requestClass, rune::objects::packageObject(rune::Name("Transient")), rune::Name(), 0);
    rune::vm::Call(request, rune::Name("AddVariable")).s("Map").s("first").run();
    rune::vm::Call(request, rune::Name("AddVariable")).s("map").s("second").run();
    rune::vm::Call get(request, rune::Name("GetVariable"));
    get.s("MAP").run();
    CHECK(*reinterpret_cast<std::string*>(get.returnValue()) == "second");
    rune::vm::Call number(request, rune::Name("GetVariableNumber"));
    number.s("map").i(1).run();
    CHECK(*reinterpret_cast<std::string*>(number.returnValue()) == "first");

    std::filesystem::path directory = std::filesystem::temp_directory_path() / "rune-uweb-test" / "Web";
    std::filesystem::create_directories(directory);
    {
        std::ofstream page(directory / "page.uhtm", std::ios::binary);
        page << "[%a%|%b%]";
        std::ofstream secret(directory.parent_path() / "secret.uhtm", std::ios::binary);
        secret << "secret";
    }
    rune::UObject* response = rune::objects::newObject(responseClass, rune::objects::packageObject(rune::Name("Transient")), rune::Name(), 0);
    *reinterpret_cast<std::string*>(response->propertyAddress(rune::Name("IncludePath"))) = directory.string();
    rune::UObject* connection = spawnClass(hel.world, "UWeb.WebConnection", rune::actor::Location(hel.placedPlayer()));
    *reinterpret_cast<rune::UObject**>(response->propertyAddress(rune::Name("Connection"))) = connection;
    rune::UFunction* send = rune::vm::findFunction(connection, rune::Name("SendText"));
    REQUIRE(send != nullptr);
    auto previous = send->native;
    send->native = recordWebSend;
    rune::vm::Call(response, rune::Name("Subst")).s("a").s("1").run();
    rune::vm::Call(response, rune::Name("Subst")).s("b").s("2").b(true).run();
    webSent.clear();
    rune::vm::Call(response, rune::Name("IncludeUHTM")).s("page.uhtm").run();
    REQUIRE(webSent.size() >= 5);
    CHECK(webSent.substr(webSent.size() - 5) == "\n[|2]");
    webSent.clear();
    rune::vm::Call(response, rune::Name("IncludeUHTM")).s("../secret.uhtm").run();
    CHECK(webSent.find("secret") == std::string::npos);
    send->native = previous;
    std::filesystem::remove_all(directory.parent_path());
}

TEST_CASE("closing a listening TcpLink pauses it and Listen resumes accepting on the same socket", "[data]")
{
    MapWorld hel;
    rune::UObject* link = spawnClass(hel.world, "IpDrv.TcpLink", rune::actor::Location(hel.placedPlayer()));
    uint8_t* state = link->propertyAddress(rune::Name("LinkState"));
    REQUIRE(state != nullptr);
    rune::vm::Call bind(link, rune::Name("BindPort"));
    bind.i(0).run();
    int32_t port = *reinterpret_cast<int32_t*>(bind.returnValue());
    REQUIRE(port > 0);
    rune::vm::Call listen(link, rune::Name("Listen"));
    listen.run();
    REQUIRE(*state == 2);
    rune::vm::Call close(link, rune::Name("Close"));
    close.run();
    CHECK(*state == 7);
    rune::vm::Call again(link, rune::Name("Listen"));
    again.run();
    CHECK(*reinterpret_cast<uint32_t*>(again.returnValue()) != 0);
    CHECK(*state == 2);
    rune::net::Socket client;
    REQUIRE(client.open(rune::net::Socket::Kind::Tcp));
    client.connect({ 0x7f000001, static_cast<uint16_t>(port) });
    for (int i = 0; i < 100 && *state == 2; i++) {
        rune::ipdrv::tick(hel.world);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(*state == 4);
}
