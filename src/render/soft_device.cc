#include "render/soft_device.h"

#include "core/config.h"
#include "core/objects.h"
#include "engine/props_engine.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>
#include <optional>
#include <string>

namespace rune {
    namespace {
        enum class Composite : uint8_t {
            Replace,
            Alpha,
            Add,
            Modulate,
            Highlight,
            Multiply,
        };

        constexpr int RowBlock = 16;

        int nextOwnedRow(int y, int band, int bands)
        {
            int block = y / RowBlock;
            int offset = ((band - block % bands) % bands + bands) % bands;
            return offset == 0 ? y : (block + offset) * RowBlock;
        }

        constexpr int SmoothKernel[2][2][2] = {
            { { 0, 4 }, { 4, -4 } },
            { { -4, -8 }, { -8, 0 } },
        };

        bool configFlag(const char* key, bool fallback)
        {
            std::optional<std::string> value = config::system().get("SoftDrv.SoftwareRenderDevice", key);
            if (!value) {
                return fallback;
            }
            return *value == "True" || *value == "true" || *value == "1";
        }

        int mipFor(float footprint, size_t levels)
        {
            if (!(footprint >= 2.0f)) {
                return 0;
            }
            return std::min(std::ilogb(footprint), static_cast<int>(levels) - 1);
        }

        constexpr int VaryingU = 0;
        constexpr int VaryingLightU = 2;
        constexpr int VaryingColor = 4;
        constexpr int VaryingFog = 8;
        constexpr int VaryingWorld = 11;
        constexpr int VaryingNormal = 14;

        void composite(uint8_t* pixel, const float* source, Composite mode)
        {
            for (int channel = 0; channel < 3; channel++) {
                float dst = static_cast<float>(pixel[channel]) / 255.0f;
                float src = source[channel];
                float result = src;
                switch (mode) {
                case Composite::Replace:
                    result = src;
                    break;
                case Composite::Alpha:
                    result = src * source[3] + dst * (1.0f - source[3]);
                    break;
                case Composite::Add:
                    result = src + dst;
                    break;
                case Composite::Modulate:
                    result = 2.0f * src * dst;
                    break;
                case Composite::Highlight:
                    result = src + dst * (1.0f - source[3]);
                    break;
                case Composite::Multiply:
                    result = dst * src;
                    break;
                }
                pixel[channel] = static_cast<uint8_t>(std::clamp(result, 0.0f, 1.0f) * 255.0f + 0.5f);
            }
            pixel[3] = 255;
        }

        Composite compositeFor(BlendMode mode)
        {
            switch (mode) {
            case BlendMode::Translucent:
                return Composite::Add;
            case BlendMode::Modulated:
                return Composite::Modulate;
            case BlendMode::Alpha:
                return Composite::Alpha;
            case BlendMode::Highlight:
                return Composite::Highlight;
            case BlendMode::Multiply:
                return Composite::Multiply;
            case BlendMode::Opaque:
            case BlendMode::Masked:
                break;
            }
            return Composite::Replace;
        }

        void texel(const SoftMip& mip, int x, int y, float* out)
        {
            int width = static_cast<int>(mip.width);
            int height = static_cast<int>(mip.height);
            if ((mip.width & (mip.width - 1)) == 0 && (mip.height & (mip.height - 1)) == 0) {
                x &= width - 1;
                y &= height - 1;
            } else {
                x = ((x % width) + width) % width;
                y = ((y % height) + height) % height;
            }
            const uint8_t* source = mip.rgba.data() + (static_cast<size_t>(y) * mip.width + static_cast<size_t>(x)) * 4;
            constexpr float scale = 1.0f / 255.0f;
            out[0] = static_cast<float>(source[0]) * scale;
            out[1] = static_cast<float>(source[1]) * scale;
            out[2] = static_cast<float>(source[2]) * scale;
            out[3] = static_cast<float>(source[3]) * scale;
        }

        void lerpVertex(const SoftRenderDevice::Vertex& a, const SoftRenderDevice::Vertex& b, float t, SoftRenderDevice::Vertex& out)
        {
            for (int i = 0; i < 4; i++) {
                out.clip[i] = a.clip[i] + (b.clip[i] - a.clip[i]) * t;
            }
            for (int i = 0; i < SoftRenderDevice::Varyings; i++) {
                out.varying[i] = a.varying[i] + (b.varying[i] - a.varying[i]) * t;
            }
        }

        template <typename Distance>
        int clipPolygon(const SoftRenderDevice::Vertex* input, int count, SoftRenderDevice::Vertex* output, Distance distance)
        {
            int written = 0;
            for (int i = 0; i < count; i++) {
                const SoftRenderDevice::Vertex& current = input[i];
                const SoftRenderDevice::Vertex& next = input[(i + 1) % count];
                float dc = distance(current);
                float dn = distance(next);
                if (dc >= 0) {
                    output[written++] = current;
                }
                if ((dc >= 0) != (dn >= 0)) {
                    lerpVertex(current, next, dc / (dc - dn), output[written++]);
                }
            }
            return written;
        }
    }

    SoftTextures::SoftTextures()
    {
        SoftImage white;
        white.mips.push_back({ 1, 1, { 255, 255, 255, 255 } });
        white.smooth = false;
        images.emplace_back();
        images.push_back(std::move(white));
    }

    uint32_t SoftTextures::white() const
    {
        return 1;
    }

    void SoftTextures::beginFrame(double now)
    {
        time = now;
        frame++;
    }

    UTexture* SoftTextures::animated(UTexture* texture)
    {
        if (!texture || !texture->data || !texture::AnimNext(texture)) {
            return texture;
        }
        return TextureCache::advance(texture, animations[texture], time);
    }

    const SoftImage* SoftTextures::image(uint32_t id) const
    {
        if (id == 0 || id >= images.size() || images[id].mips.empty()) {
            return nullptr;
        }
        return &images[id];
    }

    uint32_t SoftTextures::store(SoftImage image)
    {
        images.push_back(std::move(image));
        return static_cast<uint32_t>(images.size() - 1);
    }

    bool SoftTextures::decode(UTexture* texture, bool masked, SoftImage& image)
    {
        uint32_t width = texture->width();
        uint32_t height = texture->height();
        for (size_t mip = 0; mip < texture->mips.size(); mip++) {
            const TextureMip& level = texture->mips[mip];
            if (level.width == 0 || level.height == 0) {
                break;
            }
            if (mip > 0 && (level.width != std::max(1u, width >> mip) || level.height != std::max(1u, height >> mip))) {
                break;
            }
            std::vector<uint8_t> pixels;
            try {
                pixels = texture->rgba(mip);
            } catch (const std::exception&) {
                break;
            }
            if (pixels.size() < static_cast<size_t>(level.width) * level.height * 4) {
                break;
            }
            if (texture->format() == 0) {
                size_t count = std::min(level.data.size(), pixels.size() / 4);
                bool paletteAlpha = texture->masked();
                for (size_t i = 0; i < count; i++) {
                    pixels[i * 4 + 3] = level.data[i] == 0 ? 0 : (paletteAlpha ? pixels[i * 4 + 3] : 255);
                    if (masked && level.data[i] == 0) {
                        pixels[i * 4 + 0] = 0;
                        pixels[i * 4 + 1] = 0;
                        pixels[i * 4 + 2] = 0;
                    }
                }
            }
            image.mips.push_back({ level.width, level.height, std::move(pixels) });
            if (level.width == 1 && level.height == 1) {
                break;
            }
        }
        image.masked = masked;
        image.smooth = !texture::bNoSmooth(texture);
        return !image.mips.empty();
    }

    const GLTextureInfo* SoftTextures::get(UTexture* texture, bool masked)
    {
        if (!texture) {
            return nullptr;
        }
        objects::ensureLoaded(texture);
        if (ProceduralTextures::isProcedural(texture)) {
            GLTextureInfo& info = textures[texture];
            uint64_t& lastFrame = proceduralFrames[texture];
            if (info.id && lastFrame == frame) {
                return &info;
            }
            lastFrame = frame;
            std::vector<uint8_t> rgba;
            uint32_t width = 0;
            uint32_t height = 0;
            bool changed = procedural.update(texture, time, rgba, width, height);
            if (info.id && !changed) {
                return &info;
            }
            if (rgba.empty()) {
                return info.id ? &info : nullptr;
            }
            SoftImage image;
            image.mips.push_back({ width, height, std::move(rgba) });
            image.masked = texture->masked();
            image.smooth = true;
            if (info.id) {
                images[info.id] = std::move(image);
            } else {
                info.id = store(std::move(image));
            }
            info.procedural = true;
            info.width = width;
            info.height = height;
            info.masked = texture->masked();
            info.smooth = true;
            info.levels = 1;
            return &info;
        }
        bool maskedCopy = masked && !texture->masked() && texture->format() == 0;
        std::unordered_map<UTexture*, GLTextureInfo>& cache = maskedCopy ? maskedTextures : textures;
        auto found = cache.find(texture);
        if (found != cache.end()) {
            return found->second.id ? &found->second : nullptr;
        }
        GLTextureInfo& info = cache[texture];
        if (!texture->decodable()) {
            return nullptr;
        }
        SoftImage image;
        if (!decode(texture, maskedCopy || texture->masked(), image)) {
            return nullptr;
        }
        info.width = texture->width();
        info.height = texture->height();
        info.masked = image.masked;
        info.smooth = image.smooth;
        info.levels = static_cast<int32_t>(image.mips.size());
        info.id = store(std::move(image));
        return &info;
    }

    void SoftTextures::clear()
    {
        images.resize(2);
        textures.clear();
        maskedTextures.clear();
        animations.clear();
        proceduralFrames.clear();
        procedural.clear();
    }

    SoftCanvas::SoftCanvas(SoftTextures& textureCache)
        : textures(textureCache)
    {
    }

    void SoftCanvas::setBrightness(float scale)
    {
        brightness = scale;
    }

    void SoftCanvas::setFlush(std::function<void()> hook)
    {
        flushHook = std::move(hook);
    }

    void SoftCanvas::begin(std::vector<uint32_t>* pixels, int width, int height)
    {
        target = pixels;
        viewWidth = std::max(width, 1);
        viewHeight = std::max(height, 1);
        clearClip();
    }

    float SoftCanvas::width() const
    {
        return static_cast<float>(viewWidth);
    }

    float SoftCanvas::height() const
    {
        return static_cast<float>(viewHeight);
    }

    void SoftCanvas::setClip(float x, float y, float w, float h)
    {
        clipRect[0] = std::clamp(static_cast<int>(std::floor(x)), 0, viewWidth);
        clipRect[1] = std::clamp(static_cast<int>(std::floor(y)), 0, viewHeight);
        clipRect[2] = std::clamp(static_cast<int>(std::floor(x)) + std::max(static_cast<int>(std::ceil(w)), 0), 0, viewWidth);
        clipRect[3] = std::clamp(static_cast<int>(std::floor(y)) + std::max(static_cast<int>(std::ceil(h)), 0), 0, viewHeight);
    }

    void SoftCanvas::clearClip()
    {
        clipRect[0] = 0;
        clipRect[1] = 0;
        clipRect[2] = viewWidth;
        clipRect[3] = viewHeight;
    }

    void SoftCanvas::drawTile(UTexture* texture, float x, float y, float w, float h, float u, float v, float ul, float vl, LinearColor color, BlendStyle style, bool)
    {
        if (style == BlendStyle::None || !target || w == 0 || h == 0) {
            return;
        }
        if (flushHook) {
            flushHook();
        }
        const SoftImage* image = nullptr;
        if (texture) {
            const GLTextureInfo* info = textures.get(textures.animated(texture), style == BlendStyle::Masked);
            image = info ? textures.image(info->id) : nullptr;
            if (!image) {
                return;
            }
        }
        if (style == BlendStyle::Modulated) {
            color = { 1, 1, 1, 1 };
        }
        bool alphaTest = image && image->masked && (style == BlendStyle::Normal || style == BlendStyle::Masked || style == BlendStyle::Modulated);
        Composite mode = Composite::Replace;
        switch (style) {
        case BlendStyle::Translucent:
            mode = Composite::Add;
            alphaTest = image != nullptr;
            break;
        case BlendStyle::Modulated:
            mode = Composite::Modulate;
            break;
        case BlendStyle::Highlighted:
            mode = Composite::Highlight;
            break;
        case BlendStyle::AlphaBlend:
            mode = Composite::Alpha;
            break;
        case BlendStyle::Multiply:
            mode = Composite::Multiply;
            break;
        case BlendStyle::None:
        case BlendStyle::Normal:
        case BlendStyle::Masked:
            mode = image ? Composite::Replace : Composite::Alpha;
            break;
        }
        float left = std::min(x, x + w);
        float right = std::max(x, x + w);
        float top = std::min(y, y + h);
        float bottom = std::max(y, y + h);
        int x0 = std::max(static_cast<int>(std::ceil(left - 0.5f)), clipRect[0]);
        int x1 = std::min(static_cast<int>(std::ceil(right - 0.5f)), clipRect[2]);
        int y0 = std::max(static_cast<int>(std::ceil(top - 0.5f)), clipRect[1]);
        int y1 = std::min(static_cast<int>(std::ceil(bottom - 0.5f)), clipRect[3]);
        float light = style == BlendStyle::Modulated ? 1.0f : brightness;
        const float tint[4] = { std::min(color.r * light, 1.0f), std::min(color.g * light, 1.0f), std::min(color.b * light, 1.0f), color.a };
        float source[4] = { tint[0], tint[1], tint[2], tint[3] };
        const SoftMip* mip = nullptr;
        if (image) {
            float scaleX = std::abs(ul / w);
            float scaleY = std::abs(vl / h);
            bool exact = scaleX == 1.0f && scaleY == 1.0f;
            mip = &image->mips[static_cast<size_t>(exact ? 0 : mipFor(1.6f * std::max(scaleX, scaleY), image->mips.size()))];
        }
        for (int py = y0; py < y1; py++) {
            float tv = v + (static_cast<float>(py) - y) / h * vl;
            uint32_t* row = target->data() + static_cast<size_t>(py) * static_cast<size_t>(viewWidth);
            for (int px = x0; px < x1; px++) {
                if (mip) {
                    float tu = u + (static_cast<float>(px) - x) / w * ul;
                    float mipScale = static_cast<float>(mip->width) / static_cast<float>(image->mips[0].width);
                    float mipScaleV = static_cast<float>(mip->height) / static_cast<float>(image->mips[0].height);
                    float sampled[4];
                    texel(*mip, static_cast<int>(std::floor(tu * mipScale + 0.0001f)), static_cast<int>(std::floor(tv * mipScaleV + 0.0001f)), sampled);
                    if (alphaTest && sampled[3] < 0.5f) {
                        continue;
                    }
                    for (int channel = 0; channel < 4; channel++) {
                        source[channel] = sampled[channel] * tint[channel];
                    }
                }
                composite(reinterpret_cast<uint8_t*>(row + px), source, mode);
            }
        }
    }

    void SoftCanvas::drawRect(float x, float y, float w, float h, LinearColor color, BlendStyle style)
    {
        drawTile(nullptr, x, y, w, h, 0, 0, 1, 1, color, style == BlendStyle::Masked ? BlendStyle::Normal : style, false);
    }

    void SoftCanvas::drawLine(float x1, float y1, float x2, float y2, LinearColor color)
    {
        if (!target) {
            return;
        }
        if (flushHook) {
            flushHook();
        }
        float source[4] = { color.r, color.g, color.b, color.a };
        int steps = static_cast<int>(std::ceil(std::max(std::abs(x2 - x1), std::abs(y2 - y1)))) + 1;
        for (int i = 0; i < steps; i++) {
            float t = steps > 1 ? static_cast<float>(i) / static_cast<float>(steps - 1) : 0.0f;
            int px = static_cast<int>(std::floor(x1 + (x2 - x1) * t + 0.5f));
            int py = static_cast<int>(std::floor(y1 + (y2 - y1) * t + 0.5f));
            if (px < clipRect[0] || px >= clipRect[2] || py < clipRect[1] || py >= clipRect[3]) {
                continue;
            }
            composite(reinterpret_cast<uint8_t*>(target->data() + static_cast<size_t>(py) * static_cast<size_t>(viewWidth) + static_cast<size_t>(px)), source, Composite::Alpha);
        }
    }

    void SoftCanvas::drawImage(const uint8_t* rgba, int imageWidth, int imageHeight, float x, float y, float w, float h)
    {
        if (!target || w <= 0 || h <= 0 || imageWidth <= 0 || imageHeight <= 0) {
            return;
        }
        if (flushHook) {
            flushHook();
        }
        int top = std::max(clipRect[1], static_cast<int>(std::lround(y)));
        int bottom = std::min(clipRect[3], static_cast<int>(std::lround(y + h)));
        int left = std::max(clipRect[0], static_cast<int>(std::lround(x)));
        int right = std::min(clipRect[2], static_cast<int>(std::lround(x + w)));
        float stepX = static_cast<float>(imageWidth) / w;
        float stepY = static_cast<float>(imageHeight) / h;
        for (int py = top; py < bottom; py++) {
            int sy = std::clamp(static_cast<int>((static_cast<float>(py) + 0.5f - y) * stepY), 0, imageHeight - 1);
            const uint8_t* row = rgba + static_cast<size_t>(sy) * static_cast<size_t>(imageWidth) * 4;
            uint32_t* out = target->data() + static_cast<size_t>(py) * static_cast<size_t>(viewWidth);
            for (int px = left; px < right; px++) {
                int sx = std::clamp(static_cast<int>((static_cast<float>(px) + 0.5f - x) * stepX), 0, imageWidth - 1);
                std::memcpy(out + px, row + static_cast<size_t>(sx) * 4, 4);
            }
        }
    }

    SoftWorkers::SoftWorkers(int count)
    {
        for (int i = 1; i < count; i++) {
            threads.emplace_back([this, i]() {
                loop(i);
            });
        }
    }

    SoftWorkers::~SoftWorkers()
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stopping = true;
        }
        wake.notify_all();
        for (std::thread& thread : threads) {
            thread.join();
        }
    }

    int SoftWorkers::count() const
    {
        return static_cast<int>(threads.size()) + 1;
    }

    void SoftWorkers::run(const std::function<void(int)>& job)
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            task = &job;
            remaining = static_cast<int>(threads.size());
            generation++;
        }
        wake.notify_all();
        job(0);
        std::unique_lock<std::mutex> lock(mutex);
        finished.wait(lock, [this]() {
            return remaining == 0;
        });
        task = nullptr;
    }

    void SoftWorkers::loop(int index)
    {
        uint64_t seen = 0;
        while (true) {
            const std::function<void(int)>* job = nullptr;
            {
                std::unique_lock<std::mutex> lock(mutex);
                wake.wait(lock, [this, seen]() {
                    return stopping || generation != seen;
                });
                if (stopping) {
                    return;
                }
                seen = generation;
                job = task;
            }
            (*job)(index);
            {
                std::lock_guard<std::mutex> lock(mutex);
                remaining--;
            }
            finished.notify_one();
        }
    }

    SoftRenderDevice::SoftRenderDevice()
        : canvasImpl(textures)
#ifdef __EMSCRIPTEN__
        , workers(std::make_unique<SoftWorkers>(1))
#else
        , workers(std::make_unique<SoftWorkers>(static_cast<int>(std::clamp(std::thread::hardware_concurrency(), 1u, 16u))))
#endif
    {
        color.assign(1, 0);
        depth.assign(1, 1.0f);
        fastTranslucency = configFlag("FastTranslucency", true);
        highResSmooth = configFlag("HighResTextureSmooth", true);
        lowResSmooth = configFlag("LowResTextureSmooth", false);
        coronas = configFlag("Coronas", false);
        canvasImpl.setFlush([this]() {
            flush();
        });
    }

    bool SoftRenderDevice::drawsCoronas()
    {
        return coronas;
    }

    bool SoftRenderDevice::drawsDecals()
    {
        return false;
    }

    bool SoftRenderDevice::blendsAlpha()
    {
        return false;
    }

    void SoftRenderDevice::setBrightness(float value)
    {
        brightness = 0.5f + value;
        canvasImpl.setBrightness(brightness);
        markState();
    }

    bool SoftRenderDevice::drawingToWindow()
    {
        return false;
    }

    bool SoftRenderDevice::lightsTranslucencyInOnePass()
    {
        return true;
    }

    void SoftRenderDevice::bindGammaTarget(int, int)
    {
    }

    void SoftRenderDevice::resolveGamma(int, int, float)
    {
    }

    void SoftRenderDevice::applyGamma(int, int, float)
    {
    }

    void SoftRenderDevice::beginScene(int, int, int)
    {
    }

    void SoftRenderDevice::endScene(int, int, bool)
    {
    }

    void SoftRenderDevice::readPixels(int width, int height, uint8_t* rgba)
    {
        flush();
        size_t stride = static_cast<size_t>(std::min(width, frameWidth)) * 4;
        for (int y = 0; y < std::min(height, frameHeight); y++) {
            const uint8_t* source = reinterpret_cast<const uint8_t*>(color.data() + static_cast<size_t>(frameHeight - 1 - y) * static_cast<size_t>(frameWidth));
            std::copy_n(source, stride, rgba + static_cast<size_t>(y) * static_cast<size_t>(width) * 4);
        }
    }

    void SoftRenderDevice::markState()
    {
        stateChanged = true;
    }

    void SoftRenderDevice::setViewport(int x, int y, int width, int height)
    {
        viewport[0] = x;
        viewport[1] = y;
        viewport[2] = width;
        viewport[3] = height;
        markState();
    }

    void SoftRenderDevice::enableScissor(int x, int y, int width, int height)
    {
        scissor = true;
        scissorRect[0] = x;
        scissorRect[1] = y;
        scissorRect[2] = width;
        scissorRect[3] = height;
        markState();
    }

    void SoftRenderDevice::disableScissor()
    {
        scissor = false;
        markState();
    }

    void SoftRenderDevice::currentBounds(int* bounds) const
    {
        bounds[0] = std::max(viewport[0], 0);
        bounds[2] = std::min(viewport[0] + viewport[2], frameWidth);
        bounds[1] = std::max(frameHeight - (viewport[1] + viewport[3]), 0);
        bounds[3] = std::min(frameHeight - viewport[1], frameHeight);
        if (scissor) {
            bounds[0] = std::max(bounds[0], scissorRect[0]);
            bounds[2] = std::min(bounds[2], scissorRect[0] + scissorRect[2]);
            bounds[1] = std::max(bounds[1], frameHeight - (scissorRect[1] + scissorRect[3]));
            bounds[3] = std::min(bounds[3], frameHeight - scissorRect[1]);
        }
    }

    void SoftRenderDevice::clear(bool clearColor, bool clearDepth, float depthValue)
    {
        Command command;
        command.clear = true;
        command.clearColor = clearColor && colorWrite;
        command.clearValue = 0xff000000u;
        command.clearDepth = clearDepth && depthWrite;
        command.depthValue = depthValue;
        if (!command.clearColor && !command.clearDepth) {
            return;
        }
        command.rect[0] = 0;
        command.rect[1] = 0;
        command.rect[2] = frameWidth;
        command.rect[3] = frameHeight;
        if (scissor) {
            command.rect[0] = std::clamp(scissorRect[0], 0, frameWidth);
            command.rect[2] = std::clamp(scissorRect[0] + scissorRect[2], 0, frameWidth);
            command.rect[1] = std::clamp(frameHeight - (scissorRect[1] + scissorRect[3]), 0, frameHeight);
            command.rect[3] = std::clamp(frameHeight - scissorRect[1], 0, frameHeight);
        } else if (command.clearColor && command.clearDepth) {
            commands.clear();
            screen.clear();
            states.clear();
            pendingQueries.clear();
            stateChanged = true;
            pendingArea = 0;
        }
        pendingArea += static_cast<float>((command.rect[2] - command.rect[0]) * (command.rect[3] - command.rect[1]));
        commands.push_back(command);
    }

    void SoftRenderDevice::clearColor(float r, float g, float b)
    {
        bool savedDepthWrite = depthWrite;
        depthWrite = false;
        clear(true, false, 1.0f);
        depthWrite = savedDepthWrite;
        if (!commands.empty() && commands.back().clear) {
            uint32_t red = static_cast<uint32_t>(std::clamp(r, 0.0f, 1.0f) * 255.0f + 0.5f);
            uint32_t green = static_cast<uint32_t>(std::clamp(g, 0.0f, 1.0f) * 255.0f + 0.5f);
            uint32_t blue = static_cast<uint32_t>(std::clamp(b, 0.0f, 1.0f) * 255.0f + 0.5f);
            commands.back().clearValue = red | (green << 8) | (blue << 16) | 0xff000000u;
        }
    }

    void SoftRenderDevice::setDepthTest(bool enabled)
    {
        depthTest = enabled;
        markState();
    }

    void SoftRenderDevice::setDepthWrite(bool enabled)
    {
        depthWrite = enabled;
        markState();
    }

    void SoftRenderDevice::setDepthFunc(DepthFunc func)
    {
        depthFunc = func;
        markState();
    }

    void SoftRenderDevice::setDepthRange(float nearValue, float farValue)
    {
        depthNear = nearValue;
        depthFar = farValue;
    }

    void SoftRenderDevice::setColorWrite(bool enabled)
    {
        colorWrite = enabled;
        markState();
    }

    void SoftRenderDevice::setCulling(bool enabled)
    {
        culling = enabled;
    }

    void SoftRenderDevice::setFrontFace(bool clockwise)
    {
        frontClockwise = clockwise;
    }

    void SoftRenderDevice::setPolygonOffset(bool enabled, float factor, float units)
    {
        offsetEnabled = enabled;
        offsetFactor = factor;
        offsetUnits = units;
        markState();
    }

    void SoftRenderDevice::setBlend(BlendMode mode)
    {
        blend = mode;
        depthWrite = mode == BlendMode::Opaque || mode == BlendMode::Masked;
        markState();
    }

    void SoftRenderDevice::setClipPlanes(const float* planes, int32_t count)
    {
        clipCount = std::clamp(count, 0, 8);
        std::copy(planes, planes + clipCount * 4, clipPlanes);
    }

    void SoftRenderDevice::useShader()
    {
    }

    void SoftRenderDevice::setViewProj(const Mat4& matrix)
    {
        viewProj = matrix;
    }

    void SoftRenderDevice::setFlags(int32_t newFlags)
    {
        flags = newFlags;
        markState();
    }

    void SoftRenderDevice::setQuality(bool, float newDetailDistance, float)
    {
        detailDistance = newDetailDistance;
    }

    void SoftRenderDevice::setPan(float u, float v)
    {
        pan[0] = u;
        pan[1] = v;
    }

    void SoftRenderDevice::setScale(float u, float v)
    {
        scale[0] = u;
        scale[1] = v;
    }

    void SoftRenderDevice::setTint(float r, float g, float b, float a)
    {
        tint[0] = r;
        tint[1] = g;
        tint[2] = b;
        tint[3] = a;
        markState();
    }

    void SoftRenderDevice::setEye(const Vector& newEye, const Vector& newForward)
    {
        eye = newEye;
        forward = newForward;
    }

    void SoftRenderDevice::setDynamicLights(const std::vector<DynamicLight>& newLights)
    {
        lights.assign(newLights.begin(), newLights.begin() + static_cast<std::ptrdiff_t>(std::min(newLights.size(), MaxDynamicLights)));
        markState();
    }

    void SoftRenderDevice::bindTexture(uint32_t texture)
    {
        boundTexture = texture;
        markState();
    }

    void SoftRenderDevice::bindLightmap(uint32_t texture)
    {
        boundLightmap = texture;
        markState();
    }

    void SoftRenderDevice::draw(const std::vector<GLVertex>& vertices, Primitive primitive)
    {
        if (primitive == Primitive::Triangles) {
            drawTriangles(vertices.data(), vertices.size());
        } else if (primitive == Primitive::Lines) {
            drawLines(vertices.data(), vertices.size());
        }
    }

    void SoftRenderDevice::upload(const std::vector<GLVertex>& vertices)
    {
        stream = vertices;
    }

    void SoftRenderDevice::drawRange(int32_t first, int32_t count)
    {
        if (first < 0 || count <= 0 || static_cast<size_t>(first + count) > stream.size()) {
            return;
        }
        drawTriangles(stream.data() + first, static_cast<size_t>(count));
    }

    uint32_t SoftRenderDevice::createStatic(const std::vector<GLVertex>& vertices)
    {
        uint32_t id = nextStatic++;
        statics[id] = vertices;
        return id;
    }

    void SoftRenderDevice::deleteStatic(uint32_t geometry)
    {
        statics.erase(geometry);
    }

    void SoftRenderDevice::bindStatic(uint32_t)
    {
    }

    void SoftRenderDevice::drawStatic(uint32_t geometry, int32_t first, int32_t count)
    {
        drawStaticRanges(geometry, &first, &count, 1);
    }

    void SoftRenderDevice::drawStaticRanges(uint32_t geometry, const int32_t* firsts, const int32_t* counts, int32_t ranges)
    {
        auto found = statics.find(geometry);
        if (found == statics.end()) {
            return;
        }
        const std::vector<GLVertex>& vertices = found->second;
        for (int32_t i = 0; i < ranges; i++) {
            if (firsts[i] < 0 || counts[i] <= 0 || static_cast<size_t>(firsts[i] + counts[i]) > vertices.size()) {
                continue;
            }
            drawTriangles(vertices.data() + firsts[i], static_cast<size_t>(counts[i]));
        }
    }

    uint32_t SoftRenderDevice::createQuery()
    {
        uint32_t id = nextQuery++;
        queries[id] = false;
        return id;
    }

    void SoftRenderDevice::deleteQuery(uint32_t query)
    {
        queries.erase(query);
    }

    void SoftRenderDevice::beginQuery(uint32_t query)
    {
        activeQuery = query;
        queries[query] = false;
        markState();
    }

    void SoftRenderDevice::endQuery()
    {
        activeQuery = 0;
        markState();
    }

    bool SoftRenderDevice::queryResult(uint32_t query, bool& visible)
    {
        if (pendingQueries.count(query)) {
            return false;
        }
        auto found = queries.find(query);
        if (found == queries.end()) {
            return false;
        }
        visible = found->second;
        return true;
    }

    void SoftRenderDevice::beginTextureFrame(double time)
    {
        textures.beginFrame(time);
    }

    UTexture* SoftRenderDevice::animated(UTexture* texture)
    {
        return textures.animated(texture);
    }

    const GLTextureInfo* SoftRenderDevice::texture(UTexture* texture)
    {
        return textures.get(texture, false);
    }

    const GLTextureInfo* SoftRenderDevice::maskedTexture(UTexture* texture)
    {
        return textures.get(texture, true);
    }

    uint32_t SoftRenderDevice::whiteTexture()
    {
        return textures.white();
    }

    void SoftRenderDevice::setTextureFiltering(float, bool)
    {
    }

    void SoftRenderDevice::clearTextures()
    {
        flush();
        textures.clear();
    }

    uint32_t SoftRenderDevice::createLightmap(int32_t size, const uint8_t* rgba)
    {
        flush();
        uint32_t id = nextLightmap++;
        Lightmap& page = lightmaps[id];
        page.size = size;
        page.rgba.assign(rgba, rgba + static_cast<size_t>(size) * static_cast<size_t>(size) * 4);
        return id;
    }

    void SoftRenderDevice::updateLightmap(uint32_t texture, int32_t x, int32_t y, int32_t width, int32_t height, const uint8_t* rgba)
    {
        flush();
        auto found = lightmaps.find(texture);
        if (found == lightmaps.end()) {
            return;
        }
        Lightmap& page = found->second;
        for (int32_t row = 0; row < height; row++) {
            int32_t ty = y + row;
            if (ty < 0 || ty >= page.size) {
                continue;
            }
            for (int32_t column = 0; column < width; column++) {
                int32_t tx = x + column;
                if (tx < 0 || tx >= page.size) {
                    continue;
                }
                std::copy_n(rgba + (static_cast<size_t>(row) * static_cast<size_t>(width) + static_cast<size_t>(column)) * 4, 4, page.rgba.data() + (static_cast<size_t>(ty) * static_cast<size_t>(page.size) + static_cast<size_t>(tx)) * 4);
            }
        }
    }

    void SoftRenderDevice::deleteLightmaps(const std::vector<uint32_t>& ids)
    {
        flush();
        for (uint32_t id : ids) {
            lightmaps.erase(id);
        }
    }

    Canvas2D& SoftRenderDevice::canvas()
    {
        return canvasImpl;
    }

    void SoftRenderDevice::beginCanvas(int width, int height)
    {
        flush();
        width = std::max(width, 1);
        height = std::max(height, 1);
        if (width != frameWidth || height != frameHeight) {
            frameWidth = width;
            frameHeight = height;
            color.assign(static_cast<size_t>(width) * static_cast<size_t>(height), 0);
            depth.assign(static_cast<size_t>(width) * static_cast<size_t>(height), 1.0f);
            markState();
        }
        canvasImpl.begin(&color, frameWidth, frameHeight);
    }

    void SoftRenderDevice::flushCanvas()
    {
    }

    const uint8_t* SoftRenderDevice::pixels()
    {
        flush();
        return reinterpret_cast<const uint8_t*>(color.data());
    }

    int SoftRenderDevice::width() const
    {
        return frameWidth;
    }

    int SoftRenderDevice::height() const
    {
        return frameHeight;
    }

    void SoftRenderDevice::shadeVertex(const GLVertex& source, Vertex& out) const
    {
        const Mat4& m = viewProj;
        for (int row = 0; row < 4; row++) {
            out.clip[row] = m[static_cast<size_t>(row)] * source.x + m[static_cast<size_t>(4 + row)] * source.y + m[static_cast<size_t>(8 + row)] * source.z + m[static_cast<size_t>(12 + row)];
        }
        float* v = out.varying;
        v[VaryingU] = source.u * scale[0] + pan[0];
        v[VaryingU + 1] = source.v * scale[1] + pan[1];
        v[VaryingLightU] = source.lu;
        v[VaryingLightU + 1] = source.lv;
        v[VaryingColor] = source.r;
        v[VaryingColor + 1] = source.g;
        v[VaryingColor + 2] = source.b;
        v[VaryingColor + 3] = source.a;
        if (flags & SHADER_Detail) {
            float distance = (Vector { source.x, source.y, source.z } - eye).dot(forward);
            float fade = std::nearbyint((1.0f - std::clamp(distance, 0.0f, detailDistance) / detailDistance) * 128.0f) / 255.0f;
            v[VaryingColor] = 128.0f / 255.0f;
            v[VaryingColor + 1] = 128.0f / 255.0f;
            v[VaryingColor + 2] = 128.0f / 255.0f;
            v[VaryingColor + 3] = fade;
        }
        v[VaryingFog] = source.fogR;
        v[VaryingFog + 1] = source.fogG;
        v[VaryingFog + 2] = source.fogB;
        v[VaryingWorld] = source.x;
        v[VaryingWorld + 1] = source.y;
        v[VaryingWorld + 2] = source.z;
        v[VaryingNormal] = source.nx;
        v[VaryingNormal + 1] = source.ny;
        v[VaryingNormal + 2] = source.nz;
    }

    void SoftRenderDevice::drawTriangles(const GLVertex* vertices, size_t count)
    {
        Vertex polygonA[24];
        Vertex polygonB[24];
        for (size_t first = 0; first + 2 < count; first += 3) {
            uint32_t outside[3] = {};
            for (int i = 0; i < 3; i++) {
                shadeVertex(vertices[first + static_cast<size_t>(i)], polygonA[i]);
                const Vertex& vertex = polygonA[i];
                for (int32_t plane = 0; plane < clipCount; plane++) {
                    const float* p = clipPlanes + plane * 4;
                    if (p[0] * vertex.varying[VaryingWorld] + p[1] * vertex.varying[VaryingWorld + 1] + p[2] * vertex.varying[VaryingWorld + 2] + p[3] < 0) {
                        outside[i] |= 1u << plane;
                    }
                }
                for (int axis = 0; axis < 6; axis++) {
                    float sign = (axis % 2) ? -1.0f : 1.0f;
                    if (vertex.clip[3] + sign * vertex.clip[axis / 2] < 0) {
                        outside[i] |= 1u << (8 + axis);
                    }
                }
            }
            if (outside[0] & outside[1] & outside[2]) {
                continue;
            }
            if ((outside[0] | outside[1] | outside[2]) == 0) {
                record(polygonA[0], polygonA[1], polygonA[2]);
                continue;
            }
            int points = 3;
            Vertex* input = polygonA;
            Vertex* output = polygonB;
            for (int32_t plane = 0; plane < clipCount && points >= 3; plane++) {
                const float* p = clipPlanes + plane * 4;
                points = clipPolygon(input, points, output, [p](const Vertex& vertex) {
                    return p[0] * vertex.varying[VaryingWorld] + p[1] * vertex.varying[VaryingWorld + 1] + p[2] * vertex.varying[VaryingWorld + 2] + p[3];
                });
                std::swap(input, output);
            }
            for (int axis = 0; axis < 6 && points >= 3; axis++) {
                int component = axis / 2;
                float sign = (axis % 2) ? -1.0f : 1.0f;
                points = clipPolygon(input, points, output, [component, sign](const Vertex& vertex) {
                    return vertex.clip[3] + sign * vertex.clip[component];
                });
                std::swap(input, output);
            }
            for (int i = 1; i + 1 < points; i++) {
                record(input[0], input[i], input[i + 1]);
            }
        }
    }

    void SoftRenderDevice::drawLines(const GLVertex* vertices, size_t count)
    {
        for (size_t first = 0; first + 1 < count; first += 2) {
            Vertex ends[2];
            shadeVertex(vertices[first], ends[0]);
            shadeVertex(vertices[first + 1], ends[1]);
            float nearA = ends[0].clip[3] + ends[0].clip[2];
            float nearB = ends[1].clip[3] + ends[1].clip[2];
            if (nearA < 0 && nearB < 0) {
                continue;
            }
            if (nearA < 0) {
                Vertex moved;
                lerpVertex(ends[0], ends[1], nearA / (nearA - nearB), moved);
                ends[0] = moved;
            } else if (nearB < 0) {
                Vertex moved;
                lerpVertex(ends[1], ends[0], nearB / (nearB - nearA), moved);
                ends[1] = moved;
            }
            float ax = ends[0].clip[0] / ends[0].clip[3];
            float ay = ends[0].clip[1] / ends[0].clip[3];
            float bx = ends[1].clip[0] / ends[1].clip[3];
            float by = ends[1].clip[1] / ends[1].clip[3];
            float width = static_cast<float>(std::max(viewport[2], 1));
            float height = static_cast<float>(std::max(viewport[3], 1));
            float dx = (bx - ax) * width;
            float dy = (by - ay) * height;
            float length = std::sqrt(dx * dx + dy * dy);
            if (length <= 0) {
                continue;
            }
            float offsetX = -dy / length / width;
            float offsetY = dx / length / height;
            Vertex quad[4] = { ends[0], ends[0], ends[1], ends[1] };
            for (int i = 0; i < 4; i++) {
                float side = (i == 0 || i == 3) ? 1.0f : -1.0f;
                quad[i].clip[0] += side * offsetX * quad[i].clip[3];
                quad[i].clip[1] += side * offsetY * quad[i].clip[3];
            }
            Vertex polygonA[24];
            Vertex polygonB[24];
            for (int i = 0; i < 4; i++) {
                polygonA[i] = quad[i];
            }
            int points = 4;
            Vertex* input = polygonA;
            Vertex* output = polygonB;
            for (int axis = 0; axis < 6 && points >= 3; axis++) {
                int component = axis / 2;
                float sign = (axis % 2) ? -1.0f : 1.0f;
                points = clipPolygon(input, points, output, [component, sign](const Vertex& vertex) {
                    return vertex.clip[3] + sign * vertex.clip[component];
                });
                std::swap(input, output);
            }
            for (int i = 1; i + 1 < points; i++) {
                record(input[0], input[i], input[i + 1]);
            }
        }
    }

    uint32_t SoftRenderDevice::currentState()
    {
        if (!stateChanged && !states.empty()) {
            return static_cast<uint32_t>(states.size() - 1);
        }
        DrawState state;
        currentBounds(state.bounds);
        state.depthTest = depthTest;
        state.depthWrite = depthWrite;
        state.depthFunc = depthFunc;
        state.colorWrite = colorWrite;
        state.offsetEnabled = offsetEnabled;
        state.offsetFactor = offsetFactor;
        state.offsetUnits = offsetUnits;
        state.blend = blend;
        state.flags = flags;
        std::copy(tint, tint + 4, state.tint);
        if (flags & SHADER_Dynamic) {
            state.lights = lights;
        }
        state.texture = boundTexture;
        state.lightmap = boundLightmap;
        state.brightness = brightness;
        state.query = activeQuery;
        if (activeQuery) {
            pendingQueries.insert(activeQuery);
        }
        states.push_back(std::move(state));
        stateChanged = false;
        return static_cast<uint32_t>(states.size() - 1);
    }

    void SoftRenderDevice::record(const Vertex& a, const Vertex& b, const Vertex& c)
    {
        const Vertex* corners[3] = { &a, &b, &c };
        ScreenVertex out[3];
        float glY[3];
        for (int i = 0; i < 3; i++) {
            const float* clip = corners[i]->clip;
            if (clip[3] <= 0) {
                return;
            }
            float inverseW = 1.0f / clip[3];
            out[i].inverseW = inverseW;
            out[i].x = static_cast<float>(viewport[0]) + (clip[0] * inverseW * 0.5f + 0.5f) * static_cast<float>(viewport[2]);
            glY[i] = static_cast<float>(viewport[1]) + (clip[1] * inverseW * 0.5f + 0.5f) * static_cast<float>(viewport[3]);
            out[i].y = static_cast<float>(frameHeight) - glY[i];
            out[i].z = depthNear + (clip[2] * inverseW * 0.5f + 0.5f) * (depthFar - depthNear);
            std::copy(corners[i]->varying, corners[i]->varying + Varyings, out[i].varying);
        }
        float glArea = (out[1].x - out[0].x) * (glY[2] - glY[0]) - (out[2].x - out[0].x) * (glY[1] - glY[0]);
        if (glArea == 0) {
            return;
        }
        bool front = frontClockwise ? glArea < 0 : glArea > 0;
        if (culling && !front) {
            return;
        }
        uint32_t state = currentState();
        if (commands.empty() || commands.back().clear || commands.back().state != state) {
            Command command;
            command.state = state;
            command.first = screen.size();
            commands.push_back(command);
        }
        screen.push_back(out[0]);
        screen.push_back(out[1]);
        screen.push_back(out[2]);
        commands.back().count += 3;
        pendingArea += std::abs(glArea) * 0.5f;
    }

    void SoftRenderDevice::flush()
    {
        if (commands.empty()) {
            return;
        }
        std::vector<std::atomic<bool>> hits(commands.size());
        for (std::atomic<bool>& hit : hits) {
            hit.store(false, std::memory_order_relaxed);
        }
        auto job = [this, &hits](int band) {
            int bands = workers->count();
            for (size_t i = 0; i < commands.size(); i++) {
                execute(commands[i], band, bands, hits[i]);
            }
        };
        if (pendingArea < 16384.0f || workers->count() == 1) {
            for (size_t i = 0; i < commands.size(); i++) {
                execute(commands[i], 0, 1, hits[i]);
            }
        } else {
            workers->run(job);
        }
        for (size_t i = 0; i < commands.size(); i++) {
            if (commands[i].clear) {
                continue;
            }
            uint32_t query = states[commands[i].state].query;
            if (query && hits[i].load(std::memory_order_relaxed)) {
                queries[query] = true;
            }
        }
        commands.clear();
        screen.clear();
        states.clear();
        pendingQueries.clear();
        stateChanged = true;
        pendingArea = 0;
    }

    void SoftRenderDevice::execute(const Command& command, int band, int bands, std::atomic<bool>& hit)
    {
        if (command.clear) {
            for (int y = nextOwnedRow(command.rect[1], band, bands); y < command.rect[3]; y = (y + 1) % RowBlock == 0 ? nextOwnedRow(y + 1, band, bands) : y + 1) {
                size_t row = static_cast<size_t>(y) * static_cast<size_t>(frameWidth);
                for (int x = command.rect[0]; x < command.rect[2]; x++) {
                    size_t index = row + static_cast<size_t>(x);
                    if (command.clearColor) {
                        color[index] = command.clearValue;
                    }
                    if (command.clearDepth) {
                        depth[index] = command.depthValue;
                    }
                }
            }
            return;
        }
        const DrawState& state = states[command.state];
        for (size_t i = command.first; i < command.first + command.count; i += 3) {
            rasterize(state, screen.data() + i, band, bands, hit);
        }
    }

    void SoftRenderDevice::rasterize(const DrawState& state, const ScreenVertex* corners, int band, int bands, std::atomic<bool>& hit)
    {
        int order[3] = { 0, 1, 2 };
        float area = (corners[1].x - corners[0].x) * (corners[2].y - corners[0].y) - (corners[2].x - corners[0].x) * (corners[1].y - corners[0].y);
        if (area == 0) {
            return;
        }
        if (area < 0) {
            std::swap(order[1], order[2]);
            area = -area;
        }
        float px[3];
        float py[3];
        float pz[3];
        float pw[3];
        const float* pv[3];
        for (int i = 0; i < 3; i++) {
            const ScreenVertex& corner = corners[order[i]];
            px[i] = corner.x;
            py[i] = corner.y;
            pz[i] = corner.z;
            pw[i] = corner.inverseW;
            pv[i] = corner.varying;
        }
        int minX = std::max(static_cast<int>(std::floor(std::min({ px[0], px[1], px[2] }))), state.bounds[0]);
        int maxX = std::min(static_cast<int>(std::ceil(std::max({ px[0], px[1], px[2] }))), state.bounds[2] - 1);
        int minY = std::max(static_cast<int>(std::floor(std::min({ py[0], py[1], py[2] }))), state.bounds[1]);
        int maxY = std::min(static_cast<int>(std::ceil(std::max({ py[0], py[1], py[2] }))), state.bounds[3] - 1);
        if (minX > maxX || minY > maxY) {
            return;
        }
        int firstRow = nextOwnedRow(minY, band, bands);
        if (firstRow > maxY) {
            return;
        }

        float inverseArea = 1.0f / area;
        float edgeA[3];
        float edgeB[3];
        float edgeC[3];
        bool inclusive[3];
        for (int i = 0; i < 3; i++) {
            int from = (i + 1) % 3;
            int to = (i + 2) % 3;
            edgeA[i] = -(py[to] - py[from]);
            edgeB[i] = px[to] - px[from];
            edgeC[i] = -(edgeA[i] * px[from] + edgeB[i] * py[from]);
            inclusive[i] = (py[from] == py[to] && px[to] < px[from]) || py[to] < py[from];
        }
        auto plane = [&](float v0, float v1, float v2, float& planeA, float& planeB, float& planeC) {
            planeA = (edgeA[0] * v0 + edgeA[1] * v1 + edgeA[2] * v2) * inverseArea;
            planeB = (edgeB[0] * v0 + edgeB[1] * v1 + edgeB[2] * v2) * inverseArea;
            planeC = (edgeC[0] * v0 + edgeC[1] * v1 + edgeC[2] * v2) * inverseArea;
        };
        float zA = ((pz[1] - pz[0]) * (py[2] - py[0]) - (pz[2] - pz[0]) * (py[1] - py[0])) * inverseArea;
        float zB = ((px[1] - px[0]) * (pz[2] - pz[0]) - (px[2] - px[0]) * (pz[1] - pz[0])) * inverseArea;
        float zC = pz[0] - zA * px[0] - zB * py[0];
        if (state.offsetEnabled) {
            zC += state.offsetFactor * std::max(std::abs(zA), std::abs(zB)) + state.offsetUnits / 16777216.0f;
        }
        float wA;
        float wB;
        float wC;
        plane(pw[0], pw[1], pw[2], wA, wB, wC);

        int32_t drawFlags = state.flags;
        bool textured = (drawFlags & (SHADER_Textured | SHADER_Detail)) != 0;
        bool lightmapped = (drawFlags & (SHADER_Lightmap | SHADER_Fog)) != 0;
        bool dynamic = (drawFlags & SHADER_Dynamic) && !state.lights.empty();
        bool vertexFog = false;
        for (int i = 0; i < 3; i++) {
            vertexFog = vertexFog || pv[i][VaryingFog] != 0 || pv[i][VaryingFog + 1] != 0 || pv[i][VaryingFog + 2] != 0;
        }
        Setup setup;
        int active = 0;
        auto use = [&](int first, int count) {
            for (int i = first; i < first + count; i++) {
                setup.active[active++] = i;
            }
        };
        use(VaryingColor, 4);
        if (textured) {
            use(VaryingU, 2);
        }
        if (lightmapped) {
            use(VaryingLightU, 2);
        }
        if (vertexFog) {
            use(VaryingFog, 3);
        }
        if (dynamic) {
            use(VaryingWorld, 3);
            use(VaryingNormal, 3);
        }
        float varying[Varyings] = {};
        int varied = 0;
        for (int k = 0; k < active; k++) {
            int i = setup.active[k];
            if (pv[0][i] == pv[1][i] && pv[0][i] == pv[2][i]) {
                varying[i] = pv[0][i];
            } else {
                setup.active[varied++] = i;
            }
        }
        active = varied;
        setup.activeCount = active;
        float planeA[Varyings] = {};
        float planeB[Varyings] = {};
        float planeC[Varyings] = {};
        for (int k = 0; k < active; k++) {
            int i = setup.active[k];
            plane(pv[0][i] * pw[0], pv[1][i] * pw[1], pv[2][i] * pw[2], planeA[i], planeB[i], planeC[i]);
        }
        setup.vertexFog = vertexFog;
        setup.dynamic = dynamic;
        setup.surface = (drawFlags & SHADER_Surface) != 0;
        setup.image = textured ? textures.image(state.texture) : nullptr;
        setup.mip = setup.image ? &setup.image->mips[0] : nullptr;
        setup.kernel = setup.surface && setup.image && setup.image->smooth && (drawFlags & SHADER_Textured) && (frameWidth > 400 ? highResSmooth : lowResSmooth);
        setup.stipple = setup.surface && state.blend == BlendMode::Translucent && fastTranslucency;
        setup.lightmap = nullptr;
        if (lightmapped) {
            auto found = lightmaps.find(state.lightmap);
            setup.lightmap = found == lightmaps.end() ? nullptr : &found->second;
        }
        float baseWidth = setup.image ? static_cast<float>(setup.image->mips[0].width) : 1.0f;
        float baseHeight = setup.image ? static_cast<float>(setup.image->mips[0].height) : 1.0f;
        Composite mode = setup.stipple ? Composite::Replace : compositeFor(state.blend);
        bool fast = setup.image && (drawFlags & SHADER_Textured) && !(drawFlags & (SHADER_Detail | SHADER_Fog)) && !dynamic && !vertexFog && mode == Composite::Replace;
        bool alphaTest = (drawFlags & SHADER_AlphaTest) != 0;
        bool lit = (drawFlags & SHADER_Lightmap) != 0;
        float scaleLight = state.blend == BlendMode::Modulated ? 1.0f : state.brightness;
        float lightScale[3];
        for (int channel = 0; channel < 3; channel++) {
            lightScale[channel] = 2.0f * scaleLight * state.tint[channel];
        }
        bool grouped = setup.lightmap && lit && !(drawFlags & (SHADER_Detail | SHADER_Fog));
        setup.grouped = grouped;
        int segment = frameWidth >= 800 ? 24 : 16;

        float fragment[4];
        bool depthAlways = state.depthFunc == DepthFunc::Always;
        bool depthEqual = state.depthFunc == DepthFunc::Equal;
        bool lightUsesVaryingColor = false;
        for (int k = 0; k < active; k++) {
            lightUsesVaryingColor = lightUsesVaryingColor || (setup.active[k] >= VaryingColor && setup.active[k] < VaryingColor + 3);
        }
        for (int y = firstRow; y <= maxY; y = (y + 1) % RowBlock == 0 ? nextOwnedRow(y + 1, band, bands) : y + 1) {
            float cy = static_cast<float>(y) + 0.5f;
            float left = static_cast<float>(minX);
            float right = static_cast<float>(maxX) + 1.0f;
            for (int i = 0; i < 3; i++) {
                float rowValue = edgeB[i] * cy + edgeC[i];
                if (edgeA[i] > 0) {
                    left = std::max(left, -rowValue / edgeA[i] - 0.5f - 0.001f);
                } else if (edgeA[i] < 0) {
                    right = std::min(right, -rowValue / edgeA[i] - 0.5f + 1.001f);
                } else if (rowValue < 0) {
                    right = left - 1;
                }
            }
            int x0 = std::max(minX, static_cast<int>(std::floor(left)));
            int x1 = std::min(maxX, static_cast<int>(std::ceil(right)));
            auto covered = [&](int column) {
                float sample = static_cast<float>(column) + 0.5f;
                for (int i = 0; i < 3; i++) {
                    float e = edgeA[i] * sample + edgeB[i] * cy + edgeC[i];
                    if (e < 0 || (e == 0 && !inclusive[i])) {
                        return false;
                    }
                }
                return true;
            };
            while (x0 <= x1 && !covered(x0)) {
                x0++;
            }
            while (x1 >= x0 && !covered(x1)) {
                x1--;
            }
            if (x0 > x1) {
                continue;
            }
            int spanLength = x1 - x0 + 1;
            int smoothed = spanLength / 8 * 8 + ((spanLength % 8) >= 4 ? 4 : 0);
            const int(*kernelRow)[2] = SmoothKernel[y & 1];
            auto lightAt = [&](int column, float* out) {
                float centre = static_cast<float>(column) + 0.5f;
                float inverse = 1.0f / (wA * centre + wB * cy + wC);
                float coordinates[Varyings];
                coordinates[VaryingLightU] = (planeA[VaryingLightU] * centre + planeB[VaryingLightU] * cy + planeC[VaryingLightU]) * inverse;
                coordinates[VaryingLightU + 1] = (planeA[VaryingLightU + 1] * centre + planeB[VaryingLightU + 1] * cy + planeC[VaryingLightU + 1]) * inverse;
                float sample[4];
                sampleLightmap(setup, coordinates, sample);
                out[0] = std::min(sample[0], 254.0f / 255.0f);
                out[1] = std::min(sample[1], 254.0f / 255.0f);
                out[2] = std::min(sample[2], 254.0f / 255.0f);
            };
            float following[3] = {};
            int followingAt = -1;
            float lightStep[3] = {};
            int groupEnd = x0;
            float exact[Varyings];
            float target[Varyings];
            float step[Varyings] = {};
            auto evaluate = [&](float column, float* out) {
                float inverse = 1.0f / (wA * column + wB * cy + wC);
                for (int k = 0; k < active; k++) {
                    int i = setup.active[k];
                    out[i] = (planeA[i] * column + planeB[i] * cy + planeC[i]) * inverse;
                }
                return inverse;
            };
            double depthValue = static_cast<double>(zA * (static_cast<float>(x0) + 0.5f) + zB * cy + zC);
            size_t index = static_cast<size_t>(y) * static_cast<size_t>(frameWidth) + static_cast<size_t>(x0);
            for (int first = x0; first <= x1; first += segment) {
                int last = std::min(first + segment, x1 + 1);
                float inverse = evaluate(static_cast<float>(first) + 0.5f, exact);
                evaluate(static_cast<float>(last) + 0.5f, target);
                float spread = 1.0f / static_cast<float>(last - first);
                for (int k = 0; k < active; k++) {
                    int i = setup.active[k];
                    varying[i] = exact[i];
                    step[i] = (target[i] - exact[i]) * spread;
                }
                if (setup.image) {
                    float u = exact[VaryingU];
                    float v = exact[VaryingU + 1];
                    float dudx = (planeA[VaryingU] - u * wA) * inverse * baseWidth;
                    float dvdx = (planeA[VaryingU + 1] - v * wA) * inverse * baseHeight;
                    float footprint;
                    if (setup.surface) {
                        float dudy = (planeB[VaryingU] - u * wB) * inverse * baseWidth;
                        float dvdy = (planeB[VaryingU + 1] - v * wB) * inverse * baseHeight;
                        footprint = std::sqrt(std::max(dudx * dudx + dvdx * dvdx, dudy * dudy + dvdy * dvdy));
                    } else {
                        footprint = 1.6f * std::max(std::abs(dudx), std::abs(dvdx));
                    }
                    setup.mip = &setup.image->mips[static_cast<size_t>(mipFor(footprint, setup.image->mips.size()))];
                }
                float colorScale[3] = { varying[VaryingColor] * lightScale[0], varying[VaryingColor + 1] * lightScale[1], varying[VaryingColor + 2] * lightScale[2] };
                const SoftMip* mip = setup.mip;
                float mipWidth = mip ? static_cast<float>(mip->width) : 1.0f;
                float mipHeight = mip ? static_cast<float>(mip->height) : 1.0f;
                bool powerOfTwo = mip && (mip->width & (mip->width - 1)) == 0 && (mip->height & (mip->height - 1)) == 0;
                int maskU = mip ? static_cast<int>(mip->width) - 1 : 0;
                int maskV = mip ? static_cast<int>(mip->height) - 1 : 0;
                float kernelU[2] = { 0, 0 };
                float kernelV[2] = { 0, 0 };
                if (setup.kernel && mip) {
                    for (int k = 0; k < 2; k++) {
                        kernelU[k] = static_cast<float>(kernelRow[k][0]) * (1.0f / 16.0f) * mipWidth / baseWidth;
                        kernelV[k] = static_cast<float>(kernelRow[k][1]) * (1.0f / 16.0f);
                    }
                }
                if (fast) {
                    float u = varying[VaryingU] * mipWidth;
                    float v = varying[VaryingU + 1] * mipHeight;
                    u -= std::floor(u / mipWidth) * mipWidth;
                    v -= std::floor(v / mipHeight) * mipHeight;
                    int32_t fixedU = static_cast<int32_t>(u * 65536.0f);
                    int32_t fixedV = static_cast<int32_t>(v * 65536.0f);
                    int32_t fixedStepU = static_cast<int32_t>(step[VaryingU] * mipWidth * 65536.0f);
                    int32_t fixedStepV = static_cast<int32_t>(step[VaryingU + 1] * mipHeight * 65536.0f);
                    int32_t kernelFixedU[2] = { static_cast<int32_t>(kernelU[0] * 65536.0f), static_cast<int32_t>(kernelU[1] * 65536.0f) };
                    int32_t kernelFixedV[2] = { static_cast<int32_t>(kernelV[0] * 65536.0f), static_cast<int32_t>(kernelV[1] * 65536.0f) };
                    int32_t textureWidth = static_cast<int32_t>(mip->width);
                    int32_t textureHeight = static_cast<int32_t>(mip->height);
                    float red = varying[VaryingColor];
                    float green = varying[VaryingColor + 1];
                    float blue = varying[VaryingColor + 2];
                    float stepRed = step[VaryingColor];
                    float stepGreen = step[VaryingColor + 1];
                    float stepBlue = step[VaryingColor + 2];
                    bool fixedLight = lit && grouped && !lightUsesVaryingColor;
                    int32_t scaled[3] = {};
                    int32_t scaledStep[3] = {};
                    if (fixedLight) {
                        for (int channel = 0; channel < 3; channel++) {
                            scaled[channel] = static_cast<int32_t>(setup.light[channel] * colorScale[channel] * 65536.0f);
                            scaledStep[channel] = static_cast<int32_t>(lightStep[channel] * colorScale[channel] * 65536.0f);
                        }
                    }
                    float light0 = setup.light[0];
                    float light1 = setup.light[1];
                    float light2 = setup.light[2];
                    const uint8_t* texels = mip->rgba.data();
                    bool stipple = setup.stipple;
                    bool kernel = setup.kernel;
                    bool testDepth = state.depthTest && !depthAlways;
                    bool writeDepth = state.depthTest && state.depthWrite;
                    bool writeColor = state.colorWrite;
                    bool reportHit = state.query != 0;
                    float tintScale[3] = { state.tint[0] * scaleLight, state.tint[1] * scaleLight, state.tint[2] * scaleLight };
                    uint32_t* colorRow = color.data();
                    float* depthRow = depth.data();
                    bool touched = false;
                    for (int x = first; x < last; x++, index++, depthValue += static_cast<double>(zA), fixedU += fixedStepU, fixedV += fixedStepV, red += stepRed, green += stepGreen, blue += stepBlue) {
                        float z = static_cast<float>(depthValue);
                        if (grouped && x == groupEnd) {
                            float start[3];
                            if (followingAt == x) {
                                start[0] = following[0];
                                start[1] = following[1];
                                start[2] = following[2];
                            } else {
                                lightAt(x, start);
                            }
                            int next = std::min(x + 8, x1);
                            lightAt(next, following);
                            followingAt = next;
                            float groupSpread = next > x ? 1.0f / static_cast<float>(next - x) : 0.0f;
                            light0 = start[0];
                            light1 = start[1];
                            light2 = start[2];
                            lightStep[0] = (following[0] - start[0]) * groupSpread;
                            lightStep[1] = (following[1] - start[1]) * groupSpread;
                            lightStep[2] = (following[2] - start[2]) * groupSpread;
                            if (fixedLight) {
                                for (int channel = 0; channel < 3; channel++) {
                                    scaled[channel] = static_cast<int32_t>(start[channel] * colorScale[channel] * 65536.0f);
                                    scaledStep[channel] = static_cast<int32_t>(lightStep[channel] * colorScale[channel] * 65536.0f);
                                }
                            }
                            groupEnd = x + 8;
                        }
                        float currentLight0 = light0;
                        float currentLight1 = light1;
                        float currentLight2 = light2;
                        int32_t multiplier[3] = { scaled[0] >> 8, scaled[1] >> 8, scaled[2] >> 8 };
                        light0 += lightStep[0];
                        light1 += lightStep[1];
                        light2 += lightStep[2];
                        scaled[0] += scaledStep[0];
                        scaled[1] += scaledStep[1];
                        scaled[2] += scaledStep[2];
                        if (stipple && ((x + y) & 1)) {
                            continue;
                        }
                        if (testDepth && (depthEqual ? z != depthRow[index] : z > depthRow[index])) {
                            continue;
                        }
                        int32_t sampleU = fixedU;
                        int32_t sampleV = fixedV;
                        if (kernel && x - x0 < smoothed) {
                            int phase = (x - x0) & 1;
                            sampleU += kernelFixedU[phase];
                            sampleV += kernelFixedV[phase];
                        }
                        int32_t iu = sampleU >> 16;
                        int32_t iv = sampleV >> 16;
                        if (powerOfTwo) {
                            iu &= maskU;
                            iv &= maskV;
                        } else {
                            iu = ((iu % textureWidth) + textureWidth) % textureWidth;
                            iv = ((iv % textureHeight) + textureHeight) % textureHeight;
                        }
                        const uint8_t* texelBytes = texels + (static_cast<size_t>(iv) * static_cast<size_t>(textureWidth) + static_cast<size_t>(iu)) * 4;
                        if (alphaTest && texelBytes[3] < 128) {
                            continue;
                        }
                        if (!fixedLight) {
                            float multiplier0;
                            float multiplier1;
                            float multiplier2;
                            if (lit) {
                                if (!grouped) {
                                    float coordinates[Varyings];
                                    coordinates[VaryingLightU] = varying[VaryingLightU] + step[VaryingLightU] * static_cast<float>(x - first);
                                    coordinates[VaryingLightU + 1] = varying[VaryingLightU + 1] + step[VaryingLightU + 1] * static_cast<float>(x - first);
                                    float sample[4];
                                    sampleLightmap(setup, coordinates, sample);
                                    currentLight0 = sample[0];
                                    currentLight1 = sample[1];
                                    currentLight2 = sample[2];
                                }
                                multiplier0 = red * std::min(currentLight0, 254.0f / 255.0f) * lightScale[0];
                                multiplier1 = green * std::min(currentLight1, 254.0f / 255.0f) * lightScale[1];
                                multiplier2 = blue * std::min(currentLight2, 254.0f / 255.0f) * lightScale[2];
                            } else {
                                multiplier0 = std::min(red * tintScale[0], 1.0f);
                                multiplier1 = std::min(green * tintScale[1], 1.0f);
                                multiplier2 = std::min(blue * tintScale[2], 1.0f);
                            }
                            multiplier[0] = static_cast<int32_t>(multiplier0 * 256.0f);
                            multiplier[1] = static_cast<int32_t>(multiplier1 * 256.0f);
                            multiplier[2] = static_cast<int32_t>(multiplier2 * 256.0f);
                        }
                        touched = true;
                        if (writeDepth) {
                            depthRow[index] = std::clamp(z, 0.0f, 1.0f);
                        }
                        if (writeColor) {
                            uint32_t outRed = static_cast<uint32_t>(std::min((static_cast<int32_t>(texelBytes[0]) * multiplier[0] + 128) >> 8, 255));
                            uint32_t outGreen = static_cast<uint32_t>(std::min((static_cast<int32_t>(texelBytes[1]) * multiplier[1] + 128) >> 8, 255));
                            uint32_t outBlue = static_cast<uint32_t>(std::min((static_cast<int32_t>(texelBytes[2]) * multiplier[2] + 128) >> 8, 255));
                            colorRow[index] = outRed | (outGreen << 8) | (outBlue << 16) | 0xff000000u;
                        }
                    }
                    setup.light[0] = light0;
                    setup.light[1] = light1;
                    setup.light[2] = light2;
                    if (touched && reportHit) {
                        hit.store(true, std::memory_order_relaxed);
                    }
                    continue;
                }
                for (int x = first; x < last; x++, index++, depthValue += static_cast<double>(zA)) {
                    float z = static_cast<float>(depthValue);
                    if (grouped && x == groupEnd) {
                        if (followingAt == x) {
                            setup.light[0] = following[0];
                            setup.light[1] = following[1];
                            setup.light[2] = following[2];
                        } else {
                            lightAt(x, setup.light);
                        }
                        int next = std::min(x + 8, x1);
                        lightAt(next, following);
                        followingAt = next;
                        float groupSpread = next > x ? 1.0f / static_cast<float>(next - x) : 0.0f;
                        for (int channel = 0; channel < 3; channel++) {
                            lightStep[channel] = (following[channel] - setup.light[channel]) * groupSpread;
                        }
                        groupEnd = x + 8;
                    }
                    bool pass = !setup.stipple || !((x + y) & 1);
                    if (pass && state.depthTest && !depthAlways) {
                        float stored = depth[index];
                        pass = depthEqual ? z == stored : z <= stored;
                    }
                    if (pass && fast) {
                        bool smoothPixel = setup.kernel && x - x0 < smoothed;
                        int phase = (x - x0) & 1;
                        float tu = varying[VaryingU] * mipWidth + (smoothPixel ? kernelU[phase] : 0.0f);
                        float tv = varying[VaryingU + 1] * mipHeight + (smoothPixel ? kernelV[phase] : 0.0f);
                        int iu = static_cast<int>(tu);
                        int iv = static_cast<int>(tv);
                        iu -= tu < static_cast<float>(iu) ? 1 : 0;
                        iv -= tv < static_cast<float>(iv) ? 1 : 0;
                        if (powerOfTwo) {
                            iu &= maskU;
                            iv &= maskV;
                        } else {
                            iu = ((iu % (maskU + 1)) + maskU + 1) % (maskU + 1);
                            iv = ((iv % (maskV + 1)) + maskV + 1) % (maskV + 1);
                        }
                        const uint8_t* texelBytes = mip->rgba.data() + (static_cast<size_t>(iv) * mip->width + static_cast<size_t>(iu)) * 4;
                        if (!(alphaTest && texelBytes[3] < 128)) {
                            float multiplier[3];
                            if (lit) {
                                float light[3] = { setup.light[0], setup.light[1], setup.light[2] };
                                if (!grouped) {
                                    float sample[4];
                                    sampleLightmap(setup, varying, sample);
                                    light[0] = sample[0];
                                    light[1] = sample[1];
                                    light[2] = sample[2];
                                }
                                if (lightUsesVaryingColor) {
                                    for (int channel = 0; channel < 3; channel++) {
                                        multiplier[channel] = varying[VaryingColor + channel] * std::min(light[channel], 254.0f / 255.0f) * lightScale[channel];
                                    }
                                } else {
                                    multiplier[0] = light[0] * colorScale[0];
                                    multiplier[1] = light[1] * colorScale[1];
                                    multiplier[2] = light[2] * colorScale[2];
                                }
                            } else {
                                for (int channel = 0; channel < 3; channel++) {
                                    multiplier[channel] = std::min(varying[VaryingColor + channel] * state.tint[channel] * scaleLight, 1.0f);
                                }
                            }
                            if (state.query) {
                                hit.store(true, std::memory_order_relaxed);
                            }
                            if (state.depthTest && state.depthWrite) {
                                depth[index] = std::clamp(z, 0.0f, 1.0f);
                            }
                            if (state.colorWrite) {
                                uint32_t red = static_cast<uint32_t>(std::min(static_cast<float>(texelBytes[0]) * multiplier[0] + 0.5f, 255.0f));
                                uint32_t green = static_cast<uint32_t>(std::min(static_cast<float>(texelBytes[1]) * multiplier[1] + 0.5f, 255.0f));
                                uint32_t blue = static_cast<uint32_t>(std::min(static_cast<float>(texelBytes[2]) * multiplier[2] + 0.5f, 255.0f));
                                color[index] = red | (green << 8) | (blue << 16) | 0xff000000u;
                            }
                        }
                    } else if (pass) {
                        setup.offsetU = 0;
                        setup.offsetV = 0;
                        if (setup.kernel && mip && x - x0 < smoothed) {
                            const int* offset = kernelRow[(x - x0) & 1];
                            setup.offsetU = static_cast<float>(offset[0]) * (1.0f / 16.0f) / baseWidth;
                            setup.offsetV = static_cast<float>(offset[1]) * (1.0f / 16.0f) / mipHeight;
                        }
                        if (shadeFragment(state, setup, varying, fragment)) {
                            if (state.query) {
                                hit.store(true, std::memory_order_relaxed);
                            }
                            if (state.depthTest && state.depthWrite) {
                                depth[index] = std::clamp(z, 0.0f, 1.0f);
                            }
                            if (state.colorWrite) {
                                composite(reinterpret_cast<uint8_t*>(color.data() + index), fragment, mode);
                            }
                        }
                    }
                    for (int k = 0; k < active; k++) {
                        int i = setup.active[k];
                        varying[i] += step[i];
                    }
                    if (grouped) {
                        setup.light[0] += lightStep[0];
                        setup.light[1] += lightStep[1];
                        setup.light[2] += lightStep[2];
                    }
                }
            }
        }
    }

    void SoftRenderDevice::sampleTexture(const Setup& setup, const float* varying, float* out) const
    {
        if (!setup.mip) {
            out[0] = 0;
            out[1] = 0;
            out[2] = 0;
            out[3] = 1;
            return;
        }
        const SoftMip& mip = *setup.mip;
        float u = (varying[VaryingU] + setup.offsetU) * static_cast<float>(mip.width);
        float v = (varying[VaryingU + 1] + setup.offsetV) * static_cast<float>(mip.height);
        texel(mip, static_cast<int>(std::floor(u)), static_cast<int>(std::floor(v)), out);
    }

    void SoftRenderDevice::sampleLightmap(const Setup& setup, const float* varying, float* light) const
    {
        const Lightmap* source = setup.lightmap;
        if (!source) {
            light[0] = 0;
            light[1] = 0;
            light[2] = 0;
            light[3] = 1;
            return;
        }
        float su = varying[VaryingLightU] * static_cast<float>(source->size) - 0.5f;
        float sv = varying[VaryingLightU + 1] * static_cast<float>(source->size) - 0.5f;
        int x = static_cast<int>(su);
        int y = static_cast<int>(sv);
        x -= su < static_cast<float>(x) ? 1 : 0;
        y -= sv < static_cast<float>(y) ? 1 : 0;
        int fx = static_cast<int>((su - static_cast<float>(x)) * 256.0f);
        int fy = static_cast<int>((sv - static_cast<float>(y)) * 256.0f);
        int last = source->size - 1;
        int x0 = std::clamp(x, 0, last);
        int x1 = std::clamp(x + 1, 0, last);
        int y0 = std::clamp(y, 0, last);
        int y1 = std::clamp(y + 1, 0, last);
        const uint8_t* row0 = source->rgba.data() + static_cast<size_t>(y0) * static_cast<size_t>(source->size) * 4;
        const uint8_t* row1 = source->rgba.data() + static_cast<size_t>(y1) * static_cast<size_t>(source->size) * 4;
        const uint8_t* a = row0 + x0 * 4;
        const uint8_t* b = row0 + x1 * 4;
        const uint8_t* c = row1 + x0 * 4;
        const uint8_t* d = row1 + x1 * 4;
        constexpr float normalize = 1.0f / (255.0f * 65536.0f);
        for (int channel = 0; channel < 4; channel++) {
            int top = a[channel] * (256 - fx) + b[channel] * fx;
            int bottom = c[channel] * (256 - fx) + d[channel] * fx;
            light[channel] = static_cast<float>(top * (256 - fy) + bottom * fy) * normalize;
        }
    }

    void SoftRenderDevice::dynamicLight(const DrawState& state, const float* varying, float* total) const
    {
        total[0] = 0;
        total[1] = 0;
        total[2] = 0;
        Vector world { varying[VaryingWorld], varying[VaryingWorld + 1], varying[VaryingWorld + 2] };
        Vector normal = Vector { varying[VaryingNormal], varying[VaryingNormal + 1], varying[VaryingNormal + 2] }.normal();
        for (const DynamicLight& light : state.lights) {
            Vector delta = light.location - world;
            float radius = light.radius;
            float index = std::floor(delta.dot(delta) * 4093.0f / (radius * radius) + 0.5f);
            if (index >= 4096.0f) {
                continue;
            }
            float v = std::sqrt((index + 1.0f) / 4096.0f);
            float falloff = 1.0f + 2.0f * v * v * v - 3.0f * v * v;
            Vector direction = delta.normal();
            float attenuation = falloff * std::abs(direction.dot(normal));
            if (light.cone < 1.0f) {
                float cosine = direction.dot(light.direction);
                if (cosine <= light.cone) {
                    continue;
                }
                float spot = (cosine - light.cone) / (1.0f - light.cone);
                attenuation *= spot * spot;
            }
            total[0] += light.color.x * attenuation;
            total[1] += light.color.y * attenuation;
            total[2] += light.color.z * attenuation;
        }
    }

    bool SoftRenderDevice::shadeFragment(const DrawState& state, const Setup& setup, const float* varying, float* out) const
    {
        int32_t drawFlags = state.flags;
        if (drawFlags & SHADER_Detail) {
            float sampled[4];
            sampleTexture(setup, varying, sampled);
            float fade = varying[VaryingColor + 3];
            for (int channel = 0; channel < 3; channel++) {
                out[channel] = varying[VaryingColor + channel] + (sampled[channel] - varying[VaryingColor + channel]) * fade;
            }
            out[3] = 1;
            return true;
        }
        if (drawFlags & SHADER_Fog) {
            sampleLightmap(setup, varying, out);
            return true;
        }
        float scaleLight = state.blend == BlendMode::Modulated ? 1.0f : state.brightness;
        bool lightmapped = (drawFlags & SHADER_Lightmap) != 0;
        float result[4];
        for (int channel = 0; channel < 4; channel++) {
            result[channel] = varying[VaryingColor + channel] * state.tint[channel];
            if (channel < 3 && !lightmapped) {
                result[channel] = std::min(result[channel] * scaleLight, 1.0f);
            }
        }
        float sampled[4] = { 1, 1, 1, 1 };
        if (drawFlags & SHADER_Textured) {
            sampleTexture(setup, varying, sampled);
            for (int channel = 0; channel < 4; channel++) {
                result[channel] *= sampled[channel];
            }
        }
        if ((drawFlags & SHADER_AlphaTest) && result[3] < 0.5f) {
            return false;
        }
        float dynamic[3] = { 0, 0, 0 };
        if (setup.dynamic) {
            dynamicLight(state, varying, dynamic);
        }
        if (lightmapped) {
            float light[4] = { setup.light[0], setup.light[1], setup.light[2], 1 };
            if (!setup.grouped) {
                sampleLightmap(setup, varying, light);
            }
            for (int channel = 0; channel < 3; channel++) {
                result[channel] *= std::min(light[channel] + dynamic[channel], 254.0f / 255.0f) * 2.0f * scaleLight;
            }
        } else if (setup.dynamic) {
            for (int channel = 0; channel < 3; channel++) {
                result[channel] += sampled[channel] * state.tint[channel] * dynamic[channel] * 2.0f * scaleLight;
            }
        }
        for (int channel = 0; channel < 3; channel++) {
            out[channel] = std::clamp(result[channel] + (setup.vertexFog ? varying[VaryingFog + channel] : 0.0f), 0.0f, 1.0f);
        }
        out[3] = result[3];
        return true;
    }
}
