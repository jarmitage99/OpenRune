#pragma once

#include "render/procedural.h"
#include "render/render_device.h"
#include "render/texture_cache.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace rune {
    struct SoftMip {
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<uint8_t> rgba;
    };

    struct SoftImage {
        std::vector<SoftMip> mips;
        bool masked = false;
        bool smooth = true;
    };

    class SoftTextures {
    public:
        SoftTextures();

        void beginFrame(double now);
        UTexture* animated(UTexture* texture);
        const GLTextureInfo* get(UTexture* texture, bool masked);
        const SoftImage* image(uint32_t id) const;
        uint32_t white() const;
        void clear();

    private:
        uint32_t store(SoftImage image);
        bool decode(UTexture* texture, bool masked, SoftImage& image);

        std::vector<SoftImage> images;
        std::unordered_map<UTexture*, GLTextureInfo> textures;
        std::unordered_map<UTexture*, GLTextureInfo> maskedTextures;
        std::unordered_map<UTexture*, TextureAnimation> animations;
        std::unordered_map<UTexture*, uint64_t> proceduralFrames;
        ProceduralTextures procedural;
        double time = 0;
        uint64_t frame = 0;
    };

    class SoftCanvas : public Canvas2D {
    public:
        explicit SoftCanvas(SoftTextures& textureCache);

        void begin(std::vector<uint32_t>* pixels, int width, int height);
        void setBrightness(float scale);
        void setFlush(std::function<void()> hook);

        float width() const override;
        float height() const override;
        void setClip(float x, float y, float w, float h) override;
        void clearClip() override;
        void drawTile(UTexture* texture, float x, float y, float w, float h, float u, float v, float ul, float vl, LinearColor color, BlendStyle style, bool smooth) override;
        void drawRect(float x, float y, float w, float h, LinearColor color, BlendStyle style) override;
        void drawLine(float x1, float y1, float x2, float y2, LinearColor color) override;
        void drawImage(const uint8_t* rgba, int imageWidth, int imageHeight, float x, float y, float w, float h) override;

    private:
        SoftTextures& textures;
        std::vector<uint32_t>* target = nullptr;
        int viewWidth = 1;
        int viewHeight = 1;
        int clipRect[4] = {};
        float brightness = 1;
        std::function<void()> flushHook;
    };

    class SoftWorkers {
    public:
        explicit SoftWorkers(int count);
        ~SoftWorkers();
        SoftWorkers(const SoftWorkers&) = delete;
        SoftWorkers& operator=(const SoftWorkers&) = delete;

        int count() const;
        void run(const std::function<void(int)>& job);

    private:
        void loop(int index);

        std::vector<std::thread> threads;
        std::mutex mutex;
        std::condition_variable wake;
        std::condition_variable finished;
        const std::function<void(int)>* task = nullptr;
        uint64_t generation = 0;
        int remaining = 0;
        bool stopping = false;
    };

    class SoftRenderDevice : public RenderDevice {
    public:
        SoftRenderDevice();

        bool drawingToWindow() override;
        bool lightsTranslucencyInOnePass() override;
        bool drawsCoronas() override;
        bool drawsDecals() override;
        bool blendsAlpha() override;
        void setBrightness(float brightness) override;
        void bindGammaTarget(int width, int height) override;
        void resolveGamma(int width, int height, float exponent) override;
        void applyGamma(int width, int height, float exponent) override;
        void beginScene(int width, int height, int samples) override;
        void endScene(int width, int height, bool fxaa) override;
        void readPixels(int width, int height, uint8_t* rgba) override;

        void setViewport(int x, int y, int width, int height) override;
        void enableScissor(int x, int y, int width, int height) override;
        void disableScissor() override;
        void clear(bool color, bool depth, float depthValue) override;
        void clearColor(float r, float g, float b) override;
        void setDepthTest(bool enabled) override;
        void setDepthWrite(bool enabled) override;
        void setDepthFunc(DepthFunc func) override;
        void setDepthRange(float nearValue, float farValue) override;
        void setColorWrite(bool enabled) override;
        void setCulling(bool enabled) override;
        void setFrontFace(bool clockwise) override;
        void setPolygonOffset(bool enabled, float factor, float units) override;
        void setBlend(BlendMode mode) override;
        void setClipPlanes(const float* planes, int32_t count) override;

        void useShader() override;
        void setViewProj(const Mat4& matrix) override;
        void setFlags(int32_t flags) override;
        void setQuality(bool smoothLightmap, float detailDistance, float fogScale) override;
        void setPan(float u, float v) override;
        void setScale(float u, float v) override;
        void setTint(float r, float g, float b, float a) override;
        void setEye(const Vector& eye, const Vector& forward) override;
        void setDynamicLights(const std::vector<DynamicLight>& lights) override;
        void bindTexture(uint32_t texture) override;
        void bindLightmap(uint32_t texture) override;

        void draw(const std::vector<GLVertex>& vertices, Primitive primitive) override;
        void upload(const std::vector<GLVertex>& vertices) override;
        void drawRange(int32_t first, int32_t count) override;
        uint32_t createStatic(const std::vector<GLVertex>& vertices) override;
        void deleteStatic(uint32_t geometry) override;
        void bindStatic(uint32_t geometry) override;
        void drawStatic(uint32_t geometry, int32_t first, int32_t count) override;
        void drawStaticRanges(uint32_t geometry, const int32_t* firsts, const int32_t* counts, int32_t ranges) override;

        uint32_t createQuery() override;
        void deleteQuery(uint32_t query) override;
        void beginQuery(uint32_t query) override;
        void endQuery() override;
        bool queryResult(uint32_t query, bool& visible) override;

        void beginTextureFrame(double time) override;
        UTexture* animated(UTexture* texture) override;
        const GLTextureInfo* texture(UTexture* texture) override;
        const GLTextureInfo* maskedTexture(UTexture* texture) override;
        uint32_t whiteTexture() override;
        void setTextureFiltering(float anisotropy, bool trilinear) override;
        void clearTextures() override;

        uint32_t createLightmap(int32_t size, const uint8_t* rgba) override;
        void updateLightmap(uint32_t texture, int32_t x, int32_t y, int32_t width, int32_t height, const uint8_t* rgba) override;
        void deleteLightmaps(const std::vector<uint32_t>& ids) override;

        Canvas2D& canvas() override;
        void beginCanvas(int width, int height) override;
        void flushCanvas() override;

        const uint8_t* pixels();
        int width() const;
        int height() const;

        static constexpr int Varyings = 17;

        struct Vertex {
            float clip[4] = {};
            float varying[Varyings] = {};
        };

    private:
        struct Lightmap {
            int32_t size = 0;
            std::vector<uint8_t> rgba;
        };

        struct ScreenVertex {
            float x = 0;
            float y = 0;
            float z = 0;
            float inverseW = 0;
            float varying[Varyings] = {};
        };

        struct DrawState {
            int bounds[4] = {};
            bool depthTest = false;
            bool depthWrite = true;
            DepthFunc depthFunc = DepthFunc::LessEqual;
            bool colorWrite = true;
            bool offsetEnabled = false;
            float offsetFactor = 0;
            float offsetUnits = 0;
            BlendMode blend = BlendMode::Opaque;
            int32_t flags = 0;
            float tint[4] = { 1, 1, 1, 1 };
            std::vector<DynamicLight> lights;
            uint32_t texture = 0;
            uint32_t lightmap = 0;
            float brightness = 1;
            uint32_t query = 0;
        };

        struct Command {
            uint32_t state = 0;
            size_t first = 0;
            size_t count = 0;
            bool clear = false;
            bool clearColor = false;
            bool clearDepth = false;
            float depthValue = 1;
            uint32_t clearValue = 0xff000000u;
            int rect[4] = {};
        };

        struct Setup {
            int active[Varyings] = {};
            int activeCount = 0;
            const SoftImage* image = nullptr;
            const SoftMip* mip = nullptr;
            float offsetU = 0;
            float offsetV = 0;
            bool surface = false;
            bool kernel = false;
            bool grouped = false;
            float light[3] = {};
            bool stipple = false;
            const Lightmap* lightmap = nullptr;
            bool vertexFog = false;
            bool dynamic = false;
        };

        void drawTriangles(const GLVertex* vertices, size_t count);
        void drawLines(const GLVertex* vertices, size_t count);
        void shadeVertex(const GLVertex& source, Vertex& out) const;
        void record(const Vertex& a, const Vertex& b, const Vertex& c);
        uint32_t currentState();
        void markState();
        void currentBounds(int* bounds) const;
        void flush();
        void execute(const Command& command, int band, int bands, std::atomic<bool>& hit);
        void rasterize(const DrawState& state, const ScreenVertex* corners, int band, int bands, std::atomic<bool>& hit);
        bool shadeFragment(const DrawState& state, const Setup& setup, const float* varying, float* color) const;
        void sampleTexture(const Setup& setup, const float* varying, float* texel) const;
        void sampleLightmap(const Setup& setup, const float* varying, float* light) const;
        void dynamicLight(const DrawState& state, const float* varying, float* total) const;

        std::vector<uint32_t> color;
        std::vector<float> depth;
        int frameWidth = 1;
        int frameHeight = 1;
        int viewport[4] = { 0, 0, 1, 1 };
        bool scissor = false;
        int scissorRect[4] = {};
        bool depthTest = false;
        bool depthWrite = true;
        DepthFunc depthFunc = DepthFunc::LessEqual;
        float depthNear = 0;
        float depthFar = 1;
        bool colorWrite = true;
        bool culling = false;
        bool frontClockwise = false;
        bool offsetEnabled = false;
        float offsetFactor = 0;
        float offsetUnits = 0;
        BlendMode blend = BlendMode::Opaque;
        float clipPlanes[32] = {};
        int32_t clipCount = 0;
        Mat4 viewProj {};
        int32_t flags = 0;
        float pan[2] = {};
        float scale[2] = { 1, 1 };
        float tint[4] = { 1, 1, 1, 1 };
        Vector eye;
        Vector forward;
        std::vector<DynamicLight> lights;
        uint32_t boundTexture = 0;
        uint32_t boundLightmap = 0;
        float detailDistance = 380;
        float brightness = 1;
        bool fastTranslucency = true;
        bool highResSmooth = true;
        bool lowResSmooth = false;
        bool coronas = false;
        std::unordered_map<uint32_t, std::vector<GLVertex>> statics;
        uint32_t nextStatic = 1;
        std::vector<GLVertex> stream;
        std::unordered_map<uint32_t, bool> queries;
        std::unordered_set<uint32_t> pendingQueries;
        uint32_t nextQuery = 1;
        uint32_t activeQuery = 0;
        std::unordered_map<uint32_t, Lightmap> lightmaps;
        uint32_t nextLightmap = 1;
        SoftTextures textures;
        SoftCanvas canvasImpl;
        std::vector<ScreenVertex> screen;
        std::vector<DrawState> states;
        std::vector<Command> commands;
        bool stateChanged = true;
        float pendingArea = 0;
        std::unique_ptr<SoftWorkers> workers;
    };
}
