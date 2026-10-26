#pragma once

#include "core/math.h"
#include "core/object.h"
#include "engine/canvas.h"

#include <functional>
#include <string>

namespace rune {
    struct ActorFrame {
        bool clipped = false;
        int32_t x = 0;
        int32_t y = 0;
        int32_t width = 0;
        int32_t height = 0;
        float fov = 90;
    };

    struct CanvasHooks {
        Canvas2D* target = nullptr;
        std::function<void(UObject* actor, bool wireframe, bool clearZ, const ActorFrame& frame)> drawActor;
        std::function<void(int x, int y, int width, int height, UObject* camera, const Vector& location, const Rotator& rotation, float fov, bool clearZ)> drawPortal;
        std::function<void(const std::string&)> clipboardCopy;
        std::function<std::string()> clipboardPaste;
        bool hasView = false;
        Vector viewLocation;
        Rotator viewRotation;
        float viewFov = 90;
    };

    CanvasHooks& canvasHooks();
    LinearColor canvasColor(UObject* canvas);
    void postRenderHud(UObject* player, UObject* canvas, float textScale);
}
