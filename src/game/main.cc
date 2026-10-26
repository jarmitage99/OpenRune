#include "audio/audio.h"
#include "core/config.h"
#include "core/objects.h"
#include "core/url.h"
#include "core/vm.h"
#include "engine/actor.h"
#include "engine/canvas_hooks.h"
#include "engine/console.h"
#include "engine/input.h"
#include "engine/movie.h"
#include "engine/net.h"
#include "engine/resources.h"
#include "engine/savegame.h"
#include "engine/world.h"
#include "game/video.h"
#include "render/lighting.h"
#include "render/scene_renderer.h"

#include <SDL.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {
#ifdef __EMSCRIPTEN__
    constexpr bool browser = true;
#else
    constexpr bool browser = false;
#endif

    uint8_t translateKey(SDL_Keycode key)
    {
        if (key >= SDLK_a && key <= SDLK_z) {
            return static_cast<uint8_t>(rune::IK_A + (key - SDLK_a));
        }
        if (key >= SDLK_0 && key <= SDLK_9) {
            return static_cast<uint8_t>(rune::IK_0 + (key - SDLK_0));
        }
        if (key >= SDLK_F1 && key <= SDLK_F12) {
            return static_cast<uint8_t>(rune::IK_F1 + (key - SDLK_F1));
        }
        if (key >= SDLK_KP_1 && key <= SDLK_KP_9) {
            return static_cast<uint8_t>(rune::IK_NumPad0 + 1 + (key - SDLK_KP_1));
        }
        switch (key) {
        case SDLK_KP_0:
            return rune::IK_NumPad0;
        case SDLK_BACKSPACE:
            return rune::IK_Backspace;
        case SDLK_TAB:
            return rune::IK_Tab;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            return rune::IK_Enter;
        case SDLK_LSHIFT:
        case SDLK_RSHIFT:
            return rune::IK_Shift;
        case SDLK_LCTRL:
        case SDLK_RCTRL:
            return rune::IK_Ctrl;
        case SDLK_LALT:
        case SDLK_RALT:
            return rune::IK_Alt;
        case SDLK_PAUSE:
            return rune::IK_Pause;
        case SDLK_CAPSLOCK:
            return rune::IK_CapsLock;
        case SDLK_ESCAPE:
            return rune::IK_Escape;
        case SDLK_SPACE:
            return rune::IK_Space;
        case SDLK_PAGEUP:
            return rune::IK_PageUp;
        case SDLK_PAGEDOWN:
            return rune::IK_PageDown;
        case SDLK_END:
            return rune::IK_End;
        case SDLK_HOME:
            return rune::IK_Home;
        case SDLK_LEFT:
            return rune::IK_Left;
        case SDLK_UP:
            return rune::IK_Up;
        case SDLK_RIGHT:
            return rune::IK_Right;
        case SDLK_DOWN:
            return rune::IK_Down;
        case SDLK_INSERT:
            return rune::IK_Insert;
        case SDLK_DELETE:
            return rune::IK_Delete;
        case SDLK_KP_MULTIPLY:
            return rune::IK_GreyStar;
        case SDLK_KP_PLUS:
            return rune::IK_GreyPlus;
        case SDLK_KP_MINUS:
            return rune::IK_GreyMinus;
        case SDLK_KP_PERIOD:
            return rune::IK_NumPadPeriod;
        case SDLK_KP_DIVIDE:
            return rune::IK_GreySlash;
        case SDLK_NUMLOCKCLEAR:
            return rune::IK_NumLock;
        case SDLK_SCROLLLOCK:
            return rune::IK_ScrollLock;
        case SDLK_SEMICOLON:
            return rune::IK_Semicolon;
        case SDLK_EQUALS:
            return rune::IK_Equals;
        case SDLK_COMMA:
            return rune::IK_Comma;
        case SDLK_MINUS:
            return rune::IK_Minus;
        case SDLK_PERIOD:
            return rune::IK_Period;
        case SDLK_SLASH:
            return rune::IK_Slash;
        case SDLK_BACKQUOTE:
            return rune::IK_Tilde;
        case SDLK_LEFTBRACKET:
            return rune::IK_LeftBracket;
        case SDLK_BACKSLASH:
            return rune::IK_Backslash;
        case SDLK_RIGHTBRACKET:
            return rune::IK_RightBracket;
        case SDLK_QUOTE:
            return rune::IK_SingleQuote;
        default:
            return rune::IK_None;
        }
    }

    uint8_t translateButton(uint8_t button)
    {
        switch (button) {
        case SDL_BUTTON_LEFT:
            return rune::IK_LeftMouse;
        case SDL_BUTTON_RIGHT:
            return rune::IK_RightMouse;
        case SDL_BUTTON_MIDDLE:
            return rune::IK_MiddleMouse;
        default:
            return rune::IK_None;
        }
    }

    struct Options {
        std::string map;
        std::string screenshot;
        int frames = -1;
        int width = 1280;
        int height = 720;
        bool sizeGiven = false;
        std::vector<std::string> commands;
        std::vector<std::pair<int, std::string>> actions;
        std::string renderer;
    };

    Options parseOptions(int argc, char** argv)
    {
        Options options;
        for (int i = 1; i < argc; i++) {
            std::string arg = argv[i];
            if (arg == "--screenshot" && i + 1 < argc) {
                options.screenshot = argv[++i];
            } else if (arg == "--frames" && i + 1 < argc) {
                options.frames = std::atoi(argv[++i]);
            } else if (arg == "--width" && i + 1 < argc) {
                options.width = std::atoi(argv[++i]);
                options.sizeGiven = true;
            } else if (arg == "--height" && i + 1 < argc) {
                options.height = std::atoi(argv[++i]);
                options.sizeGiven = true;
            } else if (arg == "--renderer" && i + 1 < argc) {
                options.renderer = argv[++i];
            } else if (arg == "--exec" && i + 1 < argc) {
                options.commands.push_back(argv[++i]);
            } else if (arg == "--at" && i + 1 < argc) {
                std::string spec = argv[++i];
                size_t colon = spec.find(':');
                if (colon != std::string::npos) {
                    options.actions.emplace_back(std::atoi(spec.substr(0, colon).c_str()), spec.substr(colon + 1));
                }
            } else if (arg == "-alladmin") {
                rune::World::allAdmin = true;
            } else {
                options.map = arg;
            }
        }
        return options;
    }

    void saveBmp(const std::string& path, const std::vector<uint8_t>& rgba, int width, int height)
    {
        std::vector<uint8_t> pixels = rgba;
        SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormatFrom(pixels.data(), width, height, 32, width * 4, SDL_PIXELFORMAT_ABGR8888);
        SDL_SaveBMP(surface, path.c_str());
        SDL_FreeSurface(surface);
    }

    class Game {
    public:
        explicit Game(const Options& settings)
            : options(settings)
        {
        }

        std::string videoSection()
        {
            return rune::config::system().get("Engine.Engine", "ViewportManager").value_or("WinDrv.WindowsClient");
        }

        int videoSetting(const char* key, int fallback)
        {
            std::optional<std::string> value = rune::config::system().get(videoSection(), key);
            if (!value) {
                return fallback;
            }
            if (*value == "True" || *value == "true") {
                return 1;
            }
            if (*value == "False" || *value == "false") {
                return 0;
            }
            return std::atoi(value->c_str());
        }

        rune::WindowMode configuredWindowMode()
        {
            auto mode = rune::config::system().get(videoSection(), "WindowMode");
            if (mode) {
                if (rune::iequals(*mode, "windowed")) {
                    return rune::WindowMode::Windowed;
                }
                if (rune::iequals(*mode, "borderless")) {
                    return rune::WindowMode::Borderless;
                }
                if (rune::iequals(*mode, "fullscreen")) {
                    return rune::WindowMode::Fullscreen;
                }
            }
            return videoSetting("StartupFullscreen", 0) ? rune::WindowMode::Fullscreen : rune::WindowMode::Windowed;
        }

        void saveVideoMode()
        {
            rune::Ini& ini = rune::config::system();
            std::string section = videoSection();
            auto mode = video.windowMode();
            if (mode != rune::WindowMode::Borderless) {
                int width = 0;
                int height = 0;
                SDL_GetWindowSize(window, &width, &height);
                bool fullscreen = mode == rune::WindowMode::Fullscreen;
                ini.set(section, fullscreen ? "FullscreenViewportX" : "WindowedViewportX", std::to_string(width));
                ini.set(section, fullscreen ? "FullscreenViewportY" : "WindowedViewportY", std::to_string(height));
            }
            ini.set(section, "WindowMode", rune::windowModeName(mode));
            ini.set(section, "StartupFullscreen", mode == rune::WindowMode::Windowed ? "False" : "True");
            ini.save();
        }

        bool applyVideoMode(rune::WindowMode mode, int width, int height)
        {
            if (browser && mode == rune::WindowMode::Fullscreen) {
                mode = rune::WindowMode::Borderless;
            }
            if (!video.setWindowMode(mode, width, height)) {
                std::fprintf(stderr, "cannot change window mode: %s\n", SDL_GetError());
                return false;
            }
            return true;
        }

        bool changeWindowMode(rune::WindowMode mode)
        {
            int previousWidth = 0;
            int previousHeight = 0;
            SDL_GetWindowSize(window, &previousWidth, &previousHeight);
            bool wasWindowed = video.windowMode() == rune::WindowMode::Windowed;
            bool windowed = mode == rune::WindowMode::Windowed;
            int width = videoSetting(windowed ? "WindowedViewportX" : "FullscreenViewportX", options.width);
            int height = videoSetting(windowed ? "WindowedViewportY" : "FullscreenViewportY", options.height);
            if (!applyVideoMode(mode, width, height)) {
                return false;
            }
            if (wasWindowed && !windowed) {
                auto& ini = rune::config::system();
                ini.set(videoSection(), "WindowedViewportX", std::to_string(previousWidth));
                ini.set(videoSection(), "WindowedViewportY", std::to_string(previousHeight));
            }
            saveVideoMode();
            return true;
        }

        std::string availableResolutions()
        {
            int display = SDL_GetWindowDisplayIndex(window);
            std::vector<std::pair<int, int>> sizes;
            for (int i = 0; i < SDL_GetNumDisplayModes(display); i++) {
                SDL_DisplayMode mode {};
                if (SDL_GetDisplayMode(display, i, &mode) == 0 && mode.w >= 640 && mode.h >= 480) {
                    sizes.emplace_back(mode.w, mode.h);
                }
            }
            std::sort(sizes.begin(), sizes.end());
            sizes.erase(std::unique(sizes.begin(), sizes.end()), sizes.end());
            if (sizes.size() > 16) {
                sizes.erase(sizes.begin(), sizes.end() - 16);
            }
            std::string text;
            for (const auto& [width, height] : sizes) {
                if (!text.empty()) {
                    text += " ";
                }
                text += std::to_string(width) + "x" + std::to_string(height);
            }
            return text;
        }

        float displayFov(float fov, int width, int height)
        {
            int index = std::clamp(videoSetting("FieldOfViewIndex", 3), 0, 12);
            float wanted = 60.0f + 5.0f * static_cast<float>(index);
            float adjusted = fov + (wanted - 75.0f) * std::clamp((fov - 25.0f) / 50.0f, 0.0f, 1.0f);
            float aspect = static_cast<float>(width) / static_cast<float>(height) * 0.75f;
            return std::atan(std::tan(adjusted * rune::Pi / 360.0f) * aspect) * 360.0f / rune::Pi;
        }

        void applyRenderOptions()
        {
            rune::RenderOptions render;
            render.anisotropy = static_cast<float>(1 << std::clamp(videoSetting("Anisotropy", 0), 0, 4));
            render.trilinear = videoSetting("UseTrilinear", 0) != 0;
            int multisample = std::clamp(videoSetting("Multisampling", 0), 0, 3);
            render.multisample = multisample > 0 ? 1 << multisample : 0;
            render.fxaa = videoSetting("FXAA", 0) != 0;
            render.smoothLightmaps = videoSetting("SmoothLightmaps", 0) != 0;
            const float detailDistances[] = { 380.0f, 1024.0f, 2048.0f };
            render.detailDistance = detailDistances[std::clamp(videoSetting("DetailDistance", 0), 0, 2)];
            render.fogDistance = static_cast<float>(std::atof(rune::config::system().get(videoSection(), "FogDistPercentage").value_or("1").c_str()));
            render.lodPercentage = static_cast<float>(std::atof(rune::config::system().get(videoSection(), "LodPercentage").value_or("1").c_str()));
            render.playerShadows = videoSetting("PlayerShadows", 0);
            render.actorShadows = videoSetting("ActorShadows", 0);
            renderer->setOptions(render);
            if (browser || options.frames >= 0) {
                return;
            }
            int vsync = std::clamp(videoSetting("VSync", 1), 0, 2);
            if (vsync != appliedVSync) {
                appliedVSync = vsync;
                if (vsync != 2 || !video.setSwapInterval(-1)) {
                    video.setSwapInterval(std::min(vsync, 1));
                }
            }
        }

        void limitFrameRate()
        {
            const int limits[] = { 0, 30, 60, 75, 120, 144, 165, 240 };
            int limit = limits[std::clamp(videoSetting("FrameRateLimit", 0), 0, 7)];
            if (browser || limit == 0 || options.frames >= 0) {
                return;
            }
            uint64_t frequency = SDL_GetPerformanceFrequency();
            uint64_t deadline = frameDeadline + frequency / static_cast<uint64_t>(limit);
            uint64_t now = SDL_GetPerformanceCounter();
            if (now >= deadline) {
                frameDeadline = now;
                return;
            }
            while (now < deadline) {
                uint64_t remaining = (deadline - now) * 1000 / frequency;
                if (remaining > 1) {
                    SDL_Delay(static_cast<uint32_t>(remaining - 1));
                }
                now = SDL_GetPerformanceCounter();
            }
            frameDeadline = deadline;
        }

        bool start()
        {
            rune::engine::initialize(rune::config::findGameRoot(RUNE_GAME_DATA).string());
            rune::audio::initialize();
            rune::audio::setLightRandom(rune::lightRandomBase);
            attachExtraOptions();
            if (!openVideo()) {
                return false;
            }
            rune::console::openMapHook = [this](const std::string& url, int32_t travelType) {
                pendingMap = url;
                pendingTravelType = travelType;
            };
            rune::console::quitHook = [this]() {
                running = false;
            };
            rune::console::currentResHook = [this]() {
                int width = 0;
                int height = 0;
                SDL_GetWindowSize(window, &width, &height);
                return std::to_string(width) + "x" + std::to_string(height);
            };
            rune::console::resolutionsHook = [this]() {
                return availableResolutions();
            };
            rune::console::setResHook = [this](int width, int height) {
                if (applyVideoMode(video.windowMode(), width, height)) {
                    saveVideoMode();
                }
            };
            rune::console::currentWindowModeHook = [this]() {
                return std::string(rune::windowModeName(video.windowMode()));
            };
            rune::console::setWindowModeHook = [this](const std::string& mode) {
                auto requested = mode == "borderless" ? rune::WindowMode::Borderless : mode == "fullscreen" ? rune::WindowMode::Fullscreen
                                                                                                            : rune::WindowMode::Windowed;
                return changeWindowMode(requested);
            };
            rune::console::toggleFullscreenHook = [this]() {
                changeWindowMode(video.windowMode() == rune::WindowMode::Windowed ? rune::WindowMode::Fullscreen : rune::WindowMode::Windowed);
            };
            rune::console::paintProgressHook = [this]() {
                paintProgress();
            };
            rune::console::screenshotHook = [this]() {
                std::filesystem::path directory = rune::config::system().path.parent_path() / "Screenshots";
                std::filesystem::create_directories(directory);
                int index = 0;
                std::filesystem::path file;
                do {
                    char name[32];
                    std::snprintf(name, sizeof(name), "Shot%04d.bmp", index++);
                    file = directory / name;
                } while (std::filesystem::exists(file));
                pendingShot = file.string();
            };
            rune::console::playVideoHook = [this](const std::string& name) {
                if (world) {
                    movies.play(*world, name);
                }
            };
            rune::console::stopVideoHook = [this]() {
                if (world) {
                    movies.stop(*world);
                }
            };
            if (options.map.empty()) {
                movies.request(rune::config::system().get("URL", "Video").value_or(""));
            }
            rune::console::applyAudioSettings();
            if (options.frames < 0 && !options.sizeGiven) {
                auto mode = configuredWindowMode();
                bool windowed = mode == rune::WindowMode::Windowed;
                applyVideoMode(mode, videoSetting(windowed ? "WindowedViewportX" : "FullscreenViewportX", options.width), videoSetting(windowed ? "WindowedViewportY" : "FullscreenViewportY", options.height));
            }
            rune::CanvasHooks& hooks = rune::canvasHooks();
            hooks.drawActor = [this](rune::UObject* actor, bool wireframe, bool clearZ, const rune::ActorFrame& region) {
                if (region.clipped) {
                    renderer->drawActorOverlay(actor, {}, {}, region.fov, wireframe, clearZ, region.x, region.y, region.width, region.height);
                } else {
                    renderer->drawActorOverlay(actor, viewLocation, viewRotation, viewFov, wireframe, clearZ, 0, 0, 0, 0);
                }
            };
            hooks.drawPortal = [this](int x, int y, int width, int height, rune::UObject* camera, const rune::Vector& location, const rune::Rotator& rotation, float fov, bool clearZ) {
                if (world && world->level) {
                    renderer->drawPortal(world->level, world->actors, location, rotation, fov, camera, x, y, width, height, clearZ);
                }
            };
            hooks.clipboardCopy = [](const std::string& text) {
                SDL_SetClipboardText(text.c_str());
            };
            hooks.clipboardPaste = []() {
                char* text = SDL_GetClipboardText();
                std::string result = text ? text : "";
                SDL_free(text);
                return result;
            };
            std::string map = options.map;
            std::filesystem::path mapFile = map;
            if (mapFile.has_parent_path() && std::filesystem::is_regular_file(mapFile)) {
                map = mapFile.stem().string();
                rune::objects::loadPackageFile(rune::Name(map), mapFile, false);
            }
            if (map.empty()) {
                auto local = rune::config::system().get("URL", "LocalMap");
                map = local ? *local : "Entry";
            }
            entryWorld = std::make_unique<rune::World>();
            entryWorld->loginPlayer = false;
            if (entryWorld->loadMap("Entry")) {
                rune::World::entryLevel = entryWorld.get();
            } else {
                entryWorld.reset();
            }
            rune::World::current = nullptr;
            return openMap(map);
        }

        bool openMap(const std::string& url, bool carryItems = false, bool serverTravel = false)
        {
            if (rune::net::isHostUrl(url.substr(0, url.find('?')))) {
                return joinServer(url);
            }
            std::vector<rune::TravelActor> travel;
            std::vector<std::pair<std::string, std::string>> travelInfo;
            if (carryItems && world) {
                travel = world->collectTravel();
                rune::UObject* player = world->player();
                if (player) {
                    rune::UObject* replication = rune::pawn::PlayerReplicationInfo(player);
                    std::string key = replication ? rune::playerreplicationinfo::PlayerName(replication) : std::string("Player");
                    travelInfo.emplace_back(key, world->travelString(player));
                }
            }
            std::string urlMovie = rune::urlOption(url, "video");
            if (!urlMovie.empty()) {
                movies.request(urlMovie);
            }
            std::string target = url;
            if (world && !target.empty() && target[0] == '?') {
                target = world->url.substr(0, world->url.find('?')) + target;
            }
            int32_t loadIndex = 0;
            bool loading = rune::savegame::loadRequested(target, loadIndex);
            if (loading && !rune::savegame::prepareLoad(loadIndex)) {
                std::fprintf(stderr, "cannot load saved game %d\n", loadIndex);
                rune::World::releaseTravel(travel);
                return false;
            }
            if (world && world->levelInfo && world->viewport) {
                rune::levelinfo::LevelAction(world->levelInfo) = 1;
                rune::levelinfo::Pauser(world->levelInfo).clear();
                if (rune::UObject* player = world->player()) {
                    rune::playerpawn::bShowMenu(player) = 0;
                }
                paintProgress();
                rune::levelinfo::LevelAction(world->levelInfo) = 0;
            }
            renderer->flushLevel();
            std::vector<std::pair<std::string, std::string>> previousTravelInfo;
            if (world && world->level) {
                previousTravelInfo = world->level->travelInfo;
            }
            rune::UObject* viewport = world ? world->viewport : nullptr;
            if (rune::UObject* console = scriptConsole()) {
                rune::vm::event(console, rune::Name("NotifyLevelChange"));
            }
            if (netDriver) {
                netDriver->attach(nullptr);
            }
            if (world && world->level) {
                rune::objects::unloadPackage(world->level->outermost()->name);
            }
            bool restart = !loading && (target.find("?restart") != std::string::npos || target.find("?RESTART") != std::string::npos);
            if (loading || restart) {
                rune::World::releaseTravel(travel);
            }
            if (restart && world) {
                travel = std::move(world->arrivalTravel);
                world->arrivalTravel.clear();
            }
            world.reset();
            canvasObject = nullptr;
            world = std::make_unique<rune::World>();
            world->viewport = viewport;
            world->incomingTravel = std::move(travel);
            if (!loading) {
                if (restart) {
                    travelInfo = previousTravelInfo;
                }
                world->arrivalTravelInfo = std::move(travelInfo);
                world->replaceTravelInfo = true;
            }
            world->inputHook = [](rune::UObject* player, float delta) {
                rune::console::readInput(player, delta);
            };
            bool hosting = hasOption(target, "listen") || (serverTravel && netDriver && netDriver->server());
            if (netDriver && (!hosting || !netDriver->server())) {
                closeNet();
            }
            if (hosting) {
                world->netMode = rune::net::NM_ListenServer;
                world->netDriver = netDriver.get();
            }
            if (!world->loadMap(target)) {
                return false;
            }
            if (hosting) {
                if (!netDriver) {
                    std::string error;
                    netDriver = rune::net::Driver::listen(listenPort(target), error);
                    if (!netDriver) {
                        std::fprintf(stderr, "%s\n", error.c_str());
                    }
                }
                world->netDriver = netDriver.get();
                if (netDriver) {
                    netDriver->attach(world.get());
                    world->spawnServerActors(rune::config::system().getAll("Engine.GameEngine", "ServerActors"));
                }
            }
            if (loading) {
                world->url = world->level->url;
            }
            rune::console::resetInput(world->player());
            precacheOnFlip = video.device() == rune::RendererKind::Hardware && rune::config::system().get(hardwareDevice, "UsePrecache").value_or("False") == "True";
            return true;
        }

        void paintProgress()
        {
            int width = 0;
            int height = 0;
            video.drawableSize(width, height);
            render(width, height);
            renderer->applyBrightness(static_cast<float>(std::atof(rune::config::system().get(videoSection(), "Brightness").value_or("0.5").c_str())));
            video.present();
        }

        rune::UObject* canvas(int width, int height)
        {
            if (!canvasObject) {
                canvasObject = rune::objects::newObject(rune::engineClasses().canvas, rune::objects::packageObject(rune::Name("Transient")), rune::Name("Canvas0"), 0);
                rune::canvas::Viewport(canvasObject) = world->viewport;
            }
            rune::vm::event(canvasObject, rune::Name("Reset"));
            rune::canvas::SizeX(canvasObject) = width;
            rune::canvas::SizeY(canvasObject) = height;
            rune::canvas::ClipX(canvasObject) = static_cast<float>(width);
            rune::canvas::ClipY(canvasObject) = static_cast<float>(height);
            return canvasObject;
        }

        void drawScreenFlash(rune::UObject* player, int width, int height)
        {
            rune::LinearColor color;
            if (rune::screenFlash(player, color)) {
                renderer->canvas().drawRect(0, 0, static_cast<float>(width), static_cast<float>(height), color, rune::BlendStyle::Highlighted);
            }
        }

        rune::UObject* scriptConsole()
        {
            if (!world || !world->viewport || !world->player()) {
                return nullptr;
            }
            return rune::player::Console(world->viewport);
        }

        bool consoleKeyEvent(uint8_t key, uint8_t action, float delta)
        {
            rune::UObject* console = scriptConsole();
            if (!console) {
                return false;
            }
            rune::vm::Call call(console, rune::Name("KeyEvent"));
            if (!call.valid()) {
                return false;
            }
            call.byte(key).byte(action).f(delta).run();
            return call.returnValue() && *reinterpret_cast<uint32_t*>(call.returnValue());
        }

        void charEvent(char c)
        {
            rune::UObject* console = scriptConsole();
            if (console) {
                rune::vm::Call call(console, rune::Name("KeyType"));
                if (call.valid()) {
                    call.byte(static_cast<uint8_t>(c)).run();
                    if (call.returnValue() && *reinterpret_cast<uint32_t*>(call.returnValue())) {
                        return;
                    }
                }
            }
        }

        void handleKey(uint8_t key, bool pressed)
        {
            if (key == rune::IK_None) {
                return;
            }
            uint8_t action = pressed ? rune::IST_Press : rune::IST_Release;
            if (consoleKeyEvent(key, action, 0)) {
                return;
            }
            rune::console::key(key, action, 0);
        }

        void mouseAxis(uint8_t key, float delta)
        {
            if (!consoleKeyEvent(key, rune::IST_Axis, delta)) {
                rune::console::key(key, rune::IST_Axis, delta);
            }
        }

        void processEvents()
        {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                switch (event.type) {
                case SDL_QUIT:
                    running = false;
                    break;
                case SDL_KEYDOWN:
                    if (!event.key.repeat) {
                        handleKey(translateKey(event.key.keysym.sym), true);
                    } else {
                        consoleKeyEvent(translateKey(event.key.keysym.sym), rune::IST_Hold, 0);
                    }
                    break;
                case SDL_KEYUP:
                    handleKey(translateKey(event.key.keysym.sym), false);
                    break;
                case SDL_TEXTINPUT:
                    for (const char* c = event.text.text; *c; c++) {
                        charEvent(*c);
                    }
                    break;
                case SDL_MOUSEMOTION:
                    mouseAxis(rune::IK_MouseX, static_cast<float>(event.motion.xrel));
                    mouseAxis(rune::IK_MouseY, static_cast<float>(-event.motion.yrel));
                    break;
                case SDL_MOUSEBUTTONDOWN:
                case SDL_MOUSEBUTTONUP:
                    handleKey(translateButton(event.button.button), event.type == SDL_MOUSEBUTTONDOWN);
                    break;
                case SDL_MOUSEWHEEL: {
                    uint8_t key = event.wheel.y > 0 ? rune::IK_MouseWheelUp : rune::IK_MouseWheelDown;
                    if (event.wheel.y != 0) {
                        mouseAxis(rune::IK_MouseW, static_cast<float>(event.wheel.y * 120));
                        handleKey(key, true);
                        handleKey(key, false);
                    }
                    break;
                }
                default:
                    break;
                }
            }
        }

        void handleTravel()
        {
            if (!pendingMap.empty()) {
                std::string url = rune::travelUrl(world ? world->url : std::string(), pendingMap, pendingTravelType);
                pendingMap.clear();
                openMap(url);
                return;
            }
            if (world && world->travelPending) {
                std::string url = rune::travelUrl(world->url, world->travelUrl, world->travelType);
                bool items = world->travelItems;
                world->travelPending = false;
                bool following = netDriver && !netDriver->server() && world->travelType == rune::TRAVEL_Relative && !rune::net::isHostUrl(world->travelUrl.substr(0, world->travelUrl.find('?')));
                if (following) {
                    return;
                }
                openMap(url, items, true);
                return;
            }
            std::string welcome;
            if (netDriver && !netDriver->server() && netDriver->takeWelcome(welcome)) {
                startClientWorld(welcome);
                return;
            }
            if (netDriver && netDriver->disconnected()) {
                std::fprintf(stderr, "disconnected: %s\n", netDriver->reason().c_str());
                auto local = rune::config::system().get("URL", "LocalMap");
                closeNet();
                openMap(local ? *local : "Entry");
            }
        }

        uint16_t listenPort(const std::string& url)
        {
            size_t mark = url.find('?');
            while (mark != std::string::npos) {
                size_t next = url.find('?', mark + 1);
                std::string option = url.substr(mark + 1, next == std::string::npos ? std::string::npos : next - mark - 1);
                if (option.size() > 5 && rune::iequals(option.substr(0, 5), "port=")) {
                    return static_cast<uint16_t>(std::atoi(option.substr(5).c_str()));
                }
                mark = next;
            }
            auto configured = rune::config::system().get("URL", "Port");
            return configured ? static_cast<uint16_t>(std::atoi(configured->c_str())) : rune::net::DefaultPort;
        }

        bool hasOption(const std::string& url, const std::string& key)
        {
            size_t mark = url.find('?');
            while (mark != std::string::npos) {
                size_t next = url.find('?', mark + 1);
                std::string option = url.substr(mark + 1, next == std::string::npos ? std::string::npos : next - mark - 1);
                std::string name = option.substr(0, option.find('='));
                if (rune::iequals(name, key)) {
                    return true;
                }
                mark = next;
            }
            return false;
        }

        void closeNet()
        {
            if (world) {
                world->netDriver = nullptr;
            }
            netDriver.reset();
        }

        bool joinServer(const std::string& url)
        {
            std::string address = url.substr(0, url.find('?'));
            std::string loginOptions = url.substr(address.size());
            for (const auto& [key, value] : rune::config::user().section("DefaultPlayer")) {
                if (!hasOption(loginOptions, key)) {
                    loginOptions += "?" + key + "=" + value;
                }
            }
            std::string host = address.substr(0, address.find(':'));
            uint16_t port = rune::net::DefaultPort;
            if (address.find(':') != std::string::npos) {
                port = static_cast<uint16_t>(std::atoi(address.substr(address.find(':') + 1).c_str()));
            }
            closeNet();
            std::string error;
            std::unique_ptr<rune::net::Driver> driver = rune::net::Driver::connect(host, port, loginOptions, error);
            std::string map;
            if (!driver || !driver->waitForWelcome(10.0, map, error)) {
                std::fprintf(stderr, "cannot join %s: %s\n", address.c_str(), error.c_str());
                return false;
            }
            netDriver = std::move(driver);
            return startClientWorld(map);
        }

        bool startClientWorld(const std::string& map)
        {
            if (!replaceWorld()) {
                return false;
            }
            world->netMode = rune::net::NM_Client;
            world->netDriver = netDriver.get();
            netDriver->attach(world.get());
            if (!world->loadMap(map)) {
                closeNet();
                return false;
            }
            netDriver->join();
            return true;
        }

        bool replaceWorld()
        {
            renderer->flushLevel();
            rune::UObject* viewport = world ? world->viewport : nullptr;
            if (rune::UObject* console = scriptConsole()) {
                rune::vm::event(console, rune::Name("NotifyLevelChange"));
            }
            if (netDriver) {
                netDriver->attach(nullptr);
            }
            if (world && world->level) {
                rune::objects::unloadPackage(world->level->outermost()->name);
            }
            world.reset();
            canvasObject = nullptr;
            world = std::make_unique<rune::World>();
            world->viewport = viewport;
            world->inputHook = [](rune::UObject* player, float delta) {
                rune::console::readInput(player, delta);
            };
            return true;
        }

        void attachExtraOptions()
        {
            rune::UClass* optionsTop = rune::objects::findClass("RMenu.RuneMenuOptionsTop");
            rune::UClass* extra = rune::objects::findClass("RuneExtra.RuneExtraOptionsTop");
            if (optionsTop && extra) {
                rune::objects::replaceClass(optionsTop, extra);
            }
            rune::UClass* videoPage = rune::objects::findClass("RMenu.RuneMenuVideoOptionsClientWindow");
            rune::UClass* extraVideo = rune::objects::findClass("RuneExtra.RuneExtraVideoOptionsClientWindow");
            if (videoPage && extraVideo) {
                rune::objects::replaceClass(videoPage, extraVideo);
            }
            rune::UClass* hud = rune::objects::findClass("RuneI.RuneHUD");
            rune::UClass* extraHud = rune::objects::findClass("RuneExtra.RuneExtraHUD");
            if (hud && extraHud) {
                rune::objects::replaceClass(hud, extraHud);
            }
            rune::UClass* page = rune::objects::findClass("RuneExtra.RuneExtraOptionsClientWindow");
            rune::UProperty* property = page ? page->findProperty(rune::Name("bLargeText")) : nullptr;
            if (property) {
                largeText = reinterpret_cast<uint32_t*>(page->defaultObject->data + property->offset);
            }
        }

        void render(int width, int height)
        {
            uint8_t levelAction = 0;
            bool precaching = precacheOnFlip && world && world->levelInfo;
            if (precaching) {
                levelAction = rune::levelinfo::LevelAction(world->levelInfo);
                rune::levelinfo::LevelAction(world->levelInfo) = 4;
            }
            drawFrame(width, height);
            if (precaching) {
                rune::levelinfo::LevelAction(world->levelInfo) = levelAction;
                precacheOnFlip = false;
            }
        }

        void drawFrame(int width, int height)
        {
            if (world) {
                renderer->setTime(world->timeSeconds);
            }
            renderer->setViewportTime(viewportTime);
            renderer->beginFrame(width, height);
            if (movies.playing()) {
                movies.draw(renderer->canvas(), width, height);
                renderer->endFrame();
                return;
            }
            viewLocation = {};
            viewRotation = {};
            viewFov = 90;
            if (world && world->calcView(viewLocation, viewRotation, viewFov)) {
                viewFov = displayFov(viewFov, width, height);
                rune::UObject* player = world->player();
                rune::UObject* viewActor = nullptr;
                if (player && !rune::pawn::bBehindView(player)) {
                    viewActor = rune::playerpawn::ViewTarget(player) ? rune::playerpawn::ViewTarget(player) : player;
                }
                bool drawWorld = !player || rune::playerpawn::RendMap(player) != 0;
                rune::UObject* canvasActor = canvas(width, height);
                rune::CanvasHooks& hooks = rune::canvasHooks();
                hooks.hasView = true;
                hooks.viewLocation = viewLocation;
                hooks.viewRotation = viewRotation;
                hooks.viewFov = viewFov;
                if (player && !player->destroyed) {
                    rune::vm::Call(player, rune::Name("PreRender")).obj(canvasActor).run();
                }
                if (drawWorld) {
                    renderer->drawScene(world->level, world->actors, viewLocation, viewRotation, viewFov, viewActor);
                }
                if (player && !player->destroyed) {
                    if (viewActor && drawWorld) {
                        rune::vm::Call(viewActor, rune::Name("RenderOverlays")).obj(canvasActor).run();
                    }
                    if (drawWorld) {
                        drawScreenFlash(player, width, height);
                    }
                    rune::postRenderHud(player, canvasActor, largeText && *largeText ? 2.0f : 1.0f);
                    rune::UObject* console = scriptConsole();
                    if (console) {
                        rune::console::FrameX(console) = static_cast<float>(width);
                        rune::console::FrameY(console) = static_cast<float>(height);
                        rune::vm::Call(console, rune::Name("PostRender")).obj(canvasActor).run();
                    }
                }
                hooks.hasView = false;
            }
            renderer->endFrame();
        }

        void scriptedAction(const std::string& action)
        {
            size_t space = action.find(' ');
            std::string verb = action.substr(0, space);
            std::string rest = space == std::string::npos ? std::string() : action.substr(space + 1);
            if (verb == "press" || verb == "release") {
                handleKey(rune::console::keyFromName(rest), verb == "press");
            } else if (verb == "type") {
                for (char c : rest) {
                    charEvent(c);
                }
            } else if (verb == "mouse") {
                float dx = 0;
                float dy = 0;
                std::sscanf(rest.c_str(), "%f %f", &dx, &dy);
                mouseAxis(rune::IK_MouseX, dx);
                mouseAxis(rune::IK_MouseY, -dy);
            } else if (verb == "exec") {
                std::string output = rune::console::execute(world ? world->player() : nullptr, rest);
                if (!output.empty()) {
                    std::printf("%s\n", output.c_str());
                }
            } else if (verb == "shot") {
                pendingShot = rest;
            } else if (verb == "state") {
                rune::UObject* player = world ? world->player() : nullptr;
                if (player) {
                    rune::Vector location = rune::actor::Location(player);
                    std::printf("player at %.1f %.1f %.1f yaw %d state %s health %d\n", static_cast<double>(location.x), static_cast<double>(location.y), static_cast<double>(location.z), rune::actor::Rotation(player).yaw & 65535, rune::vm::stateName(player).c_str(), rune::pawn::Health(player));
                }
            }
        }

        void begin()
        {
            for (const std::string& command : options.commands) {
                rune::console::execute(world ? world->player() : nullptr, command);
            }
            last = SDL_GetPerformanceCounter();
        }

        int run()
        {
            begin();
            while (running) {
                step();
            }
            return 0;
        }

        void step()
        {
            processEvents();
            applyRenderDevice();
            uint64_t frequency = SDL_GetPerformanceFrequency();
            uint64_t now = SDL_GetPerformanceCounter();
            while (options.frames < 0 && now - last < frequency / 1000) {
                SDL_Delay(0);
                now = SDL_GetPerformanceCounter();
            }
            float delta = static_cast<float>(now - last) / static_cast<float>(frequency);
            last = now;
            if (options.frames >= 0) {
                delta = 1.0f / 30.0f;
            }
            viewportTime += static_cast<double>(delta);
            applyRenderOptions();
            if (world) {
                world->tick(delta);
            }
            if (entryWorld && world && world.get() != entryWorld.get()) {
                entryWorld->tick(delta);
            }
            if (world) {
                movies.tick(*world, delta);
            }
            rune::UObject* console = scriptConsole();
            if (console) {
                rune::vm::Call(console, rune::Name("Tick")).f(delta).run();
                int height = 0;
                int width = 0;
                video.drawableSize(width, height);
                rune::console::ConsoleLines(console) = static_cast<int32_t>(std::min(rune::console::ConsolePos(console) * static_cast<float>(height), static_cast<float>(height)));
            }
            handleTravel();
            int width = 0;
            int height = 0;
            video.drawableSize(width, height);
            render(width, height);
            renderer->applyBrightness(static_cast<float>(std::atof(rune::config::system().get(videoSection(), "Brightness").value_or("0.5").c_str())));
            rune::audio::update(viewLocation, viewRotation, delta);
            if (!pendingShot.empty()) {
                std::vector<uint8_t> pixels;
                int shotWidth = 0;
                int shotHeight = 0;
                renderer->readPixels(pixels, shotWidth, shotHeight);
                saveBmp(pendingShot, pixels, shotWidth, shotHeight);
                pendingShot.clear();
            }
            frame++;
            for (const auto& [actionFrame, action] : options.actions) {
                if (actionFrame == frame) {
                    scriptedAction(action);
                }
            }
            if (options.frames >= 0 && frame >= options.frames) {
                if (!options.screenshot.empty()) {
                    std::vector<uint8_t> pixels;
                    int shotWidth = 0;
                    int shotHeight = 0;
                    renderer->readPixels(pixels, shotWidth, shotHeight);
                    saveBmp(options.screenshot, pixels, shotWidth, shotHeight);
                }
                running = false;
            }
            video.present();
            limitFrameRate();
        }

        bool isRunning() const
        {
            return running;
        }

        void pressEscape()
        {
            rune::UObject* console = scriptConsole();
            if (!movies.playing() && console && rune::vm::stateName(console) == rune::Name("UWindow")) {
                return;
            }
            handleKey(rune::IK_Escape, true);
            handleKey(rune::IK_Escape, false);
        }

    private:
        rune::RendererKind requestedDevice() const
        {
            if (options.renderer == "soft" || options.renderer == "software") {
                return rune::RendererKind::Software;
            }
            if (options.renderer == "gl" || options.renderer == "opengl") {
                return rune::RendererKind::Hardware;
            }
            return rune::rendererKindOf(rune::config::system().get("Engine.Engine", "GameRenderDevice").value_or(""));
        }

        bool openVideo()
        {
            std::string configured = rune::config::system().get("Engine.Engine", "GameRenderDevice").value_or("");
            if (rune::rendererKindOf(configured) == rune::RendererKind::Hardware && !configured.empty()) {
                hardwareDevice = configured;
            }
            if (!video.open(requestedDevice(), options.width, options.height)) {
                return false;
            }
            attachVideo();
            rune::vm::setCriticalHandler([this](const std::string& message) {
                std::fprintf(stderr, "Critical Error: %s\n", message.c_str());
                std::fflush(stderr);
                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Critical Error", message.c_str(), window);
                std::_Exit(1);
            });
            rune::console::changeVideoHook = [this]() {
                pendingDevice = video.device() == rune::RendererKind::Software ? rune::RendererKind::Hardware : rune::RendererKind::Software;
            };
            return true;
        }

        void attachVideo()
        {
            window = video.window();
            renderer = &video.renderer();
            rune::canvasHooks().target = &renderer->canvas();
            appliedVSync = -1;
            if (options.frames >= 0) {
                video.setSwapInterval(0);
            }
            SDL_ShowCursor(SDL_DISABLE);
            SDL_SetRelativeMouseMode(SDL_TRUE);
        }

        void applyRenderDevice()
        {
            if (!pendingDevice) {
                return;
            }
            rune::RendererKind device = *pendingDevice;
            pendingDevice.reset();
            if (device == video.device()) {
                return;
            }
            auto mode = video.windowMode();
            int width = 0;
            int height = 0;
            SDL_GetWindowSize(window, &width, &height);
            if (!video.reopen(device)) {
                std::fprintf(stderr, "cannot switch to %s\n", rune::rendererClass(device));
            }
            attachVideo();
            applyVideoMode(mode, width, height);
            if (video.device() == device) {
                rune::config::system().set("Engine.Engine", "GameRenderDevice", device == rune::RendererKind::Software ? std::string(rune::rendererClass(device)) : hardwareDevice);
                rune::config::system().save();
            }
        }

        SDL_Window* window = nullptr;
        Options options;
        rune::Video video;
        rune::Renderer* renderer = nullptr;
        std::optional<rune::RendererKind> pendingDevice;
        std::string hardwareDevice = rune::rendererClass(rune::RendererKind::Hardware);
        bool precacheOnFlip = false;
        std::unique_ptr<rune::World> entryWorld;
        std::unique_ptr<rune::World> world;
        std::unique_ptr<rune::net::Driver> netDriver;
        rune::UObject* canvasObject = nullptr;
        uint32_t* largeText = nullptr;
        rune::MoviePlayer movies;
        std::string pendingMap;
        int32_t pendingTravelType = rune::TRAVEL_Partial;
        std::string pendingShot;
        rune::Vector viewLocation;
        rune::Rotator viewRotation;
        float viewFov = 90;
        double viewportTime = 0;
        bool running = true;
        uint64_t last = 0;
        int frame = 0;
        int appliedVSync = -1;
        uint64_t frameDeadline = 0;
    };
}

int main(int argc, char** argv)
{
    Options options = parseOptions(argc, argv);
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_StartTextInput();
    rune::vm::setCriticalHandler([](const std::string& message) {
        std::fprintf(stderr, "Critical Error: %s\n", message.c_str());
        std::fflush(stderr);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Critical Error", message.c_str(), nullptr);
        std::_Exit(1);
    });
#ifdef __EMSCRIPTEN__
    try {
        Game* game = new Game(options);
        if (!game->start()) {
            std::fprintf(stderr, "failed to start game\n");
            return 1;
        }
        game->begin();
        emscripten_set_pointerlockchange_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, game, true, [](int, const EmscriptenPointerlockChangeEvent* event, void* data) -> EM_BOOL {
            if (!event->isActive) {
                static_cast<Game*>(data)->pressEscape();
            }
            return false;
        });
        emscripten_set_click_callback("#fullscreen", nullptr, true, [](int, const EmscriptenMouseEvent*, void*) -> EM_BOOL {
            rune::console::toggleFullscreenHook();
            return true;
        });
        emscripten_set_main_loop_arg(
            [](void* argument) {
                Game* running = static_cast<Game*>(argument);
                try {
                    running->step();
                } catch (const std::exception& error) {
                    std::fprintf(stderr, "error: %s\n", error.what());
                    emscripten_cancel_main_loop();
                    return;
                }
                if (!running->isRunning()) {
                    emscripten_cancel_main_loop();
                }
            },
            game, 0, true);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "error: %s\n", error.what());
        return 1;
    }
    return 0;
#else
    int result = 0;
    try {
        Game game(options);
        if (!game.start()) {
            std::fprintf(stderr, "failed to start game\n");
            result = 1;
        } else {
            result = game.run();
        }
    } catch (const std::exception& error) {
        std::fprintf(stderr, "error: %s\n", error.what());
        result = 1;
    }
    rune::audio::shutdown();
    SDL_Quit();
    return result;
#endif
}
