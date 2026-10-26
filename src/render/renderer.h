#pragma once

#include "engine/canvas.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace rune {
    class UObject;
    class ULevel;

    enum class RendererKind : uint8_t {
        Hardware,
        Software,
    };

    RendererKind rendererKindOf(std::string_view className);
    const char* rendererClass(RendererKind device);

    struct RenderOptions {
        float anisotropy = 1;
        bool trilinear = false;
        int multisample = 0;
        bool fxaa = false;
        bool smoothLightmaps = false;
        float detailDistance = 380;
        float fogDistance = 1;
        float lodPercentage = 1;
        int playerShadows = 0;
        int actorShadows = 0;
    };

    class Renderer {
    public:
        virtual ~Renderer() = default;

        virtual void beginFrame(int width, int height) = 0;
        virtual void drawScene(ULevel* level, const std::vector<UObject*>& actors, const Vector& location, const Rotator& rotation, float fov, UObject* viewActor) = 0;
        virtual void drawPortal(ULevel* level, const std::vector<UObject*>& actors, const Vector& location, const Rotator& rotation, float fov, UObject* viewActor, int x, int y, int width, int height, bool clearZ) = 0;
        virtual void drawActorOverlay(UObject* actor, const Vector& viewLocation, const Rotator& viewRotation, float fov, bool wireframe, bool clearZ, int x, int y, int width, int height) = 0;
        virtual Canvas2D& canvas() = 0;
        virtual void endFrame() = 0;
        virtual void applyBrightness(float brightness) = 0;
        virtual void setOptions(const RenderOptions& newOptions) = 0;
        virtual void readPixels(std::vector<uint8_t>& rgba, int& width, int& height) = 0;
        virtual void flushLevel() = 0;
        virtual void setTime(double seconds) = 0;
        virtual void setViewportTime(double seconds) = 0;
    };
}
