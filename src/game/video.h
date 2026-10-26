#pragma once

#include "render/renderer.h"

#include <SDL.h>

#include <cstdint>
#include <memory>

namespace rune {
    class SoftRenderDevice;

    enum class WindowMode {
        Windowed,
        Borderless,
        Fullscreen,
    };

    const char* windowModeName(WindowMode mode);

    class Video {
    public:
        Video() = default;
        ~Video();
        Video(const Video&) = delete;
        Video& operator=(const Video&) = delete;

        bool open(RendererKind device, int width, int height);
        bool reopen(RendererKind device);
        void close();

        SDL_Window* window() const;
        Renderer& renderer() const;
        RendererKind device() const;
        WindowMode windowMode() const;
        bool setWindowMode(WindowMode mode, int width, int height);
        void drawableSize(int& width, int& height) const;
        bool setSwapInterval(int interval);
        void present();

    private:
        bool create(RendererKind device, int x, int y, int width, int height, uint32_t flags);

        RendererKind current = RendererKind::Hardware;
        SDL_Window* sdlWindow = nullptr;
        SDL_GLContext context = nullptr;
        SDL_Renderer* presenter = nullptr;
        SDL_Texture* screen = nullptr;
        int screenWidth = 0;
        int screenHeight = 0;
        std::unique_ptr<Renderer> active;
        SoftRenderDevice* software = nullptr;
    };
}
