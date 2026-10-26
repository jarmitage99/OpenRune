#include "audio/audio.h"
#include "core/objects.h"
#include "engine/canvas.h"
#include "engine/movie.h"
#include "engine/props_engine.h"
#include "engine/world.h"

#include <catch_amalgamated.hpp>

#include <filesystem>
#include <vector>

namespace {
    struct ImageRecorder : rune::Canvas2D {
        float rect[4] = {};
        int images = 0;

        float width() const override
        {
            return 1024;
        }

        float height() const override
        {
            return 768;
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

        void drawLine(float, float, float, float, rune::LinearColor) override
        {
        }

        void drawImage(const uint8_t*, int, int, float x, float y, float w, float h) override
        {
            rect[0] = x;
            rect[1] = y;
            rect[2] = w;
            rect[3] = h;
            images++;
        }
    };

    double meanBrightness(const rune::Movie& movie)
    {
        const std::vector<uint8_t>& pixels = movie.frame();
        double sum = 0;
        for (size_t i = 0; i < pixels.size(); i += 4) {
            sum += pixels[i] + pixels[i + 1] + pixels[i + 2];
        }
        return sum / static_cast<double>(pixels.size() / 4 * 3);
    }
}

TEST_CASE("the intro movie decodes its Theora video and Vorbis audio to the end", "[data]")
{
    rune::Movie movie;
    REQUIRE(movie.open(std::filesystem::path(RUNE_GAME_DATA) / "Video" / "intro.ogv"));
    CHECK(movie.width() == 1280);
    CHECK(movie.height() == 720);
    CHECK(movie.sampleRate() == 48000);
    REQUIRE(movie.frame().size() == static_cast<size_t>(1280 * 720 * 4));

    std::vector<float> audio(2 * 4800);
    size_t samples = 0;
    double darkest = 255;
    double brightest = 0;
    uint64_t lastFrame = 0;
    int steps = 0;
    while (movie.advance(1.0 / 30.0) && steps < 30 * 60) {
        steps++;
        size_t read = 0;
        while ((read = movie.readAudio(audio.data(), 4800)) > 0) {
            samples += read;
        }
        if (movie.frameNumber() != lastFrame) {
            lastFrame = movie.frameNumber();
            double brightness = meanBrightness(movie);
            darkest = std::min(darkest, brightness);
            brightest = std::max(brightest, brightness);
        }
    }
    CHECK(movie.finished());
    CHECK(movie.time() > 14.0);
    CHECK(movie.time() < 15.0);
    CHECK(movie.frameNumber() == 411);
    double audioSeconds = static_cast<double>(samples) / 48000.0;
    CHECK(audioSeconds > movie.time() - 0.5);
    CHECK(audioSeconds < movie.time() + 0.5);
    CHECK(darkest == 0);
    CHECK(brightest == Catch::Approx(222.2801).margin(0.001));
    CHECK(movie.frame()[3] == 255);
}

TEST_CASE("a missing movie file does not open")
{
    rune::Movie movie;
    CHECK_FALSE(movie.open("does-not-exist.ogv"));
}

TEST_CASE("the startup movie pauses the intro, follows the audio clock and hands back to the menu", "[data]")
{
    rune::engine::initialize(RUNE_GAME_DATA);
    rune::objects::unloadPackage(rune::Name("intro"));
    auto world = std::make_unique<rune::World>();
    REQUIRE(world->loadMap("intro"));
    rune::UObject* player = world->player();
    REQUIRE(player != nullptr);
    REQUIRE(world->gameInfo != nullptr);
    REQUIRE(world->gameInfo->cls->name == rune::Name("RuneIntro"));
    REQUIRE(rune::audio::initializeOffline(48000));

    rune::MoviePlayer movies;
    movies.request("intro.ogv");
    movies.tick(*world, 1.0f / 30.0f);
    movies.tick(*world, 1.0f / 30.0f);
    CHECK_FALSE(movies.playing());
    movies.tick(*world, 1.0f / 30.0f);
    REQUIRE(movies.playing());
    CHECK(rune::playerpawn::RendMap(player) == 19);
    CHECK_FALSE(rune::levelinfo::Pauser(world->levelInfo).empty());

    ImageRecorder canvas;
    movies.draw(canvas, 1024, 768);
    CHECK(canvas.images == 1);
    CHECK(canvas.rect[0] == 0);
    CHECK(canvas.rect[1] == 97);
    CHECK(canvas.rect[2] == 1024);
    CHECK(canvas.rect[3] == 574);

    for (int i = 0; i < 30; i++) {
        movies.tick(*world, 1.0f / 30.0f);
    }
    REQUIRE(movies.movie() != nullptr);
    CHECK(movies.movie()->time() < 0.05);
    std::vector<float> mix(24000 * 2);
    rune::audio::render(mix.data(), 24000);
    movies.tick(*world, 1.0f / 30.0f);
    CHECK(movies.movie()->time() == Catch::Approx(0.5).margin(0.01));

    rune::playerpawn::RendMap(player) = 5;
    movies.tick(*world, 1.0f / 30.0f);
    CHECK_FALSE(movies.playing());
    CHECK(rune::playerpawn::RendMap(player) == 0);
    CHECK(rune::levelinfo::Pauser(world->levelInfo).empty());
    rune::audio::shutdown();

    REQUIRE(movies.play(*world, "intro.ogv"));
    for (int i = 0; i < 30; i++) {
        movies.tick(*world, 1.0f / 30.0f);
    }
    CHECK(movies.movie()->time() == Catch::Approx(1.0).margin(0.01));
    movies.stop(*world);
    CHECK_FALSE(movies.play(*world, "missing.ogv"));
    CHECK(rune::playerpawn::RendMap(player) == 0);
    CHECK(rune::levelinfo::Pauser(world->levelInfo).empty());
    world.reset();
    rune::objects::unloadPackage(rune::Name("intro"));
}
