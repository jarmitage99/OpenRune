#include "core/config.h"
#include "core/objects.h"
#include "engine/console.h"
#include "engine/resources.h"
#include "engine/world.h"
#include "render/renderer.h"
#include "render/scene_renderer.h"
#include "render/soft_device.h"

#include <catch_amalgamated.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace {
    std::vector<uint8_t> pixelAt(rune::SceneRenderer& renderer, int x, int y)
    {
        std::vector<uint8_t> rgba;
        int width = 0;
        int height = 0;
        renderer.readPixels(rgba, width, height);
        size_t at = (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4;
        return { rgba[at], rgba[at + 1], rgba[at + 2], rgba[at + 3] };
    }
}

TEST_CASE("render device names select the software renderer only for SoftDrv")
{
    CHECK(rune::rendererKindOf("SoftDrv.SoftwareRenderDevice") == rune::RendererKind::Software);
    CHECK(rune::rendererKindOf("softdrv.softwarerenderdevice") == rune::RendererKind::Software);
    CHECK(rune::rendererKindOf("D3DDrv.D3DRenderDevice") == rune::RendererKind::Hardware);
    CHECK(rune::rendererKindOf("OpenGLDrv.OpenGLRenderDevice") == rune::RendererKind::Hardware);
    CHECK(rune::rendererKindOf("") == rune::RendererKind::Hardware);
    CHECK(std::string(rune::rendererClass(rune::RendererKind::Software)) == "SoftDrv.SoftwareRenderDevice");
    CHECK(std::string(rune::rendererClass(rune::RendererKind::Hardware)) == "D3DDrv.D3DRenderDevice");
}

TEST_CASE("the video menu reads the render device as a class reference", "[data]")
{
    rune::engine::initialize(RUNE_GAME_DATA);
    std::string original = rune::config::system().get("Engine.Engine", "GameRenderDevice").value_or("");
    rune::config::system().set("Engine.Engine", "GameRenderDevice", "SoftDrv.SoftwareRenderDevice");
    CHECK(rune::console::execute(nullptr, "get ini:Engine.Engine.GameRenderDevice Class") == "Class'SoftDrv.SoftwareRenderDevice'");
    rune::config::system().set("Engine.Engine", "GameRenderDevice", "D3DDrv.D3DRenderDevice");
    CHECK(rune::console::execute(nullptr, "get ini:Engine.Engine.GameRenderDevice Class") == "Class'D3DDrv.D3DRenderDevice'");
    rune::config::system().set("Engine.Engine", "GameRenderDevice", original);
}

TEST_CASE("the change video relaunch switches the render device instead of relaunching", "[data]")
{
    rune::engine::initialize(RUNE_GAME_DATA);
    int calls = 0;
    rune::console::changeVideoHook = [&calls]() {
        calls++;
    };
    rune::console::execute(nullptr, "RELAUNCH rune://intro.run?video=.");
    CHECK(calls == 0);
    rune::console::execute(nullptr, "RELAUNCH -changevideo");
    CHECK(calls == 1);
    rune::console::changeVideoHook = nullptr;
}

TEST_CASE("the software canvas fills, adds with saturation and clips rectangles")
{
    rune::SceneRenderer renderer(std::make_unique<rune::SoftRenderDevice>());
    renderer.beginFrame(8, 4);
    CHECK(pixelAt(renderer, 0, 0) == std::vector<uint8_t> { 0, 0, 0, 255 });
    renderer.canvas().drawRect(0, 0, 8, 4, { 1.0f, 0.5f, 0.0f, 1.0f }, rune::BlendStyle::Normal);
    CHECK(pixelAt(renderer, 7, 3) == std::vector<uint8_t> { 255, 128, 0, 255 });
    renderer.canvas().drawRect(0, 0, 4, 4, { 0.0f, 0.25f, 1.0f, 1.0f }, rune::BlendStyle::Translucent);
    CHECK(pixelAt(renderer, 1, 1) == std::vector<uint8_t> { 255, 192, 255, 255 });
    renderer.canvas().drawRect(0, 0, 4, 4, { 0.0f, 0.5f, 0.0f, 1.0f }, rune::BlendStyle::Translucent);
    CHECK(pixelAt(renderer, 1, 1) == std::vector<uint8_t> { 255, 255, 255, 255 });
    renderer.canvas().drawRect(4, 0, 4, 4, { 0.0f, 0.0f, 0.0f, 0.5f }, rune::BlendStyle::AlphaBlend);
    CHECK(pixelAt(renderer, 5, 1) == std::vector<uint8_t> { 128, 64, 0, 255 });
    renderer.canvas().setClip(0, 0, 2, 2);
    renderer.canvas().drawRect(0, 0, 8, 4, { 0.0f, 1.0f, 0.0f, 1.0f }, rune::BlendStyle::Normal);
    CHECK(pixelAt(renderer, 1, 1) == std::vector<uint8_t> { 0, 255, 0, 255 });
    CHECK(pixelAt(renderer, 2, 1) == std::vector<uint8_t> { 255, 255, 255, 255 });
    CHECK(pixelAt(renderer, 1, 2) == std::vector<uint8_t> { 255, 255, 255, 255 });
}

TEST_CASE("the software canvas scales a raw image into its rectangle with nearest sampling")
{
    const std::vector<uint8_t> image {
        10, 20, 30, 255, 40, 50, 60, 255,
        70, 80, 90, 255, 100, 110, 120, 255
    };
    rune::SceneRenderer renderer(std::make_unique<rune::SoftRenderDevice>());
    renderer.beginFrame(8, 6);
    renderer.canvas().drawImage(image.data(), 2, 2, 2, 1, 4, 4);
    CHECK(pixelAt(renderer, 1, 1) == std::vector<uint8_t> { 0, 0, 0, 255 });
    CHECK(pixelAt(renderer, 2, 1) == std::vector<uint8_t> { 10, 20, 30, 255 });
    CHECK(pixelAt(renderer, 3, 2) == std::vector<uint8_t> { 10, 20, 30, 255 });
    CHECK(pixelAt(renderer, 4, 1) == std::vector<uint8_t> { 40, 50, 60, 255 });
    CHECK(pixelAt(renderer, 2, 3) == std::vector<uint8_t> { 70, 80, 90, 255 });
    CHECK(pixelAt(renderer, 5, 4) == std::vector<uint8_t> { 100, 110, 120, 255 });
    CHECK(pixelAt(renderer, 6, 4) == std::vector<uint8_t> { 0, 0, 0, 255 });
    CHECK(pixelAt(renderer, 5, 5) == std::vector<uint8_t> { 0, 0, 0, 255 });
}

TEST_CASE("the software canvas copies texels one to one for an unscaled tile", "[data]")
{
    rune::engine::initialize(RUNE_GAME_DATA);
    rune::UTexture* texture = rune::cast<rune::UTexture>(rune::objects::loadObject("Brckston.BRCK6_c", nullptr));
    REQUIRE(texture != nullptr);
    rune::objects::ensureLoaded(texture);
    std::vector<uint8_t> texels = texture->rgba(0);
    uint32_t width = texture->width();
    REQUIRE(width >= 16);
    rune::SceneRenderer renderer(std::make_unique<rune::SoftRenderDevice>());
    renderer.beginFrame(8, 8);
    renderer.canvas().drawTile(texture, 0, 0, 8, 8, 3, 5, 8, 8, { 1, 1, 1, 1 }, rune::BlendStyle::Normal, false);
    for (int y = 0; y < 8; y += 3) {
        for (int x = 0; x < 8; x += 3) {
            size_t at = (static_cast<size_t>(5 + y) * width + static_cast<size_t>(3 + x)) * 4;
            CHECK(pixelAt(renderer, x, y) == std::vector<uint8_t> { texels[at], texels[at + 1], texels[at + 2], 255 });
        }
    }
}

TEST_CASE("software brightness scales colours linearly instead of applying gamma")
{
    rune::SceneRenderer renderer(std::make_unique<rune::SoftRenderDevice>());
    renderer.beginFrame(2, 1);
    renderer.canvas().drawRect(0, 0, 2, 1, { 128.0f / 255.0f, 0.0f, 1.0f, 1.0f }, rune::BlendStyle::Normal);
    renderer.applyBrightness(0.5f);
    CHECK(pixelAt(renderer, 0, 0) == std::vector<uint8_t> { 128, 0, 255, 255 });
    renderer.applyBrightness(0.9f);
    renderer.beginFrame(2, 1);
    renderer.canvas().drawRect(0, 0, 2, 1, { 128.0f / 255.0f, 0.0f, 1.0f, 1.0f }, rune::BlendStyle::Normal);
    renderer.applyBrightness(0.9f);
    CHECK(pixelAt(renderer, 0, 0) == std::vector<uint8_t> { 179, 0, 255, 255 });
}

namespace {
    std::vector<rune::GLVertex> square(float depth, float r, float g, float b)
    {
        std::vector<rune::GLVertex> vertices;
        const float corners[6][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, -1 }, { 1, 1 }, { -1, 1 } };
        for (const auto& corner : corners) {
            rune::GLVertex vertex;
            vertex.x = corner[0];
            vertex.y = corner[1];
            vertex.z = depth;
            vertex.r = r;
            vertex.g = g;
            vertex.b = b;
            vertices.push_back(vertex);
        }
        return vertices;
    }

    std::vector<uint8_t> devicePixel(rune::SoftRenderDevice& device, int x, int y)
    {
        size_t at = (static_cast<size_t>(y) * static_cast<size_t>(device.width()) + static_cast<size_t>(x)) * 4;
        const uint8_t* pixels = device.pixels();
        return { pixels[at], pixels[at + 1], pixels[at + 2], pixels[at + 3] };
    }

    void prepare(rune::SoftRenderDevice& device)
    {
        device.beginCanvas(4, 4);
        device.setViewport(0, 0, 4, 4);
        device.clear(true, true, 1.0f);
        device.setViewProj({ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 });
        device.setFlags(0);
        device.setBrightness(0.5f);
    }
}

TEST_CASE("translucent world surfaces are stippled while translucent actors add with saturation")
{
    rune::SoftRenderDevice device;
    prepare(device);
    device.draw(square(0, 0.5f, 0.5f, 0.5f), rune::Primitive::Triangles);
    device.setBlend(rune::BlendMode::Translucent);
    device.setFlags(rune::SHADER_Surface);
    device.draw(square(0, 1.0f, 0.0f, 0.0f), rune::Primitive::Triangles);
    CHECK(devicePixel(device, 0, 0) == std::vector<uint8_t> { 255, 0, 0, 255 });
    CHECK(devicePixel(device, 1, 0) == std::vector<uint8_t> { 128, 128, 128, 255 });
    CHECK(devicePixel(device, 1, 1) == std::vector<uint8_t> { 255, 0, 0, 255 });
    CHECK(devicePixel(device, 2, 1) == std::vector<uint8_t> { 128, 128, 128, 255 });
    device.setBlend(rune::BlendMode::Opaque);
    device.setFlags(0);
    device.draw(square(0, 0.5f, 0.5f, 0.5f), rune::Primitive::Triangles);
    device.setBlend(rune::BlendMode::Translucent);
    device.draw(square(0, 0.75f, 0.25f, 0.0f), rune::Primitive::Triangles);
    CHECK(devicePixel(device, 0, 0) == std::vector<uint8_t> { 255, 192, 128, 255 });
    CHECK(devicePixel(device, 1, 0) == std::vector<uint8_t> { 255, 192, 128, 255 });
}

TEST_CASE("the software device keeps the nearer surface and culls back faces")
{
    rune::SoftRenderDevice device;
    prepare(device);
    device.setDepthTest(true);
    device.setDepthFunc(rune::DepthFunc::LessEqual);
    device.setBlend(rune::BlendMode::Opaque);
    device.draw(square(-0.5f, 0.0f, 1.0f, 0.0f), rune::Primitive::Triangles);
    device.draw(square(0.5f, 1.0f, 0.0f, 0.0f), rune::Primitive::Triangles);
    CHECK(devicePixel(device, 2, 2) == std::vector<uint8_t> { 0, 255, 0, 255 });
    device.setCulling(true);
    device.setFrontFace(true);
    device.draw(square(-0.9f, 0.0f, 0.0f, 1.0f), rune::Primitive::Triangles);
    CHECK(devicePixel(device, 2, 2) == std::vector<uint8_t> { 0, 255, 0, 255 });
    device.setFrontFace(false);
    device.draw(square(-0.9f, 0.0f, 0.0f, 1.0f), rune::Primitive::Triangles);
    CHECK(devicePixel(device, 2, 2) == std::vector<uint8_t> { 0, 0, 255, 255 });
}

TEST_CASE("the software device follows SoftDrv in drawing no decals, coronas or distance fog")
{
    rune::SoftRenderDevice device;
    CHECK_FALSE(device.drawsDecals());
    CHECK_FALSE(device.drawsCoronas());
    CHECK(device.lightsTranslucencyInOnePass());
    prepare(device);
    std::vector<rune::GLVertex> fogged = square(0, 0.5f, 0.5f, 0.5f);
    for (rune::GLVertex& vertex : fogged) {
        vertex.zoneFogR = 1.0f;
        vertex.zoneFogInverse = 1.0f;
    }
    device.setEye({ 0, 0, -100 }, { 0, 0, 1 });
    device.draw(fogged, rune::Primitive::Triangles);
    CHECK(devicePixel(device, 1, 1) == std::vector<uint8_t> { 128, 128, 128, 255 });
}

TEST_CASE("the software device clears to a requested colour within the scissor")
{
    rune::SoftRenderDevice device;
    prepare(device);
    device.enableScissor(0, 0, 2, 4);
    device.clearColor(1.0f, 0.5f, 0.0f);
    device.disableScissor();
    CHECK(devicePixel(device, 0, 0) == std::vector<uint8_t> { 255, 128, 0, 255 });
    CHECK(devicePixel(device, 1, 3) == std::vector<uint8_t> { 255, 128, 0, 255 });
    CHECK(devicePixel(device, 2, 0) == std::vector<uint8_t> { 0, 0, 0, 255 });
}

TEST_CASE("software occlusion queries resolve once their draws are rasterised")
{
    rune::SoftRenderDevice device;
    prepare(device);
    device.setDepthTest(true);
    device.setDepthFunc(rune::DepthFunc::LessEqual);
    device.setBlend(rune::BlendMode::Opaque);
    device.draw(square(-0.5f, 0.0f, 1.0f, 0.0f), rune::Primitive::Triangles);
    uint32_t hidden = device.createQuery();
    uint32_t shown = device.createQuery();
    device.setColorWrite(false);
    device.beginQuery(hidden);
    device.draw(square(0.5f, 1.0f, 0.0f, 0.0f), rune::Primitive::Triangles);
    device.endQuery();
    device.beginQuery(shown);
    device.draw(square(-0.9f, 1.0f, 0.0f, 0.0f), rune::Primitive::Triangles);
    device.endQuery();
    device.setColorWrite(true);
    bool visible = true;
    CHECK_FALSE(device.queryResult(shown, visible));
    device.pixels();
    REQUIRE(device.queryResult(hidden, visible));
    CHECK_FALSE(visible);
    REQUIRE(device.queryResult(shown, visible));
    CHECK(visible);
    CHECK(devicePixel(device, 2, 2) == std::vector<uint8_t> { 0, 255, 0, 255 });
}
