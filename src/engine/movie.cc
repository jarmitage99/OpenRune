#include "engine/movie.h"

#include "audio/audio.h"
#include "core/objects.h"
#include "core/vm.h"
#include "engine/canvas.h"
#include "engine/props_engine.h"
#include "engine/world.h"

#include <ogg/ogg.h>
#include <theora/theoradec.h>
#include <vorbis/codec.h>

#include <algorithm>
#include <cstdio>

namespace rune {
    struct Movie::State {
        ogg_sync_state sync {};
        ogg_stream_state theoraStream {};
        ogg_stream_state vorbisStream {};
        bool hasTheora = false;
        bool hasVorbis = false;
        th_info theoraInfo {};
        th_comment theoraComment {};
        th_setup_info* setup = nullptr;
        th_dec_ctx* decoder = nullptr;
        vorbis_info vorbisInfo {};
        vorbis_comment vorbisComment {};
        vorbis_dsp_state vorbisDsp {};
        vorbis_block vorbisBlock {};
        bool vorbisReady = false;
    };

    namespace {
        uint8_t clampByte(int value)
        {
            return static_cast<uint8_t>(std::clamp(value, 0, 255));
        }

        bool pullPage(ogg_sync_state& sync, std::ifstream& input, ogg_page& page)
        {
            while (ogg_sync_pageout(&sync, &page) <= 0) {
                char* buffer = ogg_sync_buffer(&sync, 4096);
                input.read(buffer, 4096);
                std::streamsize count = input.gcount();
                if (count <= 0) {
                    return false;
                }
                ogg_sync_wrote(&sync, static_cast<long>(count));
            }
            return true;
        }
    }

    Movie::Movie()
        : state(std::make_unique<State>())
    {
        ogg_sync_init(&state->sync);
        th_info_init(&state->theoraInfo);
        th_comment_init(&state->theoraComment);
        vorbis_info_init(&state->vorbisInfo);
        vorbis_comment_init(&state->vorbisComment);
    }

    Movie::~Movie()
    {
        if (state->decoder) {
            th_decode_free(state->decoder);
        }
        if (state->setup) {
            th_setup_free(state->setup);
        }
        th_comment_clear(&state->theoraComment);
        th_info_clear(&state->theoraInfo);
        if (state->vorbisReady) {
            vorbis_block_clear(&state->vorbisBlock);
            vorbis_dsp_clear(&state->vorbisDsp);
        }
        vorbis_comment_clear(&state->vorbisComment);
        vorbis_info_clear(&state->vorbisInfo);
        if (state->hasTheora) {
            ogg_stream_clear(&state->theoraStream);
        }
        if (state->hasVorbis) {
            ogg_stream_clear(&state->vorbisStream);
        }
        ogg_sync_clear(&state->sync);
    }

    bool Movie::open(const std::filesystem::path& file)
    {
        input.open(file, std::ios::binary);
        if (!input) {
            return false;
        }
        int theoraHeaders = 0;
        int vorbisHeaders = 0;
        ogg_page page;
        while (pullPage(state->sync, input, page)) {
            if (!ogg_page_bos(&page)) {
                if (state->hasTheora) {
                    ogg_stream_pagein(&state->theoraStream, &page);
                }
                if (state->hasVorbis) {
                    ogg_stream_pagein(&state->vorbisStream, &page);
                }
                break;
            }
            ogg_stream_state test;
            ogg_stream_init(&test, ogg_page_serialno(&page));
            ogg_stream_pagein(&test, &page);
            ogg_packet packet;
            if (ogg_stream_packetout(&test, &packet) <= 0) {
                ogg_stream_clear(&test);
                continue;
            }
            if (!state->hasTheora && th_decode_headerin(&state->theoraInfo, &state->theoraComment, &state->setup, &packet) > 0) {
                state->theoraStream = test;
                state->hasTheora = true;
                theoraHeaders = 1;
            } else if (!state->hasVorbis && vorbis_synthesis_headerin(&state->vorbisInfo, &state->vorbisComment, &packet) == 0) {
                state->vorbisStream = test;
                state->hasVorbis = true;
                vorbisHeaders = 1;
            } else {
                ogg_stream_clear(&test);
            }
        }
        if (!state->hasTheora) {
            return false;
        }
        while (theoraHeaders < 3 || (state->hasVorbis && vorbisHeaders < 3)) {
            ogg_packet packet;
            bool progressed = false;
            while (theoraHeaders < 3 && ogg_stream_packetout(&state->theoraStream, &packet) > 0) {
                if (th_decode_headerin(&state->theoraInfo, &state->theoraComment, &state->setup, &packet) <= 0) {
                    return false;
                }
                theoraHeaders++;
                progressed = true;
            }
            while (state->hasVorbis && vorbisHeaders < 3 && ogg_stream_packetout(&state->vorbisStream, &packet) > 0) {
                if (vorbis_synthesis_headerin(&state->vorbisInfo, &state->vorbisComment, &packet) != 0) {
                    return false;
                }
                vorbisHeaders++;
                progressed = true;
            }
            if (!progressed && !readPage()) {
                return false;
            }
        }
        state->decoder = th_decode_alloc(&state->theoraInfo, state->setup);
        if (!state->decoder) {
            return false;
        }
        if (state->hasVorbis) {
            vorbis_synthesis_init(&state->vorbisDsp, &state->vorbisInfo);
            vorbis_block_init(&state->vorbisDsp, &state->vorbisBlock);
            state->vorbisReady = true;
        }
        pixels.assign(static_cast<size_t>(width()) * static_cast<size_t>(height()) * 4, 0);
        for (size_t i = 3; i < pixels.size(); i += 4) {
            pixels[i] = 255;
        }
        decodeAudio(1.0);
        return true;
    }

    bool Movie::readPage()
    {
        ogg_page page;
        if (!pullPage(state->sync, input, page)) {
            return false;
        }
        if (state->hasTheora) {
            ogg_stream_pagein(&state->theoraStream, &page);
        }
        if (state->hasVorbis) {
            ogg_stream_pagein(&state->vorbisStream, &page);
        }
        return true;
    }

    bool Movie::nextVideoPacket()
    {
        ogg_packet packet;
        while (ogg_stream_packetout(&state->theoraStream, &packet) <= 0) {
            if (!readPage()) {
                return false;
            }
        }
        ogg_int64_t granule = -1;
        int result = th_decode_packetin(state->decoder, &packet, &granule);
        if (result == 0) {
            convertFrame();
            frames++;
        }
        if (granule >= 0) {
            nextFrameTime = th_granule_time(state->decoder, granule);
        }
        return true;
    }

    bool Movie::advance(double seconds)
    {
        return advanceTo(clock + seconds);
    }

    bool Movie::advanceTo(double seconds)
    {
        if (ended) {
            return false;
        }
        clock = std::max(clock, seconds);
        while (!videoEnded && nextFrameTime <= clock) {
            if (!nextVideoPacket()) {
                videoEnded = true;
            }
        }
        decodeAudio(clock + 1.0);
        if (videoEnded && clock >= nextFrameTime) {
            ended = true;
        }
        return !ended;
    }

    bool Movie::finished() const
    {
        return ended;
    }

    bool Movie::audioDrained() const
    {
        std::lock_guard<std::mutex> lock(audioMutex);
        return !state->vorbisReady || (audioEnded && audio.empty());
    }

    double Movie::time() const
    {
        return clock;
    }

    int Movie::width() const
    {
        return static_cast<int>(state->theoraInfo.pic_width);
    }

    int Movie::height() const
    {
        return static_cast<int>(state->theoraInfo.pic_height);
    }

    const std::vector<uint8_t>& Movie::frame() const
    {
        return pixels;
    }

    uint64_t Movie::frameNumber() const
    {
        return frames;
    }

    uint32_t Movie::sampleRate() const
    {
        return state->hasVorbis ? static_cast<uint32_t>(state->vorbisInfo.rate) : 0;
    }

    void Movie::decodeAudio(double until)
    {
        if (!state->vorbisReady) {
            return;
        }
        double rate = static_cast<double>(state->vorbisInfo.rate);
        int channels = state->vorbisInfo.channels;
        while (audioDecoded < until) {
            float** pcm = nullptr;
            int count = vorbis_synthesis_pcmout(&state->vorbisDsp, &pcm);
            if (count > 0) {
                std::lock_guard<std::mutex> lock(audioMutex);
                for (int i = 0; i < count; i++) {
                    float left = pcm[0][i];
                    float right = channels > 1 ? pcm[1][i] : left;
                    audio.push_back(left);
                    audio.push_back(right);
                }
                vorbis_synthesis_read(&state->vorbisDsp, count);
                audioDecoded += count / rate;
                continue;
            }
            ogg_packet packet;
            if (ogg_stream_packetout(&state->vorbisStream, &packet) > 0) {
                if (vorbis_synthesis(&state->vorbisBlock, &packet) == 0) {
                    vorbis_synthesis_blockin(&state->vorbisDsp, &state->vorbisBlock);
                }
                continue;
            }
            if (!readPage()) {
                audioEnded = true;
                break;
            }
        }
    }

    size_t Movie::readAudio(float* stereo, size_t count)
    {
        std::lock_guard<std::mutex> lock(audioMutex);
        size_t available = std::min(count, audio.size() / 2);
        std::copy(audio.begin(), audio.begin() + static_cast<std::ptrdiff_t>(available * 2), stereo);
        audio.erase(audio.begin(), audio.begin() + static_cast<std::ptrdiff_t>(available * 2));
        return available;
    }

    void Movie::convertFrame()
    {
        th_ycbcr_buffer planes;
        if (th_decode_ycbcr_out(state->decoder, planes) != 0) {
            return;
        }
        const th_info& info = state->theoraInfo;
        int xShift = info.pixel_fmt == TH_PF_444 ? 0 : 1;
        int yShift = info.pixel_fmt == TH_PF_420 ? 1 : 0;
        int w = width();
        int h = height();
        for (int y = 0; y < h; y++) {
            int sourceY = static_cast<int>(info.pic_y) + y;
            const uint8_t* luma = planes[0].data + sourceY * planes[0].stride;
            const uint8_t* cb = planes[1].data + (sourceY >> yShift) * planes[1].stride;
            const uint8_t* cr = planes[2].data + (sourceY >> yShift) * planes[2].stride;
            uint8_t* out = pixels.data() + static_cast<size_t>(y) * static_cast<size_t>(w) * 4;
            for (int x = 0; x < w; x++) {
                int sourceX = static_cast<int>(info.pic_x) + x;
                int c = luma[sourceX] - 16;
                int d = cb[sourceX >> xShift] - 128;
                int e = cr[sourceX >> xShift] - 128;
                out[0] = clampByte((298 * c + 409 * e + 128) >> 8);
                out[1] = clampByte((298 * c - 100 * d - 208 * e + 128) >> 8);
                out[2] = clampByte((298 * c + 516 * d + 128) >> 8);
                out[3] = 255;
                out += 4;
            }
        }
    }

    void MoviePlayer::request(const std::string& name)
    {
        pending = name;
    }

    bool MoviePlayer::play(World& world, const std::string& name)
    {
        stop(world);
        UObject* player = world.player();
        if (!player) {
            return false;
        }
        vm::Call(player, Name("SetPause")).b(true).run();
        int32_t saved = playerpawn::RendMap(player);
        playerpawn::RendMap(player) = 19;
        auto movie = std::make_unique<Movie>();
        if (!movie->open(objects::gameRoot() / "Video" / name)) {
            std::fprintf(stderr, "cannot play video %s\n", name.c_str());
            playerpawn::RendMap(player) = saved;
            vm::Call(player, Name("SetPause")).b(false).run();
            return false;
        }
        clip = std::move(movie);
        audioClock = false;
        if (clip->sampleRate() > 0) {
            Movie* source = clip.get();
            audioClock = audio::beginStream([source](float* stereo, size_t frames) {
                return source->readAudio(stereo, frames);
            },
                clip->sampleRate());
        }
        return true;
    }

    void MoviePlayer::stop(World& world)
    {
        if (!clip) {
            return;
        }
        audio::endStream();
        clip.reset();
        UObject* player = world.player();
        if (!player) {
            return;
        }
        vm::Call(player, Name("SetPause")).b(false).run();
        bool intro = world.gameInfo && world.gameInfo->cls->name == Name("RuneIntro");
        playerpawn::RendMap(player) = intro ? 0 : 5;
    }

    void MoviePlayer::tick(World& world, float deltaSeconds)
    {
        if (!pending.empty() && world.player() && ticks > 1) {
            std::string name = pending;
            pending.clear();
            play(world, name);
        }
        ticks++;
        if (!clip) {
            return;
        }
        double target = clip->time() + static_cast<double>(deltaSeconds);
        if (audioClock && !clip->audioDrained()) {
            target = audio::streamTime();
        }
        clip->advanceTo(target);
        UObject* player = world.player();
        if (clip->finished() || !player || playerpawn::RendMap(player) != 19) {
            stop(world);
        }
    }

    void MoviePlayer::draw(Canvas2D& canvas, int width, int height) const
    {
        canvas.drawRect(0, 0, static_cast<float>(width), static_cast<float>(height), { 0, 0, 0, 1 }, BlendStyle::Normal);
        if (!clip) {
            return;
        }
        int w = clip->width();
        int h = clip->height();
        if (w <= 0 || h <= 0) {
            return;
        }
        int left = 0;
        int top = 0;
        int right = width;
        int bottom = height;
        if (static_cast<int64_t>(width) * h / w > height) {
            int margin = width - w * height / h;
            left = margin - margin / 2 + 1;
            right = width - margin / 2 - 1;
        } else {
            int margin = height - h * width / w;
            top = margin - margin / 2 + 1;
            bottom = height - margin / 2 - 1;
        }
        canvas.drawImage(clip->frame().data(), w, h, static_cast<float>(left), static_cast<float>(top), static_cast<float>(right - left), static_cast<float>(bottom - top));
    }

    bool MoviePlayer::playing() const
    {
        return clip != nullptr;
    }

    const Movie* MoviePlayer::movie() const
    {
        return clip.get();
    }
}
