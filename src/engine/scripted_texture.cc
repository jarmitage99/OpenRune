#include "engine/scripted_texture.h"

#include "core/objects.h"
#include "core/vm.h"
#include "engine/props_engine.h"
#include "engine/resources.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace rune {
    namespace {
        const Name nameScriptedTexture("ScriptedTexture");
        const Name nameRenderTexture("RenderTexture");

        float appSeconds()
        {
            static const auto start = std::chrono::steady_clock::now();
            return std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();
        }

        int32_t appRound(float value)
        {
            return static_cast<int32_t>(std::lrint(value));
        }

        TextureMip* topMip(UTexture* texture)
        {
            objects::ensureLoaded(texture);
            if (!texture || texture->mips.empty()) {
                return nullptr;
            }
            return &texture->mips[0];
        }

        TextureMip* drawMip(UTexture* texture)
        {
            TextureMip* mip = topMip(texture);
            size_t size = static_cast<size_t>(std::max(0, bitmap::USize(texture) * bitmap::VSize(texture)));
            if (mip && mip->data.size() < size) {
                mip->data.resize(size, 0);
            }
            return mip;
        }

        ScriptedTextureData& dataOf(UTexture* texture)
        {
            if (!texture->scripted) {
                texture->scripted = std::make_unique<ScriptedTextureData>();
            }
            return *texture->scripted;
        }

        UTexture* currentFrame(UTexture* texture)
        {
            UTexture* current = cast<UTexture>(texture::AnimCurrent(texture));
            return current ? current : texture;
        }

        void postLoad(UTexture* texture)
        {
            UTexture* source = cast<UTexture>(scriptedtexture::SourceTexture(texture));
            TextureMip* sourceMip = source ? topMip(source) : nullptr;
            if (!sourceMip) {
                return;
            }
            ScriptedTextureData& data = dataOf(texture);
            int32_t usize = bitmap::USize(texture);
            int32_t vsize = bitmap::VSize(texture);
            data.localSource.assign(static_cast<size_t>(std::max(0, usize * vsize)), 0);
            int32_t uScaler = (bitmap::UBits(texture) - bitmap::UBits(source)) & 31;
            int32_t vScaler = (bitmap::VBits(texture) - bitmap::VBits(source)) & 31;
            if (source != data.oldSource) {
                bitmap::Palette(texture) = bitmap::Palette(source);
            }
            data.oldSource = source;
            int32_t sourceUBits = bitmap::UBits(source);
            for (int32_t v = 0; v < vsize; v++) {
                for (int32_t u = 0; u < usize; u++) {
                    size_t index = static_cast<size_t>((u >> uScaler) + ((v >> vScaler) << sourceUBits));
                    data.localSource[static_cast<size_t>(u + v * usize)] = index < sourceMip->data.size() ? sourceMip->data[index] : 0;
                }
            }
        }

        void drawTile(UTexture* self, float x, float y, float xl, float yl, float u, float v, float ul, float vl, UTexture* tex, const TextureMip& source, bool masked, bool useColor, uint8_t colorIndex)
        {
            TextureMip* dest = drawMip(self);
            if (!dest) {
                return;
            }
            int32_t usize = bitmap::USize(self);
            int32_t vsize = bitmap::VSize(self);
            if (x + xl < 0 || y + yl < 0 || x > static_cast<float>(usize) || y > static_cast<float>(vsize)) {
                return;
            }
            const std::vector<uint8_t>* palMap = nullptr;
            if (!useColor) {
                ScriptedTextureData& data = dataOf(self);
                auto found = data.paletteMap.find(tex);
                if (found == data.paletteMap.end()) {
                    std::vector<uint8_t> temp(256, 0);
                    UPalette* palette = self->palette();
                    UPalette* texPalette = tex->palette();
                    for (int i = 0; i < 256; i++) {
                        uint32_t color = texPalette && static_cast<size_t>(i) < texPalette->colors.size() ? texPalette->colors[static_cast<size_t>(i)] : 0;
                        temp[static_cast<size_t>(i)] = palette ? palette->bestMatch(color, 0) : 0;
                    }
                    found = data.paletteMap.emplace(tex, std::move(temp)).first;
                }
                palMap = &found->second;
            }
            int32_t dx1 = std::min(std::max(appRound(x), 0), usize - 1);
            u += (static_cast<float>(dx1) - x) * (ul / xl);
            int32_t dx2 = std::min(appRound(xl + x), usize - 1);
            int32_t dy1 = std::min(std::max(appRound(y), 0), vsize - 1);
            v += (static_cast<float>(dy1) - y) * (vl / yl);
            int32_t dy2 = std::min(appRound(yl + y), vsize - 1);
            uint32_t srcUMask = static_cast<uint32_t>(bitmap::USize(tex) - 1);
            uint32_t srcVMask = static_cast<uint32_t>(bitmap::VSize(tex) - 1);
            int32_t texUSize = bitmap::USize(tex);
            int32_t destBase = dy1 * usize;
            int32_t xFixStep = appRound(ul / xl * static_cast<float>(1 << 19));
            int32_t fixU = appRound(u * static_cast<float>(1 << 19)) - xFixStep;
            int32_t yFixStep = appRound(vl / yl * static_cast<float>(1 << 19));
            int32_t yFixStart = appRound(v * static_cast<float>(1 << 19)) - yFixStep;
            auto sourcePixel = [&](int32_t index) -> uint8_t {
                return index >= 0 && static_cast<size_t>(index) < source.data.size() ? source.data[static_cast<size_t>(index)] : 0;
            };
            for (int32_t dy = dy1; dy < dy2; dy++) {
                int32_t xFixStart = fixU;
                yFixStart += yFixStep;
                int32_t srcYIndex = static_cast<int32_t>(srcVMask & static_cast<uint32_t>(yFixStart >> 19)) * texUSize;
                for (int32_t dx = dx1; dx < dx2; dx++) {
                    xFixStart += xFixStep;
                    uint8_t pixel = sourcePixel(srcYIndex + static_cast<int32_t>(srcUMask & static_cast<uint32_t>(xFixStart >> 19)));
                    uint8_t& out = dest->data[static_cast<size_t>(destBase + dx)];
                    if (useColor && masked) {
                        if (pixel != 0) {
                            out = colorIndex;
                        }
                    } else if (!masked) {
                        out = (*palMap)[pixel];
                    } else if (pixel != 0) {
                        out = (*palMap)[pixel];
                    }
                }
                destBase += usize;
            }
        }

        void drawString(UTexture* self, UFont* font, int32_t drawX, int32_t drawY, const std::string& text, bool useColor, uint8_t paletteEntry)
        {
            int32_t lineX = 0;
            for (char c : text) {
                UTexture* page = nullptr;
                const FontCharacter* glyph = font->glyph(static_cast<uint8_t>(c), &page);
                if (!glyph || !page) {
                    continue;
                }
                TextureMip* mip = topMip(page);
                if (mip) {
                    drawTile(self, static_cast<float>(drawX + lineX), static_cast<float>(drawY), static_cast<float>(glyph->width), static_cast<float>(glyph->height), static_cast<float>(glyph->u), static_cast<float>(glyph->v), static_cast<float>(glyph->width), static_cast<float>(glyph->height), page, *mip, true, useColor, paletteEntry);
                }
                lineX += glyph->width;
            }
        }

        void nativeDrawText(UObject* self, Frame& stack, void*)
        {
            float x = vm::get<float>(stack);
            float y = vm::get<float>(stack);
            std::string text = vm::get<std::string>(stack);
            UFont* font = cast<UFont>(vm::get<UObject*>(stack));
            vm::finish(stack);
            if (!font) {
                vm::warn(stack, "DrawText: No font");
                return;
            }
            drawString(static_cast<UTexture*>(self), font, appRound(x), appRound(y), text, false, 0);
        }

        void nativeDrawColoredText(UObject* self, Frame& stack, void*)
        {
            float x = vm::get<float>(stack);
            float y = vm::get<float>(stack);
            std::string text = vm::get<std::string>(stack);
            UFont* font = cast<UFont>(vm::get<UObject*>(stack));
            Color color = vm::get<Color>(stack);
            vm::finish(stack);
            if (!font) {
                vm::warn(stack, "DrawText: No font");
                return;
            }
            UTexture* texture = static_cast<UTexture*>(self);
            UPalette* palette = texture->palette();
            uint32_t packed = static_cast<uint32_t>(color.r) | (static_cast<uint32_t>(color.g) << 8) | (static_cast<uint32_t>(color.b) << 16) | (static_cast<uint32_t>(color.a) << 24);
            uint8_t entry = palette ? palette->bestMatch(packed, 0) : 0;
            drawString(texture, font, appRound(x), appRound(y), text, true, entry);
        }

        void nativeDrawTile(UObject* self, Frame& stack, void*)
        {
            float x = vm::get<float>(stack);
            float y = vm::get<float>(stack);
            float xl = vm::get<float>(stack);
            float yl = vm::get<float>(stack);
            float u = vm::get<float>(stack);
            float v = vm::get<float>(stack);
            float ul = vm::get<float>(stack);
            float vl = vm::get<float>(stack);
            UTexture* tex = cast<UTexture>(vm::get<UObject*>(stack));
            bool masked = vm::get<bool>(stack);
            vm::finish(stack);
            if (!tex) {
                vm::warn(stack, "DrawTile: Missing Texture");
                return;
            }
            UTexture* frame = currentFrame(tex);
            TextureMip* mip = topMip(frame);
            if (mip) {
                drawTile(static_cast<UTexture*>(self), x, y, xl, yl, u, v, ul, vl, frame, *mip, masked, false, 0);
            }
        }

        void nativeReplaceTexture(UObject* self, Frame& stack, void*)
        {
            UTexture* tex = cast<UTexture>(vm::get<UObject*>(stack));
            vm::finish(stack);
            if (!tex) {
                vm::warn(stack, "ReplaceTexture: Missing Texture");
                return;
            }
            if (bitmap::USize(self) != bitmap::USize(tex) || bitmap::VSize(self) != bitmap::VSize(tex)) {
                vm::warn(stack, "ReplaceTexture: Dimensions are different");
                return;
            }
            TextureMip* source = topMip(currentFrame(tex));
            TextureMip* dest = drawMip(static_cast<UTexture*>(self));
            if (!source || !dest) {
                return;
            }
            size_t count = std::min({ static_cast<size_t>(bitmap::USize(self) * bitmap::VSize(self)), source->data.size(), dest->data.size() });
            std::memcpy(dest->data.data(), source->data.data(), count);
        }

        void nativeTextSize(UObject*, Frame& stack, void*)
        {
            std::string text = vm::get<std::string>(stack);
            float* xl = vm::getRef<float>(stack);
            float* yl = vm::getRef<float>(stack);
            UFont* font = cast<UFont>(vm::get<UObject*>(stack));
            vm::finish(stack);
            if (!font) {
                vm::warn(stack, "TextSize: No font");
                return;
            }
            int32_t width = 0;
            int32_t height = 0;
            for (char c : text) {
                const FontCharacter* glyph = font->glyph(static_cast<uint8_t>(c), nullptr);
                if (glyph) {
                    width += glyph->width;
                    height = std::max(height, glyph->height);
                }
            }
            *xl = static_cast<float>(width);
            *yl = static_cast<float>(height);
        }
    }

    namespace scripted {
        bool isScripted(UTexture* texture)
        {
            return texture && texture->cls && texture->isA(nameScriptedTexture);
        }

        void tick(UTexture* texture)
        {
            ScriptedTextureData& data = dataOf(texture);
            if (!data.loaded) {
                data.loaded = true;
                postLoad(texture);
            }
            scriptedtexture::LocalTime(texture) = appSeconds();
            TextureMip* dest = drawMip(texture);
            if (dest && !data.localSource.empty()) {
                std::memcpy(dest->data.data(), data.localSource.data(), std::min(dest->data.size(), data.localSource.size()));
            }
            UObject* notify = scriptedtexture::NotifyActor(texture);
            if (notify && !notify->destroyed) {
                vm::Call(notify, nameRenderTexture).obj(texture).run();
            }
        }
    }

    void registerScriptedTextureNatives()
    {
        vm::registerNative(472, nativeDrawText);
        vm::registerNative(473, nativeDrawTile);
        vm::registerNative(474, nativeDrawColoredText);
        vm::registerNative(475, nativeReplaceTexture);
        vm::registerNative(476, nativeTextSize);
    }
}
