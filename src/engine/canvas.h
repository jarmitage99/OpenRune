#pragma once

#include "core/math.h"

#include <cstdint>
#include <string_view>

namespace rune {
    class UTexture;
    class UFont;

    enum class BlendStyle : uint8_t {
        None = 0,
        Normal = 1,
        Masked = 2,
        Translucent = 3,
        Modulated = 4,
        Highlighted = 5,
        AlphaBlend = 6,
        Multiply = 7,
    };

    struct LinearColor {
        float r = 1;
        float g = 1;
        float b = 1;
        float a = 1;
    };

    class Canvas2D {
    public:
        virtual ~Canvas2D() = default;
        virtual float width() const = 0;
        virtual float height() const = 0;
        virtual void setClip(float x, float y, float w, float h) = 0;
        virtual void clearClip() = 0;
        virtual void drawTile(UTexture* texture, float x, float y, float w, float h, float u, float v, float ul, float vl, LinearColor color, BlendStyle style, bool smooth) = 0;
        virtual void drawRect(float x, float y, float w, float h, LinearColor color, BlendStyle style) = 0;
        virtual void drawLine(float x1, float y1, float x2, float y2, LinearColor color) = 0;
        virtual void drawImage(const uint8_t* rgba, int imageWidth, int imageHeight, float x, float y, float w, float h) = 0;
    };

    struct StringStyle {
        LinearColor color;
        BlendStyle blend = BlendStyle::Normal;
        float spaceX = 0;
        float orgX = 0;
        float orgY = 0;
        bool clip = false;
        float clipX = 0;
        float clipY = 0;
        bool hotKey = false;
    };

    void setTextScale(float scale);
    bool charSize(UFont* font, uint8_t character, int32_t& width, int32_t& height);
    int32_t drawString(Canvas2D* canvas, UFont* font, int32_t drawX, int32_t drawY, std::string_view text, const StringStyle& style);
    float textWidth(UFont* font, std::string_view text, float spacing);
    float textHeight(UFont* font);
    void drawText(Canvas2D& canvas, UFont* font, std::string_view text, float x, float y, LinearColor color, BlendStyle style, float spacing);
}
