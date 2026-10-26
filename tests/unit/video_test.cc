#include "video_fixture.h"

TEST_CASE("window modes distinguish decorated windows desktop fullscreen and exclusive fullscreen")
{
    VideoFixture fixture;
    CHECK(fixture.video.windowMode() == rune::WindowMode::Windowed);
    REQUIRE(fixture.video.setWindowMode(rune::WindowMode::Borderless, 640, 480));
    CHECK(fixture.video.windowMode() == rune::WindowMode::Borderless);
    auto flags = SDL_GetWindowFlags(fixture.video.window());
    CHECK((flags & SDL_WINDOW_FULLSCREEN_DESKTOP) == SDL_WINDOW_FULLSCREEN_DESKTOP);
    SDL_DisplayMode desktop {};
    REQUIRE(SDL_GetDesktopDisplayMode(0, &desktop) == 0);
    int width = 0;
    int height = 0;
    SDL_GetWindowSize(fixture.video.window(), &width, &height);
    CHECK(width == desktop.w);
    CHECK(height == desktop.h);
    REQUIRE(fixture.video.setWindowMode(rune::WindowMode::Fullscreen, 640, 480));
    CHECK(fixture.video.windowMode() == rune::WindowMode::Fullscreen);
    flags = SDL_GetWindowFlags(fixture.video.window());
    CHECK((flags & SDL_WINDOW_FULLSCREEN_DESKTOP) == SDL_WINDOW_FULLSCREEN);
    REQUIRE(fixture.video.setWindowMode(rune::WindowMode::Windowed, 800, 600));
    CHECK(fixture.video.windowMode() == rune::WindowMode::Windowed);
    SDL_GetWindowSize(fixture.video.window(), &width, &height);
    CHECK(width == 800);
    CHECK(height == 600);
}

TEST_CASE("reopening a renderer preserves each window mode")
{
    VideoFixture fixture;
    for (auto mode : { rune::WindowMode::Windowed, rune::WindowMode::Borderless, rune::WindowMode::Fullscreen }) {
        CAPTURE(mode);
        REQUIRE(fixture.video.setWindowMode(mode, 800, 600));
        REQUIRE(fixture.video.reopen(rune::RendererKind::Software));
        CHECK(fixture.video.windowMode() == mode);
    }
}
