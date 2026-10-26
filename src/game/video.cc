#include "game/video.h"

#include "render/gl_device.h"
#include "render/scene_renderer.h"
#include "render/soft_device.h"

#include <glad/gl.h>

#include <cstdio>

namespace rune {
    const char* windowModeName(WindowMode mode)
    {
        switch (mode) {
        case WindowMode::Borderless:
            return "borderless";
        case WindowMode::Fullscreen:
            return "fullscreen";
        case WindowMode::Windowed:
        default:
            return "windowed";
        }
    }

    Video::~Video()
    {
        close();
    }

    bool Video::open(RendererKind device, int width, int height)
    {
        return create(device, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, SDL_WINDOW_RESIZABLE);
    }

    bool Video::reopen(RendererKind device)
    {
        int x = SDL_WINDOWPOS_CENTERED;
        int y = SDL_WINDOWPOS_CENTERED;
        int width = 1280;
        int height = 720;
        uint32_t flags = SDL_WINDOW_RESIZABLE;
        if (sdlWindow) {
            SDL_GetWindowPosition(sdlWindow, &x, &y);
            SDL_GetWindowSize(sdlWindow, &width, &height);
            flags |= SDL_GetWindowFlags(sdlWindow) & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP);
        }
        RendererKind previous = current;
        close();
        if (create(device, x, y, width, height, flags)) {
            return true;
        }
        close();
        create(previous, x, y, width, height, flags);
        return false;
    }

    bool Video::create(RendererKind device, int x, int y, int width, int height, uint32_t flags)
    {
        current = device;
        if (device == RendererKind::Software) {
            sdlWindow = SDL_CreateWindow("Rune", x, y, width, height, flags);
            if (!sdlWindow) {
                std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
                return false;
            }
            presenter = SDL_CreateRenderer(sdlWindow, -1, SDL_RENDERER_ACCELERATED);
            if (!presenter) {
                presenter = SDL_CreateRenderer(sdlWindow, -1, SDL_RENDERER_SOFTWARE);
            }
            if (!presenter) {
                std::fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
                return false;
            }
            auto softDevice = std::make_unique<SoftRenderDevice>();
            software = softDevice.get();
            active = std::make_unique<SceneRenderer>(std::move(softDevice));
            return true;
        }
#ifdef __EMSCRIPTEN__
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
#else
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#endif
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        sdlWindow = SDL_CreateWindow("Rune", x, y, width, height, flags | SDL_WINDOW_OPENGL);
        if (!sdlWindow) {
            std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
            return false;
        }
        context = SDL_GL_CreateContext(sdlWindow);
        if (!context) {
            std::fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
            return false;
        }
        if (!gladLoadGL(reinterpret_cast<GLADloadfunc>(SDL_GL_GetProcAddress))) {
            std::fprintf(stderr, "failed to load OpenGL\n");
            return false;
        }
        active = std::make_unique<SceneRenderer>(std::make_unique<GLRenderDevice>());
        return true;
    }

    void Video::close()
    {
        active.reset();
        software = nullptr;
        if (screen) {
            SDL_DestroyTexture(screen);
            screen = nullptr;
        }
        screenWidth = 0;
        screenHeight = 0;
        if (presenter) {
            SDL_DestroyRenderer(presenter);
            presenter = nullptr;
        }
        if (context) {
            SDL_GL_DeleteContext(context);
            context = nullptr;
        }
        if (sdlWindow) {
            SDL_DestroyWindow(sdlWindow);
            sdlWindow = nullptr;
        }
    }

    SDL_Window* Video::window() const
    {
        return sdlWindow;
    }

    Renderer& Video::renderer() const
    {
        return *active;
    }

    RendererKind Video::device() const
    {
        return current;
    }

    WindowMode Video::windowMode() const
    {
        uint32_t flags = SDL_GetWindowFlags(sdlWindow);
        if ((flags & SDL_WINDOW_FULLSCREEN_DESKTOP) == SDL_WINDOW_FULLSCREEN_DESKTOP) {
            return WindowMode::Borderless;
        }
        return (flags & SDL_WINDOW_FULLSCREEN) ? WindowMode::Fullscreen : WindowMode::Windowed;
    }

    bool Video::setWindowMode(WindowMode mode, int width, int height)
    {
        if (mode == WindowMode::Windowed) {
            if (SDL_SetWindowFullscreen(sdlWindow, 0) != 0) {
                return false;
            }
            SDL_SetWindowSize(sdlWindow, width, height);
            return true;
        }
        if (mode == WindowMode::Borderless) {
            return SDL_SetWindowFullscreen(sdlWindow, SDL_WINDOW_FULLSCREEN_DESKTOP) == 0;
        }
        SDL_DisplayMode previous {};
        SDL_GetWindowDisplayMode(sdlWindow, &previous);
        SDL_DisplayMode wanted {};
        wanted.w = width;
        wanted.h = height;
        SDL_DisplayMode closest {};
        if (!SDL_GetClosestDisplayMode(SDL_GetWindowDisplayIndex(sdlWindow), &wanted, &closest) || SDL_SetWindowDisplayMode(sdlWindow, &closest) != 0) {
            return false;
        }
        if (SDL_SetWindowFullscreen(sdlWindow, SDL_WINDOW_FULLSCREEN) != 0) {
            SDL_SetWindowDisplayMode(sdlWindow, &previous);
            return false;
        }
        return true;
    }

    void Video::drawableSize(int& width, int& height) const
    {
        if (presenter) {
            SDL_GetRendererOutputSize(presenter, &width, &height);
            return;
        }
        SDL_GL_GetDrawableSize(sdlWindow, &width, &height);
    }

    bool Video::setSwapInterval(int interval)
    {
        if (presenter) {
            return SDL_RenderSetVSync(presenter, interval != 0 ? 1 : 0) == 0;
        }
        return SDL_GL_SetSwapInterval(interval) == 0;
    }

    void Video::present()
    {
        if (!software) {
            SDL_GL_SwapWindow(sdlWindow);
            return;
        }
        if (!screen || screenWidth != software->width() || screenHeight != software->height()) {
            if (screen) {
                SDL_DestroyTexture(screen);
            }
            screenWidth = software->width();
            screenHeight = software->height();
            screen = SDL_CreateTexture(presenter, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, screenWidth, screenHeight);
        }
        if (!screen) {
            return;
        }
        SDL_UpdateTexture(screen, nullptr, software->pixels(), screenWidth * 4);
        SDL_RenderClear(presenter);
        SDL_RenderCopy(presenter, screen, nullptr, nullptr);
        SDL_RenderPresent(presenter);
    }
}
