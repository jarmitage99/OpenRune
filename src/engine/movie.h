#pragma once

#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace rune {
    class Canvas2D;
    class World;

    class Movie {
    public:
        Movie();
        ~Movie();
        Movie(const Movie&) = delete;
        Movie& operator=(const Movie&) = delete;

        bool open(const std::filesystem::path& file);
        bool advance(double seconds);
        bool advanceTo(double seconds);
        bool finished() const;
        bool audioDrained() const;
        double time() const;

        int width() const;
        int height() const;
        const std::vector<uint8_t>& frame() const;
        uint64_t frameNumber() const;

        uint32_t sampleRate() const;
        size_t readAudio(float* stereo, size_t frames);

    private:
        struct State;

        bool readPage();
        bool nextVideoPacket();
        void decodeAudio(double until);
        void convertFrame();

        std::unique_ptr<State> state;
        std::ifstream input;
        std::vector<uint8_t> pixels;
        std::deque<float> audio;
        mutable std::mutex audioMutex;
        double clock = 0;
        double nextFrameTime = 0;
        double audioDecoded = 0;
        bool audioEnded = false;
        uint64_t frames = 0;
        bool videoEnded = false;
        bool ended = false;
    };

    class MoviePlayer {
    public:
        void request(const std::string& name);
        bool play(World& world, const std::string& name);
        void stop(World& world);
        void tick(World& world, float deltaSeconds);
        void draw(Canvas2D& canvas, int width, int height) const;
        bool playing() const;
        const Movie* movie() const;

    private:
        std::unique_ptr<Movie> clip;
        std::string pending;
        int ticks = 0;
        bool audioClock = false;
    };
}
