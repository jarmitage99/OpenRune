#include "audio/audio.h"
#include "audio/module.h"
#include "core/config.h"
#include "core/objects.h"
#include "core/vm.h"
#include "engine/actor.h"
#include "engine/console.h"
#include "engine/props_engine.h"
#include "engine/package.h"
#include "engine/resources.h"
#include "engine/world.h"

#include <catch_amalgamated.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace {
    struct AudioDevice {
        std::optional<std::string> saved;

        explicit AudioDevice(const char* device)
        {
            rune::engine::initialize(RUNE_GAME_DATA);
            saved = rune::config::system().get("Engine.Engine", "AudioDevice");
            rune::config::system().set("Engine.Engine", "AudioDevice", device);
        }

        ~AudioDevice()
        {
            rune::config::system().set("Engine.Engine", "AudioDevice", saved.value_or(""));
        }
    };

    std::vector<uint8_t> resourceBytes(const std::string& path, const char* className, const char* objectName)
    {
        rune::Package package(path);
        int32_t ref = package.findExport(className, objectName);
        REQUIRE(ref != 0);
        rune::Reader reader = package.load(ref).native();
        reader.index();
        reader.u32();
        int32_t size = reader.index();
        std::vector<uint8_t> bytes(static_cast<size_t>(size));
        reader.bytes(bytes.data(), bytes.size());
        return bytes;
    }

    std::vector<uint8_t> loopedHumBytes()
    {
        return resourceBytes(RUNE_GAME_DATA "/Sounds/EnvironmentalSnd.uax", "Sound", "machinehum01L");
    }

    std::vector<uint8_t> shieldHitBytes()
    {
        return resourceBytes(RUNE_GAME_DATA "/Sounds/WeaponsSnd.uax", "Sound", "shield03");
    }

    std::vector<uint8_t> sampleWave(const std::vector<int16_t>& samples, uint32_t rate)
    {
        uint32_t frames = static_cast<uint32_t>(samples.size());
        std::vector<uint8_t> bytes;
        auto u32 = [&bytes](uint32_t v) {
            for (int i = 0; i < 4; i++) {
                bytes.push_back(static_cast<uint8_t>(v >> (8 * i)));
            }
        };
        auto u16 = [&bytes](uint16_t v) {
            bytes.push_back(static_cast<uint8_t>(v));
            bytes.push_back(static_cast<uint8_t>(v >> 8));
        };
        bytes.insert(bytes.end(), { 'R', 'I', 'F', 'F' });
        u32(36 + frames * 2);
        bytes.insert(bytes.end(), { 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ' });
        u32(16);
        u16(1);
        u16(1);
        u32(rate);
        u32(rate * 2);
        u16(2);
        u16(16);
        bytes.insert(bytes.end(), { 'd', 'a', 't', 'a' });
        u32(frames * 2);
        for (int16_t sample : samples) {
            u16(static_cast<uint16_t>(sample));
        }
        return bytes;
    }

    std::vector<uint8_t> loopWave(const std::vector<int16_t>& samples, uint32_t rate, uint32_t start, uint32_t last)
    {
        std::vector<uint8_t> bytes = sampleWave(samples, rate);
        std::vector<uint32_t> chunk(15, 0);
        chunk[7] = 1;
        chunk[11] = start;
        chunk[12] = last;
        bytes.insert(bytes.end(), { 's', 'm', 'p', 'l', 60, 0, 0, 0 });
        for (uint32_t value : chunk) {
            for (int i = 0; i < 4; i++) {
                bytes.push_back(static_cast<uint8_t>(value >> (8 * i)));
            }
        }
        uint32_t riff = static_cast<uint32_t>(bytes.size() - 8);
        for (int i = 0; i < 4; i++) {
            bytes[4 + static_cast<size_t>(i)] = static_cast<uint8_t>(riff >> (8 * i));
        }
        return bytes;
    }

    std::vector<uint8_t> constantWave(int16_t value, uint32_t rate, uint32_t frames)
    {
        return sampleWave(std::vector<int16_t>(frames, value), rate);
    }

    void renderFrames(size_t frames)
    {
        std::vector<float> buffer(4096 * 2);
        while (frames > 0) {
            size_t chunk = std::min<size_t>(frames, 4096);
            rune::audio::render(buffer.data(), chunk);
            frames -= chunk;
        }
    }

    struct TrackerCell {
        int channel = 0;
        int note = -1;
        int instrument = 0;
        int volume = -1;
        int command = 0;
        int param = 0;
    };

    struct TrackerSample {
        std::vector<int16_t> data;
        uint8_t flags = 0x13;
        uint32_t loopBegin = 0;
        uint32_t loopEnd = 0;
        uint32_t sustainBegin = 0;
        uint32_t sustainEnd = 0;
        uint8_t globalVolume = 64;
        uint8_t volume = 64;
    };

    std::vector<uint8_t> trackerModule(const std::vector<std::vector<TrackerCell>>& rows, const TrackerSample& sample)
    {
        std::vector<uint8_t> bytes(0xC0, 0);
        auto put16 = [&bytes](size_t at, uint32_t v) {
            bytes[at] = static_cast<uint8_t>(v);
            bytes[at + 1] = static_cast<uint8_t>(v >> 8);
        };
        auto put32 = [&bytes](size_t at, uint32_t v) {
            for (size_t i = 0; i < 4; i++) {
                bytes[at + i] = static_cast<uint8_t>(v >> (8 * i));
            }
        };
        std::memcpy(bytes.data(), "IMPM", 4);
        put16(0x20, 2);
        put16(0x22, 0);
        put16(0x24, 1);
        put16(0x26, 1);
        put16(0x28, 0x217);
        put16(0x2A, 0x200);
        put16(0x2C, 0x09);
        bytes[0x30] = 128;
        bytes[0x31] = 48;
        bytes[0x32] = 6;
        bytes[0x33] = 125;
        bytes[0x34] = 128;
        for (size_t i = 0; i < 64; i++) {
            bytes[0x40 + i] = 32;
            bytes[0x80 + i] = 64;
        }
        bytes.push_back(0);
        bytes.push_back(255);
        size_t sampleTable = bytes.size();
        bytes.resize(bytes.size() + 8, 0);
        size_t sampleHeader = bytes.size();
        bytes.resize(bytes.size() + 0x50, 0);
        std::memcpy(bytes.data() + sampleHeader, "IMPS", 4);
        bytes[sampleHeader + 0x11] = sample.globalVolume;
        bytes[sampleHeader + 0x12] = sample.flags;
        bytes[sampleHeader + 0x13] = sample.volume;
        bytes[sampleHeader + 0x2E] = 1;
        bytes[sampleHeader + 0x2F] = 32;
        put32(sampleHeader + 0x30, static_cast<uint32_t>(sample.data.size()));
        put32(sampleHeader + 0x34, sample.loopBegin);
        put32(sampleHeader + 0x38, sample.loopEnd);
        put32(sampleHeader + 0x3C, 44100);
        put32(sampleHeader + 0x40, sample.sustainBegin);
        put32(sampleHeader + 0x44, sample.sustainEnd);
        size_t pattern = bytes.size();
        bytes.resize(bytes.size() + 8, 0);
        for (const std::vector<TrackerCell>& row : rows) {
            for (const TrackerCell& cell : row) {
                uint8_t mask = static_cast<uint8_t>((cell.note >= 0 ? 1 : 0) | (cell.instrument > 0 ? 2 : 0) | (cell.volume >= 0 ? 4 : 0) | (cell.command > 0 ? 8 : 0));
                bytes.push_back(static_cast<uint8_t>((cell.channel + 1) | 0x80));
                bytes.push_back(mask);
                if (cell.note >= 0) {
                    bytes.push_back(static_cast<uint8_t>(cell.note));
                }
                if (cell.instrument > 0) {
                    bytes.push_back(static_cast<uint8_t>(cell.instrument));
                }
                if (cell.volume >= 0) {
                    bytes.push_back(static_cast<uint8_t>(cell.volume));
                }
                if (cell.command > 0) {
                    bytes.push_back(static_cast<uint8_t>(cell.command));
                    bytes.push_back(static_cast<uint8_t>(cell.param));
                }
            }
            bytes.push_back(0);
        }
        for (size_t i = rows.size(); i < 64; i++) {
            bytes.push_back(0);
        }
        put16(pattern, static_cast<uint32_t>(bytes.size() - pattern - 8));
        put16(pattern + 2, 64);
        size_t data = bytes.size();
        for (int16_t value : sample.data) {
            bytes.push_back(static_cast<uint8_t>(value));
            bytes.push_back(static_cast<uint8_t>(static_cast<uint16_t>(value) >> 8));
        }
        put32(sampleTable, static_cast<uint32_t>(sampleHeader));
        put32(sampleTable + 4, static_cast<uint32_t>(pattern));
        put32(sampleHeader + 0x48, static_cast<uint32_t>(data));
        return bytes;
    }

    std::shared_ptr<const rune::audio::ItModule> trackerSong(const std::vector<std::vector<TrackerCell>>& rows, const TrackerSample& sample)
    {
        std::vector<uint8_t> bytes = trackerModule(rows, sample);
        auto module = std::make_shared<rune::audio::ItModule>();
        REQUIRE(module->load(bytes.data(), bytes.size()));
        return module;
    }

    TrackerSample constantSample(int16_t value)
    {
        TrackerSample sample;
        sample.data.assign(64, value);
        sample.loopEnd = 64;
        return sample;
    }

    std::vector<float> renderSong(std::shared_ptr<const rune::audio::ItModule> module, size_t frames)
    {
        rune::audio::ModulePlayer player(module, 44100);
        std::vector<float> out(frames * 2);
        player.render(out.data(), frames);
        return out;
    }
}

TEST_CASE("PCM wav sound decodes with header format and length", "[data]")
{
    auto pcm = rune::audio::decodeBytes(shieldHitBytes());
    REQUIRE(pcm);
    CHECK(pcm->sampleRate == 22050);
    CHECK(pcm->channels == 1);
    CHECK(pcm->frames() == 13566);
    CHECK(pcm->samples[0] == Catch::Approx(-3392.0 / 32768.0).margin(1e-7));
    CHECK(pcm->duration() == Catch::Approx(13566.0 / 22050.0).margin(1e-6));
}

TEST_CASE("garbage bytes do not decode")
{
    std::vector<uint8_t> bytes(64, 0x42);
    CHECK_FALSE(rune::audio::decodeBytes(bytes));
}

TEST_CASE("Galaxy pans by the view space angle over seven eighths of the range and attenuates linearly")
{
    rune::Vector listener;
    rune::Rotator facingX;
    rune::audio::Placement right = rune::audio::place(listener, facingX, { 0, 100, 0 }, 400.0f);
    CHECK(right.pan == 30718);
    CHECK(right.attenuation == Catch::Approx(0.75f));

    rune::audio::Placement left = rune::audio::place(listener, facingX, { 0, -100, 0 }, 400.0f);
    CHECK(left.pan == 2047);

    rune::audio::Placement front = rune::audio::place(listener, facingX, { 100, 0, 0 }, 200.0f);
    CHECK(front.pan == 16383);
    CHECK(front.attenuation == Catch::Approx(0.5f));

    rune::audio::Placement behind = rune::audio::place(listener, facingX, { -100, 0, 0 }, 400.0f);
    CHECK(behind.pan == 16383);

    rune::audio::Placement close = rune::audio::place(listener, facingX, { 0, 10, 0 }, 400.0f);
    CHECK(close.pan == 19966);

    rune::audio::Placement outside = rune::audio::place(listener, facingX, { 500, 0, 0 }, 200.0f);
    CHECK(outside.attenuation == 0.0f);

    rune::Rotator facingY;
    facingY.yaw = 16384;
    CHECK(rune::audio::place(listener, facingY, { 0, 100, 0 }, 400.0f).pan == 16383);
    CHECK(rune::audio::place(listener, facingY, { -100, 0, 0 }, 400.0f).pan == 30718);
}

TEST_CASE("Galaxy master volume takes the louder of sound and speech volume on a 127 scale")
{
    CHECK(rune::audio::masterVolume(255, 255) == 127);
    CHECK(rune::audio::masterVolume(204, 255) == 126);
    CHECK(rune::audio::masterVolume(255, 204) == 127);
    CHECK(rune::audio::masterVolume(0, 0) == 0);
}

TEST_CASE("Galaxy voice volume scales by category, obstruction and attenuation with a floor of 128")
{
    CHECK(rune::audio::voiceVolume(0.75f, 0.0f, 204, 255, false, 1.0f) == 19660);
    CHECK(rune::audio::voiceVolume(0.75f, 0.0f, 204, 255, true, 1.0f) == 24575);
    CHECK(rune::audio::voiceVolume(1.0f, 0.5f, 255, 255, false, 1.0f) == 10813);
    CHECK(rune::audio::voiceVolume(1.0f, 0.25f, 255, 255, false, 1.0f) == 16383);
    CHECK(rune::audio::voiceVolume(0.0f, 0.0f, 255, 255, false, 1.0f) == 128);
    CHECK(rune::audio::voiceVolume(0.75f, 0.0f, 204, 255, true, 2.0f) == 32767);
}

TEST_CASE("Galaxy channel gains use the square root pan table and the fixed point volume chain")
{
    rune::audio::Gains center = rune::audio::channelGains(32767, 16383, 127, 128);
    CHECK(center.left == Catch::Approx(22450.0 * 32767.0 / 1073741824.0).epsilon(1e-6));
    CHECK(center.right == Catch::Approx(22449.0 * 32767.0 / 1073741824.0).epsilon(1e-6));

    rune::audio::Gains send = rune::audio::channelGains(32767, 16383, 127, 127);
    CHECK(send.left == Catch::Approx(22274.0 * 32767.0 / 1073741824.0).epsilon(1e-6));

    rune::audio::Gains panned = rune::audio::channelGains(19660, 30718, 126, 128);
    CHECK(panned.left == Catch::Approx(4688.0 * 32767.0 / 1073741824.0).epsilon(1e-6));
    CHECK(panned.right == Catch::Approx(18154.0 * 32767.0 / 1073741824.0).epsilon(1e-6));
}

TEST_CASE("Galaxy Doppler shifts ambient pitch by the approach speed over DopplerSpeed")
{
    rune::Vector listener;
    rune::Vector source { 1000, 0, 0 };
    CHECK(rune::audio::dopplerShift({ 650, 0, 0 }, source, listener, 6500.0f) == Catch::Approx(0.9f));
    CHECK(rune::audio::dopplerShift({ -650, 0, 0 }, source, listener, 6500.0f) == Catch::Approx(1.1f));
    CHECK(rune::audio::dopplerShift({ -13000, 0, 0 }, source, listener, 6500.0f) == 2.0f);
    CHECK(rune::audio::dopplerShift({ 13000, 0, 0 }, source, listener, 6500.0f) == 0.5f);
    CHECK(rune::audio::dopplerShift({ 0, 650, 0 }, source, listener, 6500.0f) == 1.0f);

    CHECK(rune::audio::voiceStep(44100, 1.0f, 1.0f, 44100) == 65536);
    CHECK(rune::audio::voiceStep(11025, 1.0f, 1.0f, 44100) == 16384);
    CHECK(rune::audio::voiceStep(22050, 1.0f, 0.9f, 44100) == 29489);
    CHECK(rune::audio::voiceStep(22050, 1.0f, 1.1f, 44100) == 36044);
}

TEST_CASE("a centered full volume voice starts at the next update at the Galaxy constant power level", "[data]")
{
    AudioDevice galaxy("Galaxy.GalaxyAudioSubsystem");
    rune::engine::initialize(RUNE_GAME_DATA);
    REQUIRE(rune::audio::initializeOffline(44100));
    rune::audio::setVolumes(1.0f, 1.0f, 1.0f);
    rune::USound tone;
    tone.bytes = constantWave(16384, 44100, 8000);
    int32_t id = rune::audio::playSound(&tone, nullptr, rune::audio::SlotNone, {}, 1.0f, 1000.0f, 1.0f, false);
    REQUIRE(id != 0);
    std::vector<float> buffer(4000 * 2);
    rune::audio::render(buffer.data(), 4000);
    CHECK(rune::audio::isPlaying(id));
    CHECK(*std::max_element(buffer.begin(), buffer.end()) == 0.0f);
    rune::audio::update({}, {}, 0.0f);
    rune::audio::render(buffer.data(), 4000);
    CHECK(buffer[0] == Catch::Approx(0.5 * 22450.0 * 32767.0 / 1073741824.0).epsilon(1e-6));
    CHECK(buffer[3999 * 2] == Catch::Approx(0.5 * 22450.0 * 32767.0 / 1073741824.0).epsilon(1e-5));
    CHECK(buffer[3999 * 2 + 1] == Catch::Approx(0.5 * 22449.0 * 32767.0 / 1073741824.0).epsilon(1e-5));
    rune::audio::shutdown();
}

TEST_CASE("voices advance in 16.16 fixed point with 15 bit interpolation weights and no pitch clamp", "[data]")
{
    AudioDevice galaxy("Galaxy.GalaxyAudioSubsystem");
    rune::engine::initialize(RUNE_GAME_DATA);
    REQUIRE(rune::audio::initializeOffline(44100));
    rune::audio::setVolumes(1.0f, 1.0f, 1.0f);
    rune::USound ramp;
    ramp.bytes = sampleWave({ 0, 20000, 0, 0 }, 22050);
    REQUIRE(rune::audio::playSound(&ramp, nullptr, rune::audio::SlotNone, {}, 1.0f, 1000.0f, 1.0f, false) != 0);
    rune::audio::update({}, {}, 0.0f);
    std::vector<float> buffer(8 * 2);
    rune::audio::render(buffer.data(), 8);
    const double unit = 22450.0 / 35184372088832.0;
    CHECK(buffer[0] == 0.0f);
    CHECK(buffer[2] == Catch::Approx(20000.0 * 16384.0 * unit).epsilon(1e-7));
    CHECK(buffer[4] == Catch::Approx(20000.0 * 32767.0 * unit).epsilon(1e-7));
    CHECK(buffer[6] == Catch::Approx(20000.0 * 16383.0 * unit).epsilon(1e-7));
    rune::audio::shutdown();

    REQUIRE(rune::audio::initializeOffline(44100));
    rune::USound tone;
    tone.bytes = constantWave(1000, 22050, 4000);
    int32_t fast = rune::audio::playSound(&tone, nullptr, rune::audio::SlotNone, {}, 1.0f, 1000.0f, 8.0f, false);
    REQUIRE(fast != 0);
    CHECK(rune::audio::voices().front().step == 4.0);
    rune::audio::shutdown();
}

TEST_CASE("the interpolation at a loop end reads the raw next sample before wrapping", "[data]")
{
    AudioDevice galaxy("Galaxy.GalaxyAudioSubsystem");
    rune::engine::initialize(RUNE_GAME_DATA);
    REQUIRE(rune::audio::initializeOffline(44100));
    rune::audio::setVolumes(1.0f, 1.0f, 1.0f);
    rune::USound looped;
    looped.bytes = loopWave({ 0, 0, 20000, 0 }, 22050, 0, 1);
    REQUIRE(rune::audio::playSound(&looped, nullptr, rune::audio::SlotNone, {}, 1.0f, 1000.0f, 1.0f, false) != 0);
    rune::audio::update({}, {}, 0.0f);
    std::vector<float> buffer(8 * 2);
    rune::audio::render(buffer.data(), 8);
    const double unit = 22450.0 / 35184372088832.0;
    CHECK(buffer[2 * 2] == 0.0f);
    CHECK(buffer[3 * 2] == Catch::Approx(20000.0 * 16384.0 * unit).epsilon(1e-7));
    CHECK(buffer[4 * 2] == 0.0f);
    CHECK(buffer[7 * 2] == Catch::Approx(20000.0 * 16384.0 * unit).epsilon(1e-7));
    rune::audio::shutdown();
}

TEST_CASE("ambient light sources scale their ambient sound by the flicker and strobe light functions", "[data]")
{
    AudioDevice galaxy("Galaxy.GalaxyAudioSubsystem");
    rune::engine::initialize(RUNE_GAME_DATA);
    rune::objects::unloadPackage(rune::Name("DM-Hel"));
    REQUIRE(rune::audio::initializeOffline(44100));
    rune::audio::setVolumes(1.0f, 1.0f, 1.0f);
    {
        rune::World world;
        REQUIRE(world.loadMap("DM-Hel"));
        rune::UObject* player = world.player();
        REQUIRE(player != nullptr);
        rune::audio::update(rune::actor::Location(player), {}, 0.0f);
        const rune::UObject* chosen = nullptr;
        for (const rune::audio::VoiceStatus& voice : rune::audio::voices()) {
            if (voice.ambient && voice.volume > 2000) {
                chosen = voice.actor;
            }
        }
        REQUIRE(chosen != nullptr);
        rune::UObject* source = const_cast<rune::UObject*>(chosen);
        rune::actor::SoundVolume(source) = 64;
        rune::actor::LightBrightness(source) = 255;
        auto level = [&]() {
            rune::audio::update(rune::actor::Location(player), {}, 0.0f);
            for (const rune::audio::VoiceStatus& voice : rune::audio::voices()) {
                if (voice.actor == source) {
                    return voice.volume;
                }
            }
            return 0;
        };
        const uint8_t steady = 1;
        const uint8_t flicker = 4;
        const uint8_t strobe = 5;
        rune::actor::LightType(source) = steady;
        int32_t full = level();
        REQUIRE(full > 1000);
        rune::audio::setLightRandom([](int32_t) {
            return 0.75f;
        });
        rune::actor::LightType(source) = flicker;
        CHECK(level() == Catch::Approx(full * 0.75).margin(1.0));
        rune::audio::setLightRandom([](int32_t) {
            return 0.4f;
        });
        CHECK(level() == 128);
        rune::actor::LightType(source) = strobe;
        std::vector<int32_t> levels;
        for (int i = 0; i < 4; i++) {
            rune::levelinfo::TimeSeconds(world.levelInfo) += 0.1f;
            levels.push_back(level());
        }
        CHECK(std::min(levels[0], levels[1]) == 128);
        CHECK(std::max(levels[0], levels[1]) == full);
        CHECK(levels[2] == levels[0]);
        CHECK(levels[3] == levels[1]);
        rune::audio::setLightRandom(nullptr);
        rune::actor::LightType(source) = 0;
        rune::objects::unloadPackage(rune::Name("DM-Hel"));
    }
    rune::audio::shutdown();
}

TEST_CASE("the reverb network is three allpass stages with a crossed half gain feedback")
{
    rune::audio::ReverbSettings settings;
    settings.volume = 1.0f;
    settings.cutoff = 0.0f;
    settings.delay = { 441.5f / 44100.0f, 13230.5f / 44100.0f, 882.5f / 44100.0f, 13230.5f / 44100.0f, 1764.5f / 44100.0f, 13230.5f / 44100.0f };
    settings.gain = { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f };
    rune::audio::Reverb reverb;
    reverb.configure(settings, 44100);
    REQUIRE(reverb.active());
    std::vector<float> send(4000 * 2, 0.0f);
    send[0] = 1.0f;
    std::vector<float> out(4000 * 2, 0.0f);
    reverb.process(send.data(), out.data(), 4000);
    auto left = [&](size_t n) {
        return out[n * 2];
    };
    CHECK(left(0) == Catch::Approx(-0.0625).margin(1e-7));
    CHECK(left(441) == Catch::Approx(0.09375).margin(1e-7));
    CHECK(left(882) == Catch::Approx(0.09375).margin(1e-7));
    CHECK(left(1323) == Catch::Approx(-0.140625).margin(1e-7));
    CHECK(left(1764) == Catch::Approx(0.09375).margin(1e-7));
    CHECK(left(2205) == Catch::Approx(-0.140625).margin(1e-7));
    CHECK(left(2646) == Catch::Approx(-0.140625).margin(1e-7));
    CHECK(left(3087) == Catch::Approx(0.2109375).margin(1e-7));
    CHECK(left(1) == 0.0f);
    CHECK(left(100) == 0.0f);
    CHECK(left(2) == Catch::Approx(-0.000244140625).margin(1e-9));
    CHECK(out[1] == 0.0f);
    CHECK(out[3] == Catch::Approx(0.00390625).margin(1e-9));

    settings.volume = 0.0f;
    reverb.configure(settings, 44100);
    CHECK_FALSE(reverb.active());
}

TEST_CASE("the reverb cutoff lowpasses the recirculated delay line")
{
    rune::audio::ReverbSettings settings;
    settings.volume = 1.0f;
    settings.cutoff = static_cast<float>(std::acos(0.5) * 44100.0 / 6.282);
    settings.delay = { 441.5f / 44100.0f, 13230.5f / 44100.0f, 1500.5f / 44100.0f, 13230.5f / 44100.0f, 3000.5f / 44100.0f, 13230.5f / 44100.0f };
    settings.gain = { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f };
    rune::audio::Reverb reverb;
    reverb.configure(settings, 44100);
    std::vector<float> send(1000 * 2, 0.0f);
    send[0] = 1.0f;
    std::vector<float> out(1000 * 2, 0.0f);
    reverb.process(send.data(), out.data(), 1000);
    CHECK(out[441 * 2] == Catch::Approx(0.09375).margin(1e-7));
    CHECK(out[882 * 2] == Catch::Approx(0.0289703).margin(1e-5));
    CHECK(out[883 * 2] == Catch::Approx(0.0110657).margin(1e-5));
}

TEST_CASE("the view actor zone selects the Galaxy reverb and a tail follows the dry sound", "[data]")
{
    AudioDevice galaxy("Galaxy.GalaxyAudioSubsystem");
    rune::engine::initialize(RUNE_GAME_DATA);
    rune::objects::unloadPackage(rune::Name("DM-Zorn"));
    REQUIRE(rune::audio::initializeOffline(44100));
    rune::audio::setVolumes(1.0f, 1.0f, 1.0f);
    {
        rune::World world;
        REQUIRE(world.loadMap("DM-Zorn"));
        rune::UObject* player = world.player();
        REQUIRE(player != nullptr);
        rune::UObject* reverbZone = nullptr;
        for (rune::UObject* actor : world.actors) {
            if (actor && actor->data && actor->name == rune::Name("ZoneInfo0")) {
                reverbZone = actor;
            }
        }
        REQUIRE(reverbZone != nullptr);
        rune::UObject* inside = nullptr;
        for (rune::UObject* actor : world.actors) {
            if (actor && actor->data && actor != reverbZone && rune::isPawn(actor) == false && rune::region(actor).zone == reverbZone) {
                inside = actor;
                break;
            }
        }
        REQUIRE(inside != nullptr);
        REQUIRE(world.farMoveActor(player, rune::actor::Location(inside), false, true));
        rune::audio::setVolumes(1.0f, 0.0f, 1.0f);
        for (rune::UObject* actor : world.actors) {
            if (actor && actor->data) {
                rune::actor::AmbientSound(actor) = nullptr;
            }
        }
        rune::USound tone;
        tone.bytes = constantWave(8000, 44100, 2000);
        auto tailPeak = [&]() {
            int32_t id = rune::audio::playSound(&tone, nullptr, rune::audio::SlotNone, {}, 1.0f, 1000.0f, 1.0f, false);
            REQUIRE(id != 0);
            rune::audio::update(rune::actor::Location(player), {}, 0.0f);
            std::vector<float> buffer(4410 * 2);
            rune::audio::render(buffer.data(), 2100);
            CHECK_FALSE(rune::audio::isPlaying(id));
            rune::audio::render(buffer.data(), 4410);
            float peak = 0;
            for (float sample : buffer) {
                peak = std::max(peak, std::fabs(sample));
            }
            return peak;
        };

        REQUIRE(rune::region(player).zone == reverbZone);
        rune::audio::update(rune::actor::Location(player), {}, 0.0f);
        rune::audio::ReverbSettings settings = rune::audio::reverbSettings();
        CHECK(settings.volume == Catch::Approx(100.0 / 255.0).epsilon(1e-6));
        CHECK(settings.cutoff == 6000.0f);
        CHECK(settings.delay[0] == Catch::Approx(0.046).epsilon(1e-6));
        CHECK(settings.delay[1] == Catch::Approx(0.1).epsilon(1e-6));
        CHECK(settings.delay[2] == Catch::Approx(0.154).epsilon(1e-6));
        CHECK(settings.delay[3] == Catch::Approx(0.224).epsilon(1e-6));
        CHECK(settings.gain[0] == Catch::Approx(63.0 / 255.0).epsilon(1e-6));
        CHECK(settings.gain[1] == Catch::Approx(96.0 / 255.0).epsilon(1e-6));
        CHECK(settings.gain[2] == Catch::Approx(34.0 / 255.0).epsilon(1e-6));
        CHECK(tailPeak() > 0.01f);

        rune::zoneinfo::bReverbZone(reverbZone) = 0;
        rune::audio::update(rune::actor::Location(player), {}, 0.0f);
        CHECK(rune::audio::reverbSettings().volume == 0.0f);
        CHECK(tailPeak() == 0.0f);
        rune::objects::unloadPackage(rune::Name("DM-Zorn"));
    }
    rune::audio::shutdown();
}

TEST_CASE("an obstructed sound fades to a third over half a second and recovers when clear", "[data]")
{
    AudioDevice galaxy("Galaxy.GalaxyAudioSubsystem");
    rune::engine::initialize(RUNE_GAME_DATA);
    rune::objects::unloadPackage(rune::Name("DwarfMap2"));
    REQUIRE(rune::audio::initializeOffline(44100));
    rune::audio::setVolumes(1.0f, 1.0f, 1.0f);
    {
        rune::World world;
        REQUIRE(world.loadMap("DwarfMap2"));
        rune::UObject* player = world.player();
        REQUIRE(player != nullptr);
        rune::UObject* source = nullptr;
        for (rune::UObject* actor : world.actors) {
            if (actor && actor->data && actor != player && rune::isPawn(actor)) {
                source = actor;
                break;
            }
        }
        REQUIRE(source != nullptr);
        rune::Vector eye = rune::actor::Location(player);
        eye.z += rune::pawn::EyeHeight(player);
        rune::actor::Location(source) = { eye.x, eye.y, eye.z - 4000.0f };
        REQUIRE_FALSE(world.fastLineCheck(eye, rune::actor::Location(source)));
        rune::USound sound;
        sound.bytes = loopedHumBytes();
        const int32_t slotMisc = 1;
        int32_t id = rune::audio::playSound(&sound, source, slotMisc, rune::actor::Location(source), 1.0f, 100000.0f, 1.0f, true);
        REQUIRE(id != 0);
        auto status = [id]() {
            for (const rune::audio::VoiceStatus& voice : rune::audio::voices()) {
                if (voice.id == id) {
                    return voice;
                }
            }
            return rune::audio::VoiceStatus {};
        };
        rune::audio::update(eye, {}, 0.2f);
        CHECK(status().obstruction == Catch::Approx(0.2f));
        for (int i = 0; i < 3; i++) {
            rune::audio::update(eye, {}, 0.2f);
        }
        CHECK(status().obstruction == 0.5f);
        CHECK(status().volume == 10380);

        rune::actor::Location(source) = { eye.x + 10.0f, eye.y, eye.z };
        REQUIRE(world.fastLineCheck(eye, rune::actor::Location(source)));
        rune::audio::update(eye, {}, 0.2f);
        CHECK(status().obstruction == Catch::Approx(0.3f));
        rune::audio::update(eye, {}, 0.5f);
        CHECK(status().obstruction == 0.0f);
        rune::objects::unloadPackage(rune::Name("DwarfMap2"));
    }
    rune::audio::shutdown();
}

TEST_CASE("ambient sounds stop while the game is paused and moving ambient sources are Doppler shifted", "[data]")
{
    AudioDevice galaxy("Galaxy.GalaxyAudioSubsystem");
    rune::engine::initialize(RUNE_GAME_DATA);
    rune::objects::unloadPackage(rune::Name("DM-Hel"));
    REQUIRE(rune::audio::initializeOffline(44100));
    {
        rune::World world;
        REQUIRE(world.loadMap("DM-Hel"));
        rune::UObject* player = world.player();
        REQUIRE(player != nullptr);
        auto ambient = []() {
            std::vector<rune::audio::VoiceStatus> result;
            for (const rune::audio::VoiceStatus& voice : rune::audio::voices()) {
                if (voice.ambient) {
                    result.push_back(voice);
                }
            }
            return result;
        };
        rune::audio::update(rune::actor::Location(player), {}, 0.0f);
        std::vector<rune::audio::VoiceStatus> playing = ambient();
        REQUIRE_FALSE(playing.empty());

        rune::levelinfo::Pauser(world.levelInfo) = "Player";
        rune::audio::update(rune::actor::Location(player), {}, 0.0f);
        CHECK(ambient().empty());
        rune::levelinfo::Pauser(world.levelInfo).clear();
        rune::audio::update(rune::actor::Location(player), {}, 0.0f);
        playing = ambient();
        REQUIRE_FALSE(playing.empty());

        rune::UObject* mover = const_cast<rune::UObject*>(playing.front().actor);
        double still = playing.front().step;
        rune::Vector away = rune::actor::Location(mover) - rune::actor::Location(player);
        rune::actor::Velocity(mover) = away.normal() * 650.0f;
        rune::audio::update(rune::actor::Location(player), {}, 0.0f);
        double receding = 0;
        for (const rune::audio::VoiceStatus& voice : ambient()) {
            if (voice.actor == mover) {
                receding = voice.step;
            }
        }
        CHECK(receding / still == Catch::Approx(0.9).epsilon(1e-3));
        rune::actor::Velocity(mover) = {};
        rune::objects::unloadPackage(rune::Name("DM-Hel"));
    }
    rune::audio::shutdown();
}

TEST_CASE("wav loop points come from the smpl chunk", "[data]")
{
    auto looped = rune::audio::decodeBytes(loopedHumBytes());
    REQUIRE(looped);
    CHECK(looped->frames() == 70508);
    CHECK(looped->looped);
    CHECK(looped->loopStart == 0);
    CHECK(looped->loopEnd == 70508);

    auto oneShot = rune::audio::decodeBytes(shieldHitBytes());
    REQUIRE(oneShot);
    CHECK_FALSE(oneShot->looped);
}

TEST_CASE("ambient sound radius follows the actor sound radius")
{
    CHECK(rune::audio::ambientRadius(0) == 25.0f);
    CHECK(rune::audio::ambientRadius(32) == 825.0f);
    CHECK(rune::audio::ambientRadius(255) == 6400.0f);
}

TEST_CASE("a looped sound keeps playing after a one shot sound has ended", "[data]")
{
    AudioDevice galaxy("Galaxy.GalaxyAudioSubsystem");
    rune::engine::initialize(RUNE_GAME_DATA);
    REQUIRE(rune::audio::initializeOffline(44100));
    rune::USound looped;
    looped.bytes = loopedHumBytes();
    rune::USound oneShot;
    oneShot.bytes = shieldHitBytes();
    float loopedLength = rune::audio::duration(&looped);
    REQUIRE(rune::audio::duration(&oneShot) < loopedLength);
    int32_t loopedId = rune::audio::playSound(&looped, nullptr, rune::audio::SlotNone, {}, 1.0f, 1000.0f, 1.0f, false);
    int32_t oneShotId = rune::audio::playSound(&oneShot, nullptr, rune::audio::SlotNone, {}, 1.0f, 1000.0f, 1.0f, false);
    REQUIRE(loopedId != 0);
    REQUIRE(oneShotId != 0);
    rune::audio::update({}, {}, 0.0f);
    renderFrames(static_cast<size_t>(loopedLength * 2.0f * 44100.0f));
    CHECK(rune::audio::isPlaying(loopedId));
    CHECK_FALSE(rune::audio::isPlaying(oneShotId));
    rune::audio::shutdown();
}

TEST_CASE("a sound on an occupied actor slot replaces the earlier one unless it is no override", "[data]")
{
    AudioDevice galaxy("Galaxy.GalaxyAudioSubsystem");
    rune::engine::initialize(RUNE_GAME_DATA);
    REQUIRE(rune::audio::initializeOffline(44100));
    rune::USound sound;
    sound.bytes = loopedHumBytes();
    rune::UObject actor;
    const int32_t slotMisc = 1;
    int32_t first = rune::audio::playSound(&sound, &actor, slotMisc, {}, 1.0f, 1000.0f, 1.0f, true);
    int32_t second = rune::audio::playSound(&sound, &actor, slotMisc, {}, 1.0f, 1000.0f, 1.0f, true);
    REQUIRE(first != 0);
    REQUIRE(second != 0);
    CHECK_FALSE(rune::audio::isPlaying(first));
    CHECK(rune::audio::isPlaying(second));

    int32_t blocked = rune::audio::playSound(&sound, &actor, slotMisc, {}, 1.0f, 1000.0f, 1.0f, true, true);
    CHECK(blocked == 0);
    CHECK(rune::audio::isPlaying(second));

    int32_t a = rune::audio::playSound(&sound, &actor, rune::audio::SlotNone, {}, 1.0f, 1000.0f, 1.0f, true);
    int32_t b = rune::audio::playSound(&sound, &actor, rune::audio::SlotNone, {}, 1.0f, 1000.0f, 1.0f, true);
    CHECK(rune::audio::isPlaying(a));
    CHECK(rune::audio::isPlaying(b));
    CHECK(rune::audio::isPlaying(second));
    rune::audio::shutdown();
}

TEST_CASE("the voice limit steals the quietest sound and rejects quieter ones", "[data]")
{
    AudioDevice galaxy("Galaxy.GalaxyAudioSubsystem");
    rune::engine::initialize(RUNE_GAME_DATA);
    REQUIRE(rune::audio::initializeOffline(44100));
    rune::USound sound;
    sound.bytes = loopedHumBytes();
    std::vector<int32_t> distant;
    for (int i = 0; i < 16; i++) {
        int32_t id = rune::audio::playSound(&sound, nullptr, rune::audio::SlotNone, { 500, 0, 0 }, 1.0f, 1000.0f, 1.0f, true);
        REQUIRE(id != 0);
        distant.push_back(id);
    }
    CHECK(rune::audio::voices().size() == 16);
    CHECK(rune::audio::voices().front().priority == Catch::Approx(0.5f));

    int32_t near = rune::audio::playSound(&sound, nullptr, rune::audio::SlotNone, { 100, 0, 0 }, 1.0f, 1000.0f, 1.0f, true);
    CHECK(near != 0);
    CHECK(rune::audio::isPlaying(near));
    size_t stillPlaying = static_cast<size_t>(std::count_if(distant.begin(), distant.end(), rune::audio::isPlaying));
    CHECK(stillPlaying == 15);
    CHECK(rune::audio::voices().size() == 16);

    int32_t far = rune::audio::playSound(&sound, nullptr, rune::audio::SlotNone, { 900, 0, 0 }, 1.0f, 1000.0f, 1.0f, true);
    CHECK(far == 0);
    CHECK(rune::audio::voices().size() == 16);
    rune::audio::shutdown();
}

TEST_CASE("Galaxy ignores the impulse tracker mix volume")
{
    auto module = trackerSong({ { { 0, 60, 1, 64 } } }, constantSample(16384));
    REQUIRE(module->mixVolume == 48);
    auto louder = std::make_shared<rune::audio::ItModule>(*module);
    louder->mixVolume = 96;
    rune::audio::ModulePlayer a(module, 44100);
    rune::audio::ModulePlayer b(louder, 44100);
    std::vector<float> first(4096 * 2);
    std::vector<float> second(4096 * 2);
    a.render(first.data(), 4096);
    b.render(second.data(), 4096);
    CHECK(first == second);
    CHECK(first[4000 * 2] == Catch::Approx(0.5 * 13808.0 / 32768.0).epsilon(1e-5));
}

TEST_CASE("Galaxy music voices run the fixed point gain chain through the square root pan table")
{
    std::vector<float> column = renderSong(trackerSong({ { { 0, 60, 1, 64 } } }, constantSample(16384)), 4096);
    CHECK(column[4000 * 2] == Catch::Approx(0.5 * 13808.0 / 32768.0).epsilon(1e-5));
    CHECK(column[4000 * 2 + 1] == Catch::Approx(0.5 * 13809.0 / 32768.0).epsilon(1e-5));
    std::vector<float> preset = renderSong(trackerSong({ { { 0, 60, 1 } } }, constantSample(16384)), 4096);
    CHECK(preset[4000 * 2] == Catch::Approx(0.5 * 13917.0 / 32768.0).epsilon(1e-5));
    CHECK(preset[4000 * 2 + 1] == Catch::Approx(0.5 * 13917.0 / 32768.0).epsilon(1e-5));
}

TEST_CASE("Galaxy drops channel volume, panbrello and the sample global volume")
{
    std::vector<float> plain = renderSong(trackerSong({ { { 0, 60, 1, 64 } }, {} }, constantSample(16384)), 12000);
    TrackerSample quiet = constantSample(16384);
    quiet.globalVolume = 16;
    std::vector<float> dropped = renderSong(trackerSong({ { { 0, 60, 1, 64, 13, 0x20 } }, { { 0, -1, 0, -1, 25, 0x44 } } }, quiet), 12000);
    CHECK(dropped == plain);
    CHECK(plain[4000 * 2] == Catch::Approx(0.5 * 13808.0 / 32768.0).epsilon(1e-5));
}

TEST_CASE("Galaxy ignores sustain loops and a note off cuts the sample at once")
{
    TrackerSample sustained;
    sustained.data.assign(2000, 16384);
    sustained.flags = 0x23;
    sustained.sustainBegin = 0;
    sustained.sustainEnd = 2000;
    std::vector<float> once = renderSong(trackerSong({ { { 0, 60, 1, 64 } } }, sustained), 4000);
    CHECK(once[1000 * 2] == Catch::Approx(0.5 * 13808.0 / 32768.0).epsilon(1e-5));
    CHECK(once[3000 * 2] == 0.0f);
    std::vector<float> released = renderSong(trackerSong({ { { 0, 60, 1, 64 } }, { { 0, 255 } } }, constantSample(16384)), 6000);
    CHECK(released[5000 * 2] == Catch::Approx(0.5 * 13808.0 / 32768.0).epsilon(1e-5));
    CHECK(released[5292 * 2 + 200] == 0.0f);
}

TEST_CASE("S9x centres the channel and inverts its right side for Galaxy surround")
{
    std::vector<float> out = renderSong(trackerSong({ { { 0, 60, 1, 64, 24, 0x00 } }, { { 0, -1, 0, -1, 19, 0x91 } } }, constantSample(16384)), 9000);
    CHECK(out[4000 * 2] == Catch::Approx(0.5 * 19528.0 / 32768.0).epsilon(1e-5));
    CHECK(out[4000 * 2 + 1] == 0.0f);
    CHECK(out[8800 * 2] == Catch::Approx(0.5 * 13808.0 / 32768.0).epsilon(1e-5));
    CHECK(out[8800 * 2 + 1] == Catch::Approx(-0.5 * 13809.0 / 32768.0).epsilon(1e-5));
}

TEST_CASE("Q keeps the volume and retriggers on every tick divisible by its interval")
{
    TrackerSample ramp;
    for (int i = 0; i < 2048; i++) {
        ramp.data.push_back(static_cast<int16_t>(i * 16));
    }
    ramp.flags = 0x03;
    std::vector<float> out = renderSong(trackerSong({ { { 0, 60, 1, 64, 17, 0x32 } } }, ramp), 4000);
    CHECK(out[1500 * 2] == Catch::Approx(1500.0 * 16.0 / 32768.0 * 13808.0 / 32768.0).epsilon(1e-5));
    CHECK(out[1774 * 2] == Catch::Approx(10.0 * 16.0 / 32768.0 * 13808.0 / 32768.0).epsilon(1e-5));
    CHECK(out[3538 * 2] == Catch::Approx(10.0 * 16.0 / 32768.0 * 13808.0 / 32768.0).epsilon(1e-5));
}

TEST_CASE("vibrato bends by the Galaxy sine table times depth in 256ths of a semitone")
{
    TrackerSample ramp;
    for (int i = 0; i < 4000; i++) {
        ramp.data.push_back(static_cast<int16_t>(i * 8));
    }
    ramp.flags = 0x03;
    std::vector<float> out = renderSong(trackerSong({ { { 0, 60, 1, 64, 8, 0x48 } } }, ramp), 2000);
    CHECK(out[1764 * 2] == Catch::Approx(1783.5167 * 8.0 / 32768.0 * 13808.0 / 32768.0).epsilon(1e-4));
}

TEST_CASE("the loader resolves effect memory per channel and drops what Galaxy drops")
{
    auto module = trackerSong({ { { 0, 60, 1, 32, 15, 0x10 } }, { { 0, -1, 0, -1, 4, 0x00 } }, { { 0, -1, 0, -1, 25, 0x44 } }, { { 0, -1, 0, -1, 1, 0x40 } }, { { 0, -1, 0, -1, 17, 0x32 } } }, constantSample(16384));
    const std::vector<rune::audio::ItCell>& cells = module->patterns[0].cells;
    CHECK(cells[64].command == 4);
    CHECK(cells[64].param == 0x10);
    CHECK(cells[2 * 64].command == 0);
    CHECK(cells[3 * 64].param == 0x20);
    CHECK(cells[4 * 64].param == 0x02);
}

TEST_CASE("the music volume is squared into the Galaxy master level", "[data]")
{
    AudioDevice galaxy("Galaxy.GalaxyAudioSubsystem");
    rune::engine::initialize(RUNE_GAME_DATA);
    REQUIRE(rune::audio::initializeOffline(44100));
    rune::UMusic music;
    music.bytes = trackerModule({ { { 0, 60, 1, 64 } } }, constantSample(16384));
    rune::audio::setVolumes(1.0f, 0.5f, 1.0f);
    rune::audio::playMusic(&music, 0, 1);
    std::vector<float> out(4096 * 2);
    rune::audio::render(out.data(), 4096);
    CHECK(out[4000 * 2] == Catch::Approx(0.5 * 13808.0 / 32768.0 * 3969.0 / 16129.0).epsilon(1e-5));
    rune::audio::setVolumes(1.0f, 1.0f, 1.0f);
    rune::audio::shutdown();
}

TEST_CASE("speech phonemes come from the strongest FFT bin of the window")
{
    auto tone = [](int bin) {
        rune::audio::DecodedSound pcm;
        pcm.sampleRate = 44100;
        pcm.channels = 1;
        pcm.samples.resize(4096);
        for (size_t n = 0; n < pcm.samples.size(); n++) {
            pcm.samples[n] = bin < 0 ? 0.0f : 0.5f * static_cast<float>(std::cos(2.0 * 3.14159265358979 * bin * static_cast<double>(n) / 1024.0));
        }
        return rune::audio::speechPhoneme(pcm, 0);
    };
    CHECK(tone(-1) == 'X');
    CHECK(tone(3) == 'E');
    CHECK(tone(8) == 'U');
    CHECK(tone(12) == 'O');
    CHECK(tone(20) == 'A');
    CHECK(tone(40) == 'F');
    CHECK(tone(60) == 'T');
}

TEST_CASE("a raw stream is resampled to the output rate at the sound volume and blocks new voices", "[data]")
{
    AudioDevice galaxy("Galaxy.GalaxyAudioSubsystem");
    rune::engine::initialize(RUNE_GAME_DATA);
    REQUIRE(rune::audio::initializeOffline(44100));
    rune::audio::setVolumes(0.5f, 1.0f, 1.0f);
    size_t produced = 0;
    rune::audio::beginStream([&](float* stereo, size_t frames) {
        size_t count = std::min(frames, static_cast<size_t>(1000) - produced);
        for (size_t i = 0; i < count; i++) {
            stereo[i * 2] = static_cast<float>(produced + i) * 0.001f;
            stereo[i * 2 + 1] = -static_cast<float>(produced + i) * 0.001f;
        }
        produced += count;
        return count;
    },
        22050);
    rune::USound tone;
    tone.bytes = constantWave(16384, 44100, 8000);
    CHECK(rune::audio::playSound(&tone, nullptr, rune::audio::SlotNone, {}, 1.0f, 1000.0f, 1.0f, false) == 0);
    rune::audio::update({}, {}, 0.0f);
    std::vector<float> buffer(100 * 2);
    rune::audio::render(buffer.data(), 100);
    CHECK(buffer[0] == Catch::Approx(0.0f).margin(1e-7));
    CHECK(buffer[10 * 2] == Catch::Approx(0.0025f).epsilon(1e-4));
    CHECK(buffer[11 * 2] == Catch::Approx(0.00275f).epsilon(1e-4));
    CHECK(buffer[11 * 2 + 1] == Catch::Approx(-0.00275f).epsilon(1e-4));
    CHECK(buffer[99 * 2] == Catch::Approx(0.02475f).epsilon(1e-4));
    rune::audio::render(buffer.data(), 100);
    CHECK(buffer[0] == Catch::Approx(0.025f).epsilon(1e-4));

    rune::audio::endStream();
    rune::audio::render(buffer.data(), 100);
    CHECK(*std::max_element(buffer.begin(), buffer.end()) == 0.0f);
    CHECK(rune::audio::playSound(&tone, nullptr, rune::audio::SlotNone, {}, 1.0f, 1000.0f, 1.0f, false) != 0);
    rune::audio::shutdown();
}

TEST_CASE("native 268 is StopAllSound and stops every voice without touching the volume", "[data]")
{
    AudioDevice galaxy("Galaxy.GalaxyAudioSubsystem");
    rune::engine::initialize(RUNE_GAME_DATA);
    rune::objects::unloadPackage(rune::Name("RagnarVillage2"));
    REQUIRE(rune::audio::initializeOffline(44100));
    rune::audio::setVolumes(0.75f, 1.0f, 1.0f);
    {
        rune::World world;
        REQUIRE(world.loadMap("RagnarVillage2"));
        rune::UObject* player = world.player();
        REQUIRE(player != nullptr);
        rune::USound tone;
        tone.bytes = constantWave(16384, 44100, 80000);
        int32_t first = rune::audio::playSound(&tone, nullptr, rune::audio::SlotNone, {}, 1.0f, 1000.0f, 1.0f, false);
        int32_t second = rune::audio::playSound(&tone, player, 1, rune::actor::Location(player), 1.0f, 1000.0f, 1.0f, false);
        REQUIRE(rune::audio::isPlaying(first));
        REQUIRE(rune::audio::isPlaying(second));
        rune::vm::Call(player, rune::Name("StopAllSound")).run();
        CHECK_FALSE(rune::audio::isPlaying(first));
        CHECK_FALSE(rune::audio::isPlaying(second));
        CHECK(rune::audio::volumes().sound == 0.75f);
    }
    rune::objects::unloadPackage(rune::Name("RagnarVillage2"));
    rune::audio::shutdown();
}

namespace {
    rune::audio::VoiceStatus voiceWithId(int32_t id)
    {
        for (const rune::audio::VoiceStatus& voice : rune::audio::voices()) {
            if (voice.id == id) {
                return voice;
            }
        }
        FAIL("no voice " << id);
        return {};
    }
}

TEST_CASE("ALAudio clamps volume and pitch and falls off as a tenth of the radius over the distance", "[data]")
{
    AudioDevice device("ALAudio.ALAudioSubsystem");
    REQUIRE(rune::audio::initializeOffline(44100));
    rune::audio::setVolumes(1.0f, 1.0f, 1.0f);
    rune::USound tone;
    tone.bytes = constantWave(16384, 44100, 44100);
    int32_t near = rune::audio::playSound(&tone, nullptr, rune::audio::SlotNone, { 50, 0, 0 }, 3.0f, 1000.0f, 5.0f, true);
    int32_t middle = rune::audio::playSound(&tone, nullptr, rune::audio::SlotNone, { 500, 0, 0 }, 1.0f, 1000.0f, 0.01f, true);
    int32_t far = rune::audio::playSound(&tone, nullptr, rune::audio::SlotNone, { 800, 0, 0 }, 0.001f, 1000.0f, 1.0f, true);
    int32_t side = rune::audio::playSound(&tone, nullptr, rune::audio::SlotNone, { 0, 500, 0 }, 1.0f, 1000.0f, 1.0f, true);
    rune::audio::update({}, {}, 0.0f);
    const double half = std::sqrt(0.5);
    CHECK(voiceWithId(near).gainLeft == Catch::Approx(half).epsilon(1e-4));
    CHECK(voiceWithId(near).gainRight == Catch::Approx(half).epsilon(1e-4));
    CHECK(voiceWithId(near).step == 2.0);
    CHECK(voiceWithId(middle).gainLeft == Catch::Approx(0.2 * half).epsilon(1e-4));
    CHECK(voiceWithId(middle).step == Catch::Approx(0.1).epsilon(1e-3));
    CHECK(voiceWithId(far).gainLeft == Catch::Approx(0.01 * 0.125 * half).epsilon(1e-3));
    CHECK(voiceWithId(side).gainLeft == Catch::Approx(0.0).margin(1e-6));
    CHECK(voiceWithId(side).gainRight == Catch::Approx(0.2).epsilon(1e-4));
    rune::audio::shutdown();
}

TEST_CASE("ALAudio plays at most 31 sources", "[data]")
{
    AudioDevice device("ALAudio.ALAudioSubsystem");
    REQUIRE(rune::config::system().get("ALAudio.ALAudioSubsystem", "Channels").value_or("") == "32");
    REQUIRE(rune::audio::initializeOffline(44100));
    rune::USound tone;
    tone.bytes = constantWave(16384, 44100, 44100);
    for (int i = 0; i < 40; i++) {
        rune::audio::playSound(&tone, nullptr, rune::audio::SlotNone, {}, 1.0f, 1000.0f, 1.0f, false);
    }
    int alive = 0;
    for (const rune::audio::VoiceStatus& voice : rune::audio::voices()) {
        alive += voice.channel >= 0 ? 1 : 0;
    }
    CHECK(alive == 31);
    rune::audio::shutdown();
}

TEST_CASE("ALAudio ambient sounds keep playing while paused without Doppler, follow SoundPitch and light brightness", "[data]")
{
    AudioDevice device("ALAudio.ALAudioSubsystem");
    rune::objects::unloadPackage(rune::Name("DM-Hel"));
    REQUIRE(rune::audio::initializeOffline(44100));
    rune::audio::setVolumes(1.0f, 1.0f, 1.0f);
    {
        rune::World world;
        REQUIRE(world.loadMap("DM-Hel"));
        rune::UObject* player = world.player();
        REQUIRE(player != nullptr);
        rune::Vector ear = rune::actor::Location(player);
        auto ambient = []() {
            std::vector<rune::audio::VoiceStatus> result;
            for (const rune::audio::VoiceStatus& voice : rune::audio::voices()) {
                if (voice.ambient && voice.channel >= 0) {
                    result.push_back(voice);
                }
            }
            return result;
        };
        rune::audio::update(ear, {}, 0.0f);
        std::vector<rune::audio::VoiceStatus> playing = ambient();
        REQUIRE_FALSE(playing.empty());
        rune::levelinfo::Pauser(world.levelInfo) = "Player";
        rune::audio::update(ear, {}, 0.0f);
        CHECK(ambient().size() == playing.size());
        rune::levelinfo::Pauser(world.levelInfo).clear();

        rune::UObject* source = const_cast<rune::UObject*>(playing.front().actor);
        int32_t id = playing.front().id;
        double still = voiceWithId(id).step;
        float left = voiceWithId(id).gainLeft;
        rune::Vector away = rune::actor::Location(source) - ear;
        rune::actor::Velocity(source) = away.normal() * 650.0f;
        rune::audio::update(ear, {}, 0.0f);
        CHECK(voiceWithId(id).step == still);
        rune::actor::Velocity(source) = {};
        uint8_t pitch = rune::actor::SoundPitch(source);
        rune::actor::SoundPitch(source) = static_cast<uint8_t>(pitch * 2);
        rune::audio::update(ear, {}, 0.0f);
        CHECK(voiceWithId(id).step == Catch::Approx(still * 2.0).epsilon(1e-3));
        rune::actor::SoundPitch(source) = pitch;
        rune::actor::LightType(source) = 1;
        rune::actor::LightBrightness(source) = 51;
        rune::audio::update(ear, {}, 0.0f);
        CHECK(voiceWithId(id).gainLeft == Catch::Approx(left * 0.2).epsilon(1e-3));
        rune::actor::LightType(source) = 0;
        rune::objects::unloadPackage(rune::Name("DM-Hel"));
    }
    rune::audio::shutdown();
}
