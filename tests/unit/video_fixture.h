#pragma once

#include "game/video.h"

#include <catch_amalgamated.hpp>

#include <cstdlib>
#include <string>

struct VideoFixture {
    std::string driver;
    bool hadDriver = false;
    rune::Video video;

    VideoFixture()
    {
        if (const char* value = SDL_getenv("SDL_VIDEODRIVER")) {
            driver = value;
            hadDriver = true;
        }
        SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
        REQUIRE(SDL_InitSubSystem(SDL_INIT_VIDEO) == 0);
        REQUIRE(video.open(rune::RendererKind::Software, 800, 600));
    }

    ~VideoFixture()
    {
        video.close();
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        if (hadDriver) {
            SDL_setenv("SDL_VIDEODRIVER", driver.c_str(), 1);
        } else {
#ifdef _WIN32
            _putenv_s("SDL_VIDEODRIVER", "");
#else
            unsetenv("SDL_VIDEODRIVER");
#endif
        }
    }
};
