#include "core/objects.h"
#include "engine/canvas.h"
#include "engine/resources.h"

#include <algorithm>

namespace rune {
    namespace {
        float textScale = 1;
    }

    void setTextScale(float scale)
    {
        textScale = scale;
    }

    float textWidth(UFont* font, std::string_view text, float spacing)
    {
        if (!font) {
            return 0;
        }
        float width = 0;
        for (char c : text) {
            const FontCharacter* glyph = font->glyph(static_cast<uint8_t>(c), nullptr);
            if (glyph) {
                width += static_cast<float>(glyph->width) + spacing;
            }
        }
        return width * textScale;
    }

    float textHeight(UFont* font)
    {
        if (!font) {
            return 0;
        }
        const FontCharacter* glyph = font->glyph(static_cast<uint8_t>('A'), nullptr);
        if (!glyph) {
            glyph = font->glyph(static_cast<uint8_t>(' '), nullptr);
        }
        return glyph ? static_cast<float>(glyph->height) * textScale : 0;
    }

    bool charSize(UFont* font, uint8_t character, int32_t& width, int32_t& height)
    {
        width = 0;
        height = 0;
        const FontCharacter* glyph = font ? font->glyph(character, nullptr) : nullptr;
        if (!glyph) {
            return false;
        }
        width = static_cast<int32_t>(static_cast<float>(glyph->width) * textScale);
        height = static_cast<int32_t>(static_cast<float>(glyph->height) * textScale);
        return true;
    }

    int32_t drawString(Canvas2D* canvas, UFont* font, int32_t drawX, int32_t drawY, std::string_view text, const StringStyle& style)
    {
        int32_t lineX = 0;
        bool drawUnderline = false;
        int32_t underlineWidth = 0;
        for (size_t i = 0; i < text.size(); i++) {
            bool underlineNext = false;
            uint8_t character = static_cast<uint8_t>(text[i]);
            if (style.hotKey) {
                if (drawUnderline) {
                    character = '_';
                }
                if (text[i] == '&') {
                    if (i + 1 >= text.size()) {
                        break;
                    }
                    if (text[i + 1] != '&') {
                        underlineNext = true;
                        character = static_cast<uint8_t>(text[i + 1]);
                    }
                }
            }
            UTexture* texture = nullptr;
            const FontCharacter* glyph = font->glyph(character, &texture);
            if (!glyph) {
                continue;
            }
            int32_t glyphWidth = static_cast<int32_t>(static_cast<float>(glyph->width) * textScale);
            int32_t glyphHeight = static_cast<int32_t>(static_cast<float>(glyph->height) * textScale);
            int32_t width = drawUnderline ? std::min(glyphWidth, underlineWidth) : glyphWidth;
            int32_t height = glyphHeight;
            float u = static_cast<float>(glyph->u);
            float v = static_cast<float>(glyph->v);
            int32_t x = lineX + drawX;
            int32_t y = drawY;
            bool visible = true;
            if (style.clip) {
                visible = x + width > 0 && static_cast<float>(x) <= style.clipX && y + height > 0 && static_cast<float>(y) <= style.clipY;
                if (visible) {
                    if (x < 0) {
                        u -= static_cast<float>(x) / textScale;
                        width += x;
                        x = 0;
                    }
                    if (y < 0) {
                        v -= static_cast<float>(y) / textScale;
                        height += y;
                        y = 0;
                    }
                    if (static_cast<float>(x + width) > style.clipX) {
                        width = static_cast<int32_t>(style.clipX - static_cast<float>(x));
                    }
                    if (static_cast<float>(y + height) > style.clipY) {
                        height = static_cast<int32_t>(style.clipY - static_cast<float>(y));
                    }
                }
            }
            if (visible && canvas && texture && width > 0 && height > 0) {
                BlendStyle glyphStyle = style.blend;
                if (glyphStyle == BlendStyle::Normal) {
                    objects::ensureLoaded(texture);
                    UPalette* palette = texture->palette();
                    bool alphaFont = palette && palette->colors.size() > 128 && (palette->colors[128] >> 24) != 0xff;
                    glyphStyle = alphaFont ? BlendStyle::Highlighted : BlendStyle::Masked;
                }
                canvas->drawTile(texture, static_cast<float>(static_cast<int32_t>(style.orgX + static_cast<float>(x))), static_cast<float>(static_cast<int32_t>(style.orgY + static_cast<float>(y))), static_cast<float>(width), static_cast<float>(height), u, v, static_cast<float>(width) / textScale, static_cast<float>(height) / textScale, style.color, glyphStyle, false);
            }
            if (drawUnderline) {
                width = underlineWidth;
            }
            if (!underlineNext) {
                lineX += static_cast<int32_t>(style.spaceX + static_cast<float>(width));
            } else {
                underlineWidth = glyphWidth;
            }
            drawUnderline = underlineNext;
        }
        return lineX;
    }

    void drawText(Canvas2D& canvas, UFont* font, std::string_view text, float x, float y, LinearColor color, BlendStyle style, float spacing)
    {
        if (!font) {
            return;
        }
        float cursor = x;
        for (char c : text) {
            UTexture* texture = nullptr;
            const FontCharacter* glyph = font->glyph(static_cast<uint8_t>(c), &texture);
            if (!glyph) {
                continue;
            }
            if (texture && glyph->width > 0 && glyph->height > 0) {
                BlendStyle glyphStyle = style;
                if (style == BlendStyle::Normal) {
                    objects::ensureLoaded(texture);
                    UPalette* palette = texture->palette();
                    bool alphaFont = palette && palette->colors.size() > 128 && (palette->colors[128] >> 24) != 0xff;
                    glyphStyle = alphaFont ? BlendStyle::Highlighted : BlendStyle::Masked;
                }
                canvas.drawTile(texture, cursor, y, static_cast<float>(glyph->width) * textScale, static_cast<float>(glyph->height) * textScale, static_cast<float>(glyph->u), static_cast<float>(glyph->v), static_cast<float>(glyph->width), static_cast<float>(glyph->height), color, glyphStyle, false);
            }
            cursor += (static_cast<float>(glyph->width) + spacing) * textScale;
        }
    }
}
