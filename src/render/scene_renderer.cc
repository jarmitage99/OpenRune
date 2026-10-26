#include "render/scene_renderer.h"

#include "core/objects.h"
#include "core/vm.h"
#include "engine/mesh.h"
#include "engine/particles.h"
#include "engine/props_engine.h"
#include "engine/world.h"
#include "render/lighting.h"
#include "render/procedural.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <limits>
#include <map>
#include <set>

namespace rune {
    namespace {
        constexpr float sceneNearPlane = 4.0f;
        constexpr float overlayNearPlane = 1.0f;
        const Name nameActor("Actor");
        const Name nameDecal("Decal");
        const Name nameLevelInfo("LevelInfo");
        const Name nameMover("Mover");
        const Name namePolyobj("Polyobj");
        const Name namePawn("Pawn");
        const Name namePlayerPawn("PlayerPawn");
        const Name nameShadowUpdate("ShadowUpdate");
        const Name nameSkyZoneInfo("SkyZoneInfo");
        const Name nameTouch("Touch");
        const Name nameTrigger("Trigger");
        const Name nameZoneInfo("ZoneInfo");

        constexpr uint32_t PF_Invisible = 0x00000001;
        constexpr uint32_t PF_Masked = 0x00000002;
        constexpr uint32_t PF_Translucent = 0x00000004;
        constexpr uint32_t PF_NotSolid = 0x00000008;
        constexpr uint32_t PF_Environment = 0x00000010;
        constexpr uint32_t PF_Modulated = 0x00000040;
        constexpr uint32_t PF_FakeBackdrop = 0x00000080;
        constexpr uint32_t PF_TwoSided = 0x00000100;
        constexpr uint32_t PF_AutoUPan = 0x00000200;
        constexpr uint32_t PF_AutoVPan = 0x00000400;
        constexpr uint32_t PF_AlphaBlend = 0x00001000;
        constexpr uint32_t PF_SpecialLit = 0x00100000;
        constexpr uint32_t PF_Unlit = 0x00400000;
        constexpr uint32_t PF_Portal = 0x04000000;
        constexpr uint32_t PF_Mirrored = 0x08000000;
        constexpr int32_t MaxFrameRecursion = 4;

        constexpr uint8_t DT_None = 0;
        constexpr uint8_t DT_Sprite = 1;
        constexpr uint8_t DT_Mesh = 2;
        constexpr uint8_t DT_Brush = 3;
        constexpr uint8_t DT_VerticalSprite = 5;
        constexpr uint8_t DT_SkeletalMesh = 6;
        constexpr uint8_t DT_SpriteAnimOnce = 7;
        constexpr uint8_t DT_ParticleSystem = 8;

        constexpr uint8_t TT_Sight = 7;

        constexpr uint8_t STY_None = 0;
        constexpr uint8_t STY_Masked = 2;
        constexpr uint8_t STY_Translucent = 3;
        constexpr uint8_t STY_Modulated = 4;
        constexpr uint8_t STY_AlphaBlend = 5;
        constexpr float particleNearDepth = 48.0f;

        bool isTranslucentFlags(uint32_t flags)
        {
            return (flags & (PF_Translucent | PF_Modulated)) != 0;
        }

        void applyZoneFog(GLVertex& vertex, const ZoneFog& fog)
        {
            vertex.zoneFogR = fog.color.x;
            vertex.zoneFogG = fog.color.y;
            vertex.zoneFogB = fog.color.z;
            vertex.zoneFogInverse = fog.inverseDistance;
        }

        uint32_t styleFlags(UObject* actor)
        {
            switch (actor::Style(actor)) {
            case STY_Masked:
                return PF_Masked;
            case STY_Translucent:
                return PF_Translucent;
            case STY_Modulated:
                return PF_Modulated;
            case STY_AlphaBlend:
                return PF_AlphaBlend;
            default:
                return 0;
            }
        }

        uint32_t particleStyleFlags(uint8_t style)
        {
            switch (style) {
            case STY_Masked:
                return PF_Masked;
            case STY_Translucent:
                return PF_Translucent;
            case STY_Modulated:
                return PF_Modulated;
            case STY_AlphaBlend:
                return PF_AlphaBlend;
            default:
                return 0;
            }
        }

        uint32_t textureFlags(UTexture* texture)
        {
            if (!texture || !texture->data) {
                return 0;
            }
            uint32_t flags = 0;
            if (texture::bMasked(texture)) {
                flags |= PF_Masked;
            }
            if (texture::bTransparent(texture)) {
                flags |= PF_Translucent;
            }
            if (texture::bModulate(texture)) {
                flags |= PF_Modulated;
            }
            if (texture::bUnlit(texture)) {
                flags |= PF_Unlit;
            }
            if (texture::bTwoSided(texture)) {
                flags |= PF_TwoSided;
            }
            return flags;
        }

        UTexture* asTexture(UObject* object)
        {
            if (!object) {
                return nullptr;
            }
            objects::ensureLoaded(object);
            return cast<UTexture>(object);
        }

        void boundingSphere(const std::vector<Vector>& points, Vector& center, float& radius)
        {
            if (points.empty()) {
                center = {};
                radius = 0;
                return;
            }
            Vector low = points[0];
            Vector high = points[0];
            for (const Vector& point : points) {
                low = { std::min(low.x, point.x), std::min(low.y, point.y), std::min(low.z, point.z) };
                high = { std::max(high.x, point.x), std::max(high.y, point.y), std::max(high.z, point.z) };
            }
            center = (low + high) * 0.5f;
            float radiusSquared = 0;
            for (const Vector& point : points) {
                radiusSquared = std::max(radiusSquared, (point - center).sizeSquared());
            }
            radius = std::sqrt(radiusSquared) + 1.0f;
        }

        bool sameVector(const Vector& a, const Vector& b)
        {
            return a.x == b.x && a.y == b.y && a.z == b.z;
        }

        bool samePrepared(const PreparedLight& a, const PreparedLight& b)
        {
            return sameVector(a.location, b.location) && a.radius == b.radius && a.effect == b.effect && sameVector(a.spotDirection, b.spotDirection) && a.outer == b.outer;
        }

        bool timeDependentEffect(uint8_t effect)
        {
            return effect == 5 || effect == 6;
        }

        Vector scaleOf(uint8_t* scaleStruct)
        {
            Vector value = *reinterpret_cast<Vector*>(scaleStruct);
            if (value.x == 0 && value.y == 0 && value.z == 0) {
                return { 1, 1, 1 };
            }
            return value;
        }
    }

    UTexture* meshSkin(UObject* actor, UMesh* mesh, size_t index)
    {
        UTexture* skin = index < 8 ? asTexture(actor::MultiSkins(actor, static_cast<int>(index))) : nullptr;
        if (skin) {
            return skin;
        }
        UTexture* meshTexture = index < mesh->textures.size() ? asTexture(mesh->textures[index]) : nullptr;
        if (index != 0 && meshTexture) {
            return meshTexture;
        }
        UTexture* actorSkin = asTexture(actor::Skin(actor));
        if (actorSkin) {
            return actorSkin;
        }
        return meshTexture;
    }

    UTexture* meshEnvironmentMap(UObject* actor, UMesh* mesh, UObject* zone, UObject* levelInfo)
    {
        if (UTexture* texture = asTexture(actor::Texture(actor))) {
            return texture;
        }
        if (zone && zone->data && zone->isA(nameZoneInfo)) {
            if (UTexture* texture = asTexture(zoneinfo::EnvironmentMap(zone))) {
                return texture;
            }
        }
        if (levelInfo && levelInfo->data) {
            if (UTexture* texture = asTexture(zoneinfo::EnvironmentMap(levelInfo))) {
                return texture;
            }
        }
        UTexture* last = nullptr;
        for (size_t i = 0; i < mesh->textures.size(); i++) {
            if (UTexture* texture = meshSkin(actor, mesh, i)) {
                last = texture;
            }
        }
        return last;
    }

    void environmentCoords(const Vector& eyeToPoint, const Vector& normal, float& u, float& v)
    {
        Vector direction = eyeToPoint.normal();
        Vector reflected = direction - normal * (2.0f * direction.dot(normal));
        u = (reflected.x + 1.0f) * 0.5f;
        v = (reflected.y + 1.0f) * 0.5f;
    }

    UObject* frameSkyZone(UObject* zone)
    {
        if (!zone || !zone->data || !zone->isA(nameZoneInfo)) {
            return nullptr;
        }
        UObject* sky = zoneinfo::SkyZone(zone);
        if (!sky || !sky->data || sky->destroyed || !sky->isA(nameSkyZoneInfo)) {
            return nullptr;
        }
        return sky;
    }

    UTexture* spriteAnimOnceFrame(UObject* actor, UTexture* texture)
    {
        int32_t count = 1;
        for (UTexture* test = asTexture(texture::AnimNext(texture)); test && test != texture; test = asTexture(texture::AnimNext(test))) {
            count++;
        }
        double ratio = 1.0 - static_cast<double>(actor::LifeSpan(actor)) / static_cast<double>(actor::LifeSpan(actor->cls->defaultObject));
        double fraction = ratio < 0.0 ? 0.0 : (ratio < 1.0 ? ratio : 1.0);
        float scaled = static_cast<float>(fraction * static_cast<double>(count));
        int32_t frame = static_cast<int32_t>(std::nearbyint(scaled - 0.5f));
        frame = frame < 0 ? 0 : std::min(frame, count - 1);
        while (frame-- > 0) {
            texture = asTexture(texture::AnimNext(texture));
        }
        return texture;
    }

    bool drawnAsSprite(uint8_t drawType)
    {
        return drawType == DT_Sprite || drawType == DT_SpriteAnimOnce;
    }

    bool hiddenInGame(UObject* actor)
    {
        return actor::bHidden(actor) || actor::bDeleteMe(actor) || (actor::bSpecialRender(actor) && !objects::editorMode());
    }

    void checkSightTrigger(UObject* actor, const Vector& viewLocation, const Vector& viewDirection)
    {
        if (!actor->isA(nameTrigger) || trigger::TriggerType(actor) != TT_Sight || !trigger::bInitiallyActive(actor)) {
            return;
        }
        UObject* viewer = World::current ? World::current->player() : nullptr;
        if (!viewer || viewer->destroyed) {
            return;
        }
        Vector delta = actor::Location(actor) - viewLocation;
        float sightDistance = trigger::SightDistance(actor);
        if (sightDistance > 0 && delta.size() > sightDistance) {
            return;
        }
        if (delta.normal().dot(viewDirection) < trigger::SightAngle(actor)) {
            return;
        }
        vm::Call(actor, nameTouch).obj(viewer).run();
        if (trigger::bTriggerOnceOnly(actor)) {
            actor::bHidden(actor) = 1;
        }
    }

    uint32_t decalPolyFlags(UObject* decal)
    {
        return particleStyleFlags(actor::Style(decal));
    }

    bool screenFlash(UObject* player, LinearColor& color)
    {
        float scale = std::clamp(playerpawn::FlashScale(player).x, 0.0f, 1.0f);
        Vector fog = playerpawn::FlashFog(player);
        if (scale >= 1.0f && fog.x <= 0.0f && fog.y <= 0.0f && fog.z <= 0.0f) {
            return false;
        }
        color = { std::clamp(fog.x, 0.0f, 1.0f), std::clamp(fog.y, 0.0f, 1.0f), std::clamp(fog.z, 0.0f, 1.0f), 1.0f - scale };
        return true;
    }

    ZoneFog zoneDistanceFog(UObject* zone)
    {
        if (!zone || !zone->data || !zone->isA(nameZoneInfo) || zoneinfo::FogDistance(zone) <= 0) {
            return {};
        }
        return { fGetHSV(zoneinfo::FogHue(zone), zoneinfo::FogSaturation(zone), zoneinfo::FogBrightness(zone)), 1.0f / zoneinfo::FogDistance(zone) };
    }

    ZoneFog actorDistanceFog(UObject* actor)
    {
        if (actor::bUnlit(actor) || actor::Style(actor) == STY_Translucent || actor->isA(namePolyobj)) {
            return {};
        }
        return zoneDistanceFog(*reinterpret_cast<UObject**>(actor::Region(actor)));
    }

    bool farClipZone(UObject* zone, Vector& color, float& distance)
    {
        if (!zone || !zone->data || !zone->isA(nameZoneInfo) || !zoneinfo::bFogZone(zone) || !zoneinfo::bFarClipZone(zone)) {
            return false;
        }
        color = fGetHSV(zoneinfo::FogHue(zone), zoneinfo::FogSaturation(zone), zoneinfo::FogBrightness(zone));
        distance = std::max(zoneinfo::FogDistance(zone), 0.0f);
        return true;
    }

    bool decalHiddenByFog(UObject* decal, const ZoneFog& fog)
    {
        return fog.inverseDistance > 0 && (decalPolyFlags(decal) & (PF_Translucent | PF_Modulated | PF_AlphaBlend)) != 0;
    }

    SceneRenderer::SceneRenderer(std::unique_ptr<RenderDevice> renderDevice)
        : device(std::move(renderDevice))
        , lightmaps(*device)
    {
        startTime = static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()) / 1e6;
    }

    SceneRenderer::~SceneRenderer()
    {
        flushLevel();
    }

    void SceneRenderer::setTime(double seconds)
    {
        time = seconds;
        timeSet = true;
    }

    void SceneRenderer::setViewportTime(double seconds)
    {
        viewportTime = seconds;
    }

    void SceneRenderer::flushLevel()
    {
        if (levelData.vao) {
            device->deleteStatic(levelData.vao);
        }
        if (levelData.detailVao) {
            device->deleteStatic(levelData.detailVao);
        }
        if (levelData.mirrorVao) {
            device->deleteStatic(levelData.mirrorVao);
        }
        levelData = LevelData {};
        touchedBatches.clear();
        device->flushCanvas();
        device->clearTextures();
        lightmaps.clear();
        triangleCache.clear();
        poseCaches.clear();
        meshDrawCaches.clear();
        brushDrawCaches.clear();
        polyobjPolys.clear();
        actorLightCache.clear();
        for (auto& [actor, state] : occlusion) {
            if (state.query) {
                device->deleteQuery(state.query);
            }
        }
        occlusion.clear();
    }

    Canvas2D& SceneRenderer::canvas()
    {
        return device->canvas();
    }

    void SceneRenderer::beginFrame(int width, int height)
    {
        if (!timeSet) {
            double now = static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()) / 1e6;
            time = now - startTime;
        }
        currentTime = viewportTime >= 0 ? viewportTime : time;
        skelApproximation = 0;
        lightDelta = lastFrameTime < 0 ? 0 : std::clamp(static_cast<int32_t>((currentTime - lastFrameTime) * 768.0), 0, 255);
        lastFrameTime = currentTime;
        lightFrameCount++;
        tickLightRandoms(static_cast<float>(time));
        frameWidth = std::max(width, 1);
        frameHeight = std::max(height, 1);
        viewAspect = static_cast<float>(frameWidth) / static_cast<float>(frameHeight);
        device->beginTextureFrame(currentTime);
        offscreen = device->drawingToWindow() && std::abs(gammaExponent - 1.0f) >= 0.0001f;
        if (offscreen) {
            device->bindGammaTarget(frameWidth, frameHeight);
        }
        device->beginCanvas(frameWidth, frameHeight);
        device->setViewport(0, 0, frameWidth, frameHeight);
        device->setDepthWrite(true);
        device->clear(true, true, 1.0f);
    }

    void SceneRenderer::endFrame()
    {
        device->flushCanvas();
        device->setBlend(BlendMode::Opaque);
    }

    void SceneRenderer::applyBrightness(float brightness)
    {
        device->setBrightness(brightness);
        float exponent = 1.0f / (2.5f * std::max(brightness, 0.01f));
        gammaExponent = exponent;
        device->flushCanvas();
        if (offscreen) {
            offscreen = false;
            device->resolveGamma(frameWidth, frameHeight, exponent);
            return;
        }
        if (std::abs(exponent - 1.0f) < 0.0001f) {
            return;
        }
        device->applyGamma(frameWidth, frameHeight, exponent);
    }

    void SceneRenderer::setOptions(const RenderOptions& newOptions)
    {
        device->setTextureFiltering(newOptions.anisotropy, newOptions.trilinear);
        if (newOptions.smoothLightmaps != options.smoothLightmaps || newOptions.detailDistance != options.detailDistance || newOptions.fogDistance != options.fogDistance) {
            device->setQuality(newOptions.smoothLightmaps, newOptions.detailDistance, newOptions.fogDistance > 0 ? 1.0f / newOptions.fogDistance : 0.0f);
        }
        options = newOptions;
    }

    void SceneRenderer::readPixels(std::vector<uint8_t>& rgba, int& width, int& height)
    {
        device->flushCanvas();
        width = frameWidth;
        height = frameHeight;
        rgba.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
        std::vector<uint8_t> raw(rgba.size());
        device->readPixels(width, height, raw.data());
        size_t stride = static_cast<size_t>(width) * 4;
        for (size_t y = 0; y < static_cast<size_t>(height); y++) {
            std::copy_n(raw.data() + y * stride, stride, rgba.data() + (static_cast<size_t>(height) - 1 - y) * stride);
        }
    }

    SceneRenderer::View SceneRenderer::makeView(const Vector& location, const Rotator& rotation, float fov, float nearPlane)
    {
        View view = makeView(location, rotationAxes(rotation), fov, nearPlane);
        view.rotation = rotation;
        return view;
    }

    SceneRenderer::View SceneRenderer::makeView(const Vector& location, const Coords& axes, float fov, float nearPlane)
    {
        View view;
        view.location = location;
        view.fov = fov;
        view.axes = axes;
        float aspect = viewAspect;
        view.tanHalfWidth = std::tan(fov * Pi / 360.0f);
        view.tanHalfHeight = view.tanHalfWidth / aspect;
        view.viewProj = mat4Multiply(projectionMatrix(fov, aspect, nearPlane, 65536.0f), viewMatrix(location, axes));
        return view;
    }

    UObject* SceneRenderer::zoneActor(uint8_t zone) const
    {
        if (levelData.model && zone < levelData.model->zones.size()) {
            UObject* actor = levelData.model->zones[zone].zoneActor;
            if (actor) {
                objects::ensureLoaded(actor);
                return actor;
            }
        }
        return levelData.levelInfo;
    }

    uint8_t SceneRenderer::pointZone(const Vector& p) const
    {
        UModel* model = levelData.model;
        if (!model || model->nodes.empty()) {
            return 0;
        }
        int32_t node = 0;
        uint8_t zoneIndex = 0;
        for (int guard = 0; guard < 10000 && node >= 0 && static_cast<size_t>(node) < model->nodes.size(); guard++) {
            const BspNode& current = model->nodes[static_cast<size_t>(node)];
            float distance = current.plane.x * p.x + current.plane.y * p.y + current.plane.z * p.z - current.plane.w;
            bool front = distance >= 0;
            zoneIndex = current.zone[front ? 1 : 0];
            int32_t next = front ? current.front : current.back;
            if (next <= 0) {
                break;
            }
            node = next;
        }
        return zoneIndex;
    }

    int32_t SceneRenderer::pointLeaf(const Vector& p) const
    {
        UModel* model = levelData.model;
        if (!model || model->nodes.empty()) {
            return -1;
        }
        int32_t node = 0;
        int32_t leaf = -1;
        for (int guard = 0; guard < 10000 && node >= 0 && static_cast<size_t>(node) < model->nodes.size(); guard++) {
            const BspNode& current = model->nodes[static_cast<size_t>(node)];
            float distance = current.plane.x * p.x + current.plane.y * p.y + current.plane.z * p.z - current.plane.w;
            bool front = distance >= 0;
            leaf = current.leaf[front ? 1 : 0];
            int32_t next = front ? current.front : current.back;
            if (next <= 0) {
                break;
            }
            node = next;
        }
        return leaf;
    }

    uint8_t SceneRenderer::actorZone(UObject* actor) const
    {
        uint8_t* region = actor::Region(actor);
        if (*reinterpret_cast<UObject**>(region)) {
            return region[sizeof(UObject*) + sizeof(int32_t)];
        }
        return pointZone(actor::Location(actor));
    }

    UObject* SceneRenderer::zoneForActor(UObject* actor)
    {
        UObject* zone = *reinterpret_cast<UObject**>(actor::Region(actor));
        if (zone) {
            return zone;
        }
        return zoneActor(pointZone(actor::Location(actor)));
    }

    uint64_t SceneRenderer::visibleZones(uint8_t zone) const
    {
        UModel* model = levelData.model;
        if (!model || zone == 0 || zone >= model->zones.size()) {
            return ~0ull;
        }
        uint64_t mask = model->zones[zone].visibility;
        if (mask == 0) {
            return ~0ull;
        }
        return mask | 1ull | (1ull << zone);
    }

    bool SceneRenderer::inFrustum(const View& view, const Vector& center, float radius) const
    {
        Vector delta = center - view.location;
        float forward = delta.dot(view.axes.xAxis);
        if (forward < -radius) {
            return false;
        }
        if (view.farClip > 0 && delta.size() - radius > 1.5f * view.farClip) {
            return false;
        }
        if (forward > 0 && radius * static_cast<float>(frameWidth) * 0.5f < forward * view.tanHalfWidth) {
            return false;
        }
        for (const Plane& plane : view.cullPlanes) {
            if (plane.x * center.x + plane.y * center.y + plane.z * center.z + plane.w < -radius) {
                return false;
            }
        }
        return !sphereOutsideView(view, center, radius);
    }

    void SceneRenderer::occludeView(View& view, const std::vector<ScreenSpan>* open)
    {
        std::shared_ptr<ViewVisibility> visibility = std::make_shared<ViewVisibility>();
        view.visibility = visibility;
        visibility->mirrorSpans.resize(levelData.mirrors.size());
        UModel* model = levelData.model;
        if (!model || model->nodes.empty()) {
            return;
        }
        if (open) {
            spans.reset(viewportWidth, viewportHeight, *open);
        } else {
            spans.reset(viewportWidth, viewportHeight);
        }
        const Vector& eye = view.location;
        const Coords& axes = view.axes;
        stampZone(pointZone(eye));
        float halfWidth = static_cast<float>(viewportWidth) * 0.5f;
        float halfHeight = static_cast<float>(viewportHeight) * 0.5f;
        float scaleX = halfWidth / view.tanHalfWidth;
        float scaleY = halfHeight / view.tanHalfHeight;
        float guardWidth = view.tanHalfWidth * 1.02f;
        float guardHeight = view.tanHalfHeight * 1.02f;
        std::array<Plane, 13> planes;
        int32_t planeCount = 0;
        auto addPlane = [&](const Vector& normal, const Vector& point) {
            planes[static_cast<size_t>(planeCount++)] = { normal.x, normal.y, normal.z, -normal.dot(point) };
        };
        addPlane(axes.xAxis, eye + axes.xAxis * sceneNearPlane);
        addPlane(axes.xAxis * guardWidth - axes.yAxis, eye);
        addPlane(axes.xAxis * guardWidth + axes.yAxis, eye);
        addPlane(axes.xAxis * guardHeight - axes.zAxis, eye);
        addPlane(axes.xAxis * guardHeight + axes.zAxis, eye);
        for (int32_t i = 0; i < view.clipCount; i++) {
            planes[static_cast<size_t>(planeCount++)] = view.clipPlanes[static_cast<size_t>(i)];
        }
        auto distance = [](const Plane& plane, const Vector& point) {
            return plane.x * point.x + plane.y * point.y + plane.z * point.z + plane.w;
        };
        auto project = [&](const Vector& point) {
            Vector delta = point - eye;
            float forward = delta.dot(axes.xAxis);
            return ScreenPoint { halfWidth + delta.dot(axes.yAxis) / forward * scaleX, halfHeight - delta.dot(axes.zAxis) / forward * scaleY };
        };
        auto boxVisible = [&](const BoundingBox& box) {
            Vector corners[8];
            for (int32_t c = 0; c < 8; c++) {
                corners[c] = { (c & 1) ? box.max.x : box.min.x, (c & 2) ? box.max.y : box.min.y, (c & 4) ? box.max.z : box.min.z };
            }
            for (int32_t p = 0; p < planeCount; p++) {
                bool outside = true;
                for (const Vector& corner : corners) {
                    if (distance(planes[static_cast<size_t>(p)], corner) >= 0) {
                        outside = false;
                        break;
                    }
                }
                if (outside) {
                    return false;
                }
            }
            float left = std::numeric_limits<float>::max();
            float right = std::numeric_limits<float>::lowest();
            float top = std::numeric_limits<float>::max();
            float bottom = std::numeric_limits<float>::lowest();
            for (const Vector& corner : corners) {
                if ((corner - eye).dot(axes.xAxis) < sceneNearPlane) {
                    return spans.rectVisible(0, 0, viewportWidth, viewportHeight);
                }
                ScreenPoint point = project(corner);
                left = std::min(left, point.x);
                right = std::max(right, point.x);
                top = std::min(top, point.y);
                bottom = std::max(bottom, point.y);
            }
            left = std::clamp(left, -1.0f, static_cast<float>(viewportWidth) + 1.0f);
            right = std::clamp(right, -1.0f, static_cast<float>(viewportWidth) + 1.0f);
            top = std::clamp(top, -1.0f, static_cast<float>(viewportHeight) + 1.0f);
            bottom = std::clamp(bottom, -1.0f, static_cast<float>(viewportHeight) + 1.0f);
            return spans.rectVisible(static_cast<int32_t>(std::floor(left)) - 1, static_cast<int32_t>(std::floor(top)) - 1, static_cast<int32_t>(std::ceil(right)) + 1, static_cast<int32_t>(std::ceil(bottom)) + 1);
        };
        bool mirrorsActive = view.recursion < MaxFrameRecursion - 1;
        const std::vector<BspNode>& nodes = model->nodes;
        int32_t sequence = 0;
        Vector clipA[64];
        Vector clipB[64];
        ScreenPoint screen[64];
        std::vector<ScreenPoint> pendingPoints;
        std::vector<int32_t> pendingCounts;
        auto processPolygon = [&](int32_t index) {
            const BspNode& node = nodes[static_cast<size_t>(index)];
            if (node.numVertices < 3 || node.surf < 0 || static_cast<size_t>(node.surf) >= levelData.surfFlags.size()) {
                return;
            }
            uint32_t flags = levelData.surfFlags[static_cast<size_t>(node.surf)];
            float side = node.plane.x * eye.x + node.plane.y * eye.y + node.plane.z * eye.z - node.plane.w;
            bool twoSided = (flags & (PF_TwoSided | PF_Portal)) != 0;
            if (side < -1.0f && !twoSided) {
                return;
            }
            bool drawn = false;
            for (int32_t e = levelData.nodeEmitStart[static_cast<size_t>(index)]; e < levelData.nodeEmitStart[static_cast<size_t>(index) + 1]; e++) {
                if (view.zoneMask & (1ull << (levelData.batches[static_cast<size_t>(levelData.nodeEmits[static_cast<size_t>(e)].batch)].zone & 63))) {
                    drawn = true;
                    break;
                }
            }
            if (!drawn) {
                return;
            }
            Vector* input = clipA;
            Vector* output = clipB;
            int32_t count = std::min<int32_t>(node.numVertices, 32);
            for (int32_t i = 0; i < count; i++) {
                input[i] = model->points[static_cast<size_t>(model->verts[static_cast<size_t>(node.vertPool + i)].vertex)];
            }
            for (int32_t p = 0; p < planeCount && count >= 3; p++) {
                const Plane& plane = planes[static_cast<size_t>(p)];
                int32_t produced = 0;
                for (int32_t i = 0; i < count; i++) {
                    const Vector& a = input[i];
                    const Vector& b = input[(i + 1) % count];
                    float da = distance(plane, a);
                    float db = distance(plane, b);
                    if (da >= 0) {
                        output[produced++] = a;
                    }
                    if ((da >= 0) != (db >= 0) && produced < 63) {
                        output[produced++] = a + (b - a) * (da / (da - db));
                    }
                }
                std::swap(input, output);
                count = produced;
            }
            if (count < 3) {
                return;
            }
            for (int32_t i = 0; i < count; i++) {
                bspDepth = std::max(bspDepth, (input[i] - eye).dot(axes.xAxis));
                screen[i] = project(input[i]);
            }
            int32_t mirror = mirrorsActive && (flags & PF_Mirrored) ? levelData.nodeMirror[static_cast<size_t>(index)] : -1;
            bool backdrop = (flags & PF_FakeBackdrop) && levelData.hasBackdrop;
            scratchSpans.clear();
            if (!spans.polygonVisible(screen, count, (mirror >= 0 || backdrop) ? &scratchSpans : nullptr)) {
                return;
            }
            visibility->nodes.push_back(index);
            visibility->nodeSequence.push_back(sequence++);
            for (int32_t e = levelData.nodeEmitStart[static_cast<size_t>(index)]; e < levelData.nodeEmitStart[static_cast<size_t>(index) + 1]; e++) {
                uint8_t emitZone = levelData.batches[static_cast<size_t>(levelData.nodeEmits[static_cast<size_t>(e)].batch)].zone;
                if (view.zoneMask & (1ull << (emitZone & 63))) {
                    stampZone(emitZone);
                }
            }
            if (mirror >= 0) {
                std::vector<ScreenSpan>& target = visibility->mirrorSpans[static_cast<size_t>(mirror)];
                target.insert(target.end(), scratchSpans.begin(), scratchSpans.end());
            } else if (backdrop) {
                visibility->backdropSpans.insert(visibility->backdropSpans.end(), scratchSpans.begin(), scratchSpans.end());
            }
            bool occluder = mirror >= 0 || backdrop || !(flags & (PF_Masked | PF_Translucent | PF_Modulated | PF_Invisible | PF_Portal));
            if (occluder && (side > 0 || twoSided)) {
                pendingCounts.push_back(count);
                pendingPoints.insert(pendingPoints.end(), screen, screen + count);
            }
        };
        auto visitLeaf = [&](int32_t leaf) {
            if (leaf >= 0 && static_cast<size_t>(leaf) < model->leaves.size()) {
                visibility->leaves.emplace_back(leaf, sequence);
            }
            sequence++;
        };
        std::vector<std::pair<int32_t, int32_t>> stack;
        stack.emplace_back(0, 0);
        while (!stack.empty() && !spans.full()) {
            auto [index, stage] = stack.back();
            stack.pop_back();
            const BspNode& node = nodes[static_cast<size_t>(index)];
            bool front = node.plane.x * eye.x + node.plane.y * eye.y + node.plane.z * eye.z - node.plane.w > 0;
            if (stage == 0) {
                bool zoneHidden = view.zoneMask != ~0ull && node.zoneMask && !(node.zoneMask & view.zoneMask);
                if (zoneHidden || (node.renderBound >= 0 && static_cast<size_t>(node.renderBound) < model->nodeBounds.size() && model->nodeBounds[static_cast<size_t>(node.renderBound)].valid && !boxVisible(model->nodeBounds[static_cast<size_t>(node.renderBound)]))) {
                    visibility->culled.emplace_back(index, sequence++);
                    continue;
                }
                stack.emplace_back(index, 1);
                int32_t nearChild = front ? node.front : node.back;
                if (nearChild > 0) {
                    stack.emplace_back(nearChild, 0);
                } else {
                    visitLeaf(node.leaf[front ? 1 : 0]);
                }
                continue;
            }
            pendingPoints.clear();
            pendingCounts.clear();
            for (int32_t plane = index; plane >= 0 && static_cast<size_t>(plane) < nodes.size();) {
                processPolygon(plane);
                int32_t next = nodes[static_cast<size_t>(plane)].iPlane;
                if (next == plane) {
                    break;
                }
                plane = next;
            }
            size_t offset = 0;
            for (int32_t count : pendingCounts) {
                spans.occlude(pendingPoints.data() + offset, count);
                offset += static_cast<size_t>(count);
            }
            int32_t farChild = front ? node.back : node.front;
            if (farChild > 0) {
                stack.emplace_back(farChild, 0);
            } else {
                visitLeaf(node.leaf[front ? 0 : 1]);
            }
        }
        visibility->endSequence = sequence;
    }

    void SceneRenderer::gatherVisible(const View& view)
    {
        for (int32_t batch : touchedBatches) {
            levelData.batchRanges[static_cast<size_t>(batch)].clear();
        }
        touchedBatches.clear();
        visibleStamp++;
        if (!view.visibility) {
            return;
        }
        UModel* model = levelData.model;
        const ViewVisibility& visibility = *view.visibility;
        for (size_t i = 0; i < visibility.nodes.size(); i++) {
            nodeOrder[static_cast<size_t>(visibility.nodes[i])] = -visibility.nodeSequence[i];
        }
        for (const auto& [leaf, sequence] : visibility.leaves) {
            leafOrder[static_cast<size_t>(leaf)] = -sequence;
            levelData.leafStamp[static_cast<size_t>(leaf)] = visibleStamp;
        }
        for (const auto& [node, sequence] : visibility.culled) {
            levelData.cullOrder[static_cast<size_t>(node)] = -sequence;
            levelData.cullStamp[static_cast<size_t>(node)] = visibleStamp;
        }
        fallbackOrder = -visibility.endSequence - 1;
        for (int32_t node : visibility.nodes) {
            if (view.farClip > 0) {
                const Plane& plane = model->nodes[static_cast<size_t>(node)].plane;
                if (std::abs(plane.x * view.location.x + plane.y * view.location.y + plane.z * view.location.z - plane.w) > view.farClip) {
                    continue;
                }
            }
            levelData.nodeStamp[static_cast<size_t>(node)] = visibleStamp;
            levelData.surfStamp[static_cast<size_t>(model->nodes[static_cast<size_t>(node)].surf)] = visibleStamp;
            for (int32_t e = levelData.nodeEmitStart[static_cast<size_t>(node)]; e < levelData.nodeEmitStart[static_cast<size_t>(node) + 1]; e++) {
                const NodeEmit& emit = levelData.nodeEmits[static_cast<size_t>(e)];
                std::vector<std::array<int32_t, 3>>& ranges = levelData.batchRanges[static_cast<size_t>(emit.batch)];
                if (ranges.empty()) {
                    touchedBatches.push_back(emit.batch);
                }
                ranges.push_back({ emit.first, emit.count, node });
            }
        }
    }

    bool SceneRenderer::sphereReachesVisible(const View& view, const Vector& center, float radius)
    {
        UModel* model = levelData.model;
        if (!view.visibility || !model || model->nodes.empty()) {
            return true;
        }
        int32_t leaf = pointLeaf(center);
        if (leaf >= 0 && static_cast<size_t>(leaf) < levelData.leafStamp.size() && levelData.leafStamp[static_cast<size_t>(leaf)] == visibleStamp) {
            return true;
        }
        std::vector<int32_t>& stack = scratchNodes;
        stack.clear();
        stack.push_back(0);
        for (int32_t budget = 256; !stack.empty(); budget--) {
            if (budget <= 0) {
                return true;
            }
            int32_t index = stack.back();
            stack.pop_back();
            if (levelData.cullStamp[static_cast<size_t>(index)] == visibleStamp) {
                continue;
            }
            const BspNode& node = model->nodes[static_cast<size_t>(index)];
            float distance = node.plane.x * center.x + node.plane.y * center.y + node.plane.z * center.z - node.plane.w;
            for (int32_t side = 1; side >= 0; side--) {
                if (side ? distance <= -radius : distance >= radius) {
                    continue;
                }
                int32_t child = side ? node.front : node.back;
                if (child > 0) {
                    stack.push_back(child);
                    continue;
                }
                int32_t touched = node.leaf[side];
                if (touched >= 0 && static_cast<size_t>(touched) < levelData.leafStamp.size() && levelData.leafStamp[static_cast<size_t>(touched)] == visibleStamp) {
                    return true;
                }
            }
        }
        return false;
    }

    float SceneRenderer::actorRadius(UObject* actor) const
    {
        float drawScale = std::max(std::abs(actor::DrawScale(actor)), 0.01f);
        UObject* meshObject = actor::Mesh(actor);
        if (actor::DrawType(actor) == DT_Mesh && meshObject) {
            UMesh* mesh = cast<UMesh>(meshObject);
            if (mesh && mesh->meshBounds.valid) {
                Vector extent = (mesh->meshBounds.max - mesh->meshBounds.min) * 0.5f;
                float scale = std::max({ std::abs(mesh->scale.x), std::abs(mesh->scale.y), std::abs(mesh->scale.z) });
                return extent.size() * scale * drawScale + 32.0f;
            }
            return 256.0f * drawScale;
        }
        if (actor::DrawType(actor) == DT_ParticleSystem && particles::isParticleSystem(actor)) {
            return particles::boundsRadius(actor);
        }
        if (actor::DrawType(actor) == DT_Brush) {
            UObject* brushObject = actor::Brush(actor);
            if (brushObject) {
                objects::ensureLoaded(brushObject);
            }
            UModel* brush = brushObject ? cast<UModel>(brushObject) : nullptr;
            if (!brush || !brush->bounds.valid) {
                return 4096.0f;
            }
            Vector scale = scaleOf(brush::MainScale(actor));
            Vector prePivot = actor::PrePivot(actor);
            float radius = 0;
            for (int32_t c = 0; c < 8; c++) {
                Vector corner { (c & 1) ? brush->bounds.max.x : brush->bounds.min.x, (c & 2) ? brush->bounds.max.y : brush->bounds.min.y, (c & 4) ? brush->bounds.max.z : brush->bounds.min.z };
                radius = std::max(radius, ((corner - prePivot) * scale).size());
            }
            return radius + 32.0f;
        }
        return 128.0f * drawScale + 32.0f;
    }

    void SceneRenderer::buildLevel(ULevel* level, const std::vector<UObject*>& actors)
    {
        flushLevel();
        levelData.level = level;
        levelData.model = level->model;
        objects::ensureLoaded(level->model);
        for (UObject* actor : actors) {
            if (actor && actor->isA(nameLevelInfo)) {
                levelData.levelInfo = actor;
                break;
            }
        }
        UModel* model = level->model;
        if (!model) {
            return;
        }

        std::vector<int32_t> surfZone(model->surfs.size(), -1);
        for (const BspNode& node : model->nodes) {
            if (node.surf >= 0 && static_cast<size_t>(node.surf) < surfZone.size() && surfZone[static_cast<size_t>(node.surf)] < 0) {
                surfZone[static_cast<size_t>(node.surf)] = node.zone[1];
            }
        }
        std::vector<Vector> surfAmbient(model->surfs.size());
        for (size_t i = 0; i < model->surfs.size(); i++) {
            surfAmbient[i] = zoneAmbient(zoneActor(static_cast<uint8_t>(std::max(surfZone[i], 0))));
        }
        std::vector<LightmapVariant> variants;
        std::set<std::pair<int32_t, uint8_t>> variantKeys;
        for (const BspNode& node : model->nodes) {
            if (node.surf < 0 || static_cast<size_t>(node.surf) >= model->surfs.size()) {
                continue;
            }
            const BspSurf& surf = model->surfs[static_cast<size_t>(node.surf)];
            for (int side = 1; side >= 0; side--) {
                if (side == 0 && !(surf.polyFlags & (PF_TwoSided | PF_Portal))) {
                    continue;
                }
                uint8_t zone = node.zone[side];
                Vector ambient = zoneAmbient(zoneActor(zone));
                if (sameVector(ambient, surfAmbient[static_cast<size_t>(node.surf)])) {
                    continue;
                }
                if (variantKeys.insert({ node.surf, zone }).second) {
                    variants.push_back({ node.surf, zone, ambient });
                }
            }
        }
        lightmaps.build(model, surfAmbient, static_cast<float>(time), {}, nullptr, &variants);
        levelData.fogFrame.assign(model->surfs.size(), 0);
        levelData.fogCache.assign(model->surfs.size(), {});
        levelData.fogged.assign(model->surfs.size(), 0);
        levelData.surfNodes.assign(model->surfs.size(), {});
        for (size_t i = 0; i < model->nodes.size(); i++) {
            int32_t surf = model->nodes[i].surf;
            if (surf >= 0 && static_cast<size_t>(surf) < model->surfs.size()) {
                levelData.surfNodes[static_cast<size_t>(surf)].push_back(static_cast<int32_t>(i));
            }
        }
        levelData.surfFlags.assign(model->surfs.size(), 0);
        for (size_t i = 0; i < model->surfs.size(); i++) {
            UTexture* texture = asTexture(model->surfs[i].texture);
            if (!texture) {
                texture = levelData.levelInfo ? asTexture(levelinfo::DefaultTexture(levelData.levelInfo)) : nullptr;
            }
            levelData.surfFlags[i] = model->surfs[i].polyFlags | textureFlags(texture);
        }

        struct Key {
            UTexture* texture;
            uint32_t flags;
            int32_t page;
            uint8_t zone;

            bool operator<(const Key& o) const
            {
                if (texture != o.texture) {
                    return texture < o.texture;
                }
                if (flags != o.flags) {
                    return flags < o.flags;
                }
                if (page != o.page) {
                    return page < o.page;
                }
                return zone < o.zone;
            }
        };

        std::map<Key, std::vector<GLVertex>> groups;
        std::map<UTexture*, std::pair<float, float>> texelSizes;
        std::map<Key, std::vector<GLVertex>> detailGroups;
        std::map<std::pair<int32_t, uint8_t>, std::vector<GLVertex>> mirrorGroups;
        std::vector<std::pair<int32_t, std::pair<int32_t, uint8_t>>> mirrorNodes;
        std::map<Key, std::vector<std::array<int32_t, 3>>> detailSizes;
        std::map<Key, std::vector<std::array<int32_t, 6>>> surfRanges;
        std::map<Key, std::vector<std::array<int32_t, 3>>> nodeRanges;
        for (const BspNode& node : model->nodes) {
            if (node.numVertices < 3 || node.surf < 0 || static_cast<size_t>(node.surf) >= model->surfs.size()) {
                continue;
            }
            const BspSurf& surf = model->surfs[static_cast<size_t>(node.surf)];
            if (surf.polyFlags & PF_Invisible) {
                continue;
            }
            if (surf.polyFlags & PF_FakeBackdrop) {
                levelData.hasBackdrop = true;
            }
            UTexture* texture = asTexture(surf.texture);
            if (!texture) {
                texture = levelData.levelInfo ? asTexture(levelinfo::DefaultTexture(levelData.levelInfo)) : nullptr;
            }
            float texWidth = texture && texture->width() ? static_cast<float>(texture->width()) : 64.0f;
            float texHeight = texture && texture->height() ? static_cast<float>(texture->height()) : 64.0f;
            if (texture && ProceduralTextures::isProcedural(texture)) {
                texWidth = static_cast<float>(std::max(bitmap::UClamp(texture), 1));
                texHeight = static_cast<float>(std::max(bitmap::VClamp(texture), 1));
                if (!texture->mips.empty() && texture->mips[0].width > 0) {
                    texWidth = static_cast<float>(texture->mips[0].width);
                    texHeight = static_cast<float>(texture->mips[0].height);
                }
            }
            float drawScale = texture && texture->data ? texture::DrawScale(texture) : 1.0f;
            if (drawScale <= 0) {
                drawScale = 1;
            }
            texelSizes[texture] = { texWidth * drawScale, texHeight * drawScale };
            const Vector& base = model->points.at(static_cast<size_t>(surf.pBase));
            const Vector& axisU = model->vectors.at(static_cast<size_t>(surf.vTextureU));
            const Vector& axisV = model->vectors.at(static_cast<size_t>(surf.vTextureV));
            Vector surfaceNormal = model->vectors.at(static_cast<size_t>(surf.vNormal)).normal();
            uint32_t combinedFlags = surf.polyFlags | textureFlags(texture);
            bool unlit = (combinedFlags & (PF_Unlit | PF_Modulated)) != 0;
            auto emit = [&](uint8_t zone, const Vector& sideAmbient, uint32_t flagMask, bool reversed) {
                int32_t page = -1;
                std::vector<GLVertex> polygon;
                ZoneFog fog = (combinedFlags & (PF_Translucent | PF_Modulated | PF_AlphaBlend)) ? ZoneFog {} : zoneDistanceFog(zoneActor(zone));
                for (uint8_t i = 0; i < node.numVertices; i++) {
                    const BspVert& vert = model->verts.at(static_cast<size_t>(node.vertPool) + i);
                    const Vector& point = model->points.at(static_cast<size_t>(vert.vertex));
                    GLVertex vertex;
                    vertex.x = point.x;
                    vertex.y = point.y;
                    vertex.z = point.z;
                    vertex.nx = surfaceNormal.x;
                    vertex.ny = surfaceNormal.y;
                    vertex.nz = surfaceNormal.z;
                    vertex.u = ((point - base).dot(axisU) + static_cast<float>(surf.panU)) / (texWidth * drawScale);
                    vertex.v = ((point - base).dot(axisV) + static_cast<float>(surf.panV)) / (texHeight * drawScale);
                    applyZoneFog(vertex, fog);
                    if (!unlit) {
                        int32_t vertexPage = -1;
                        if (lightmaps.texCoord(model, surf, point, vertex.lu, vertex.lv, vertexPage, zone)) {
                            page = vertexPage;
                        } else {
                            const Vector& zoneLight = sideAmbient;
                            Vector ambient = Vector { std::floor(zoneLight.x * 64.0f), std::floor(zoneLight.y * 64.0f), std::floor(zoneLight.z * 64.0f) } * (2.0f / 127.5f);
                            vertex.r = std::min(ambient.x, 1.0f);
                            vertex.g = std::min(ambient.y, 1.0f);
                            vertex.b = std::min(ambient.z, 1.0f);
                        }
                    }
                    polygon.push_back(vertex);
                }
                if (reversed) {
                    std::reverse(polygon.begin(), polygon.end());
                }
                Key key { texture, (surf.polyFlags | textureFlags(texture)) & flagMask, page, zone };
                if ((surf.polyFlags & PF_Mirrored) && !reversed) {
                    std::vector<GLVertex>& mirrorGroup = mirrorGroups[{ node.surf, zone }];
                    mirrorNodes.push_back({ static_cast<int32_t>(&node - model->nodes.data()), { node.surf, zone } });
                    for (size_t i = 1; i + 1 < polygon.size(); i++) {
                        mirrorGroup.push_back(polygon[0]);
                        mirrorGroup.push_back(polygon[i]);
                        mirrorGroup.push_back(polygon[i + 1]);
                    }
                }
                std::vector<GLVertex>& group = groups[key];
                if (polygon.size() >= 3) {
                    surfRanges[key].push_back({ static_cast<int32_t>(group.size()), static_cast<int32_t>((polygon.size() - 2) * 3), node.surf, node.zone[0], node.zone[1], static_cast<int32_t>(&node - model->nodes.data()) });
                    nodeRanges[key].push_back({ static_cast<int32_t>(group.size()), static_cast<int32_t>((polygon.size() - 2) * 3), static_cast<int32_t>(&node - model->nodes.data()) });
                }
                for (size_t i = 1; i + 1 < polygon.size(); i++) {
                    group.push_back(polygon[0]);
                    group.push_back(polygon[i]);
                    group.push_back(polygon[i + 1]);
                }
                UTexture* detail = texture && texture->data && !(surf.polyFlags & PF_Portal) ? asTexture(texture::DetailTexture(texture)) : nullptr;
                if (detail && polygon.size() >= 3) {
                    float detailScale = detail->data && texture::DrawScale(detail) > 0 ? texture::DrawScale(detail) : 1.0f;
                    float detailWidth = static_cast<float>(std::max(detail->width(), 1u)) * detailScale;
                    float detailHeight = static_cast<float>(std::max(detail->height(), 1u)) * detailScale;
                    std::vector<GLVertex>& detailGroup = detailGroups[key];
                    for (size_t i = 1; i + 1 < polygon.size(); i++) {
                        for (size_t corner : { size_t { 0 }, i, i + 1 }) {
                            GLVertex vertex = polygon[corner];
                            Vector point { vertex.x, vertex.y, vertex.z };
                            vertex.u = (point - base).dot(axisU) / detailWidth;
                            vertex.v = (point - base).dot(axisV) / detailHeight;
                            detailGroup.push_back(vertex);
                        }
                    }
                    detailSizes[key].push_back({ static_cast<int32_t>((polygon.size() - 2) * 3), node.surf, static_cast<int32_t>(&node - model->nodes.data()) });
                }
            };
            uint8_t frontZone = node.zone[1];
            uint8_t backZone = node.zone[0];
            Vector frontAmbient = zoneAmbient(zoneActor(frontZone));
            Vector backAmbient = zoneAmbient(zoneActor(backZone));
            if ((surf.polyFlags & (PF_TwoSided | PF_Portal)) && !unlit && !sameVector(frontAmbient, backAmbient)) {
                emit(frontZone, frontAmbient, ~PF_TwoSided, false);
                emit(backZone, backAmbient, ~PF_TwoSided, true);
            } else {
                emit(frontZone, frontAmbient, ~0u, false);
            }
        }

        std::vector<GLVertex> all;
        std::vector<GLVertex> detailVertices;
        for (auto& [key, vertices] : groups) {
            Batch batch;
            batch.texture = key.texture;
            batch.polyFlags = key.flags;
            batch.lightmapPage = key.page;
            batch.zone = key.zone;
            auto isFogZone = [&](int32_t zone) {
                UObject* zoneObject = zoneActor(static_cast<uint8_t>(zone));
                return zoneObject && zoneObject->data && zoneObject->isA(nameZoneInfo) && zoneinfo::bFogZone(zoneObject);
            };
            for (std::array<int32_t, 6> range : surfRanges[key]) {
                if (isFogZone(range[3]) || isFogZone(range[4])) {
                    batch.fogZone = true;
                }
            }
            if (batch.fogZone) {
                for (std::array<int32_t, 6> range : surfRanges[key]) {
                    range[0] += static_cast<int32_t>(all.size());
                    range[3] = isFogZone(range[3]) ? 1 : 0;
                    range[4] = isFogZone(range[4]) ? 1 : 0;
                    batch.surfPolys.push_back(range);
                }
            }
            for (std::array<int32_t, 3> range : nodeRanges[key]) {
                range[0] += static_cast<int32_t>(all.size());
                batch.nodePolys.push_back(range);
            }
            batch.uSize = texelSizes[key.texture].first;
            batch.vSize = texelSizes[key.texture].second;
            auto detailGroup = detailGroups.find(key);
            if (detailGroup != detailGroups.end()) {
                batch.detail = asTexture(texture::DetailTexture(key.texture));
                int32_t at = static_cast<int32_t>(detailVertices.size());
                for (const auto& [size, surfIndex, node] : detailSizes[key]) {
                    batch.detailPolys.push_back({ at, size, surfIndex, node });
                    at += size;
                }
                for (const GLVertex& vertex : detailGroup->second) {
                    levelData.detailPoints.push_back({ vertex.x, vertex.y, vertex.z });
                }
                detailVertices.insert(detailVertices.end(), detailGroup->second.begin(), detailGroup->second.end());
            }
            batch.first = static_cast<int32_t>(all.size());
            batch.count = static_cast<int32_t>(vertices.size());
            batch.boundsMin = { vertices[0].x, vertices[0].y, vertices[0].z };
            batch.boundsMax = batch.boundsMin;
            for (const GLVertex& vertex : vertices) {
                batch.boundsMin = { std::min(batch.boundsMin.x, vertex.x), std::min(batch.boundsMin.y, vertex.y), std::min(batch.boundsMin.z, vertex.z) };
                batch.boundsMax = { std::max(batch.boundsMax.x, vertex.x), std::max(batch.boundsMax.y, vertex.y), std::max(batch.boundsMax.z, vertex.z) };
            }
            all.insert(all.end(), vertices.begin(), vertices.end());
            levelData.batches.push_back(batch);
        }
        if (!all.empty()) {
            levelData.vao = device->createStatic(all);
        }
        if (!detailVertices.empty()) {
            levelData.detailVao = device->createStatic(detailVertices);
        }
        levelData.nodeEmitStart.assign(model->nodes.size() + 1, 0);
        for (const Batch& batch : levelData.batches) {
            for (const auto& [first, count, node] : batch.nodePolys) {
                levelData.nodeEmitStart[static_cast<size_t>(node) + 1]++;
            }
        }
        for (size_t i = 0; i < model->nodes.size(); i++) {
            levelData.nodeEmitStart[i + 1] += levelData.nodeEmitStart[i];
        }
        levelData.nodeEmits.resize(static_cast<size_t>(levelData.nodeEmitStart.back()));
        std::vector<int32_t> emitFill(levelData.nodeEmitStart.begin(), levelData.nodeEmitStart.end() - 1);
        for (size_t b = 0; b < levelData.batches.size(); b++) {
            for (const auto& [first, count, node] : levelData.batches[b].nodePolys) {
                levelData.nodeEmits[static_cast<size_t>(emitFill[static_cast<size_t>(node)]++)] = { static_cast<int32_t>(b), first, count };
            }
        }
        levelData.nodeStamp.assign(model->nodes.size(), 0);
        levelData.surfStamp.assign(model->surfs.size(), 0);
        levelData.leafStamp.assign(model->leaves.size(), 0);
        levelData.nodeVolumetrics.assign(model->nodes.size(), 0);
        levelData.leafVolumetrics.assign(model->leaves.size(), 0);
        for (const ConvexLeaf& leaf : model->leaves) {
            levelData.anyVolumetric = levelData.anyVolumetric || leaf.volumetric >= 0;
        }
        levelData.cullStamp.assign(model->nodes.size(), 0);
        levelData.cullOrder.assign(model->nodes.size(), 0);
        nodeOrder.assign(model->nodes.size(), 0);
        leafOrder.assign(model->leaves.size(), 0);
        levelData.batchRanges.assign(levelData.batches.size(), {});
        levelData.nodeMirror.assign(model->nodes.size(), -1);
        std::map<std::pair<int32_t, uint8_t>, int32_t> mirrorIndex;
        std::vector<GLVertex> mirrorVertices;
        for (const auto& [key, vertices] : mirrorGroups) {
            mirrorIndex[key] = static_cast<int32_t>(levelData.mirrors.size());
            const BspSurf& surf = model->surfs[static_cast<size_t>(key.first)];
            MirrorSurface mirror;
            mirror.normal = model->vectors.at(static_cast<size_t>(surf.vNormal)).normal();
            mirror.distance = mirror.normal.dot(model->points.at(static_cast<size_t>(surf.pBase)));
            mirror.zone = key.second;
            mirror.polyFlags = surf.polyFlags | textureFlags(asTexture(surf.texture));
            mirror.first = static_cast<int32_t>(mirrorVertices.size());
            mirror.count = static_cast<int32_t>(vertices.size());
            Vector tangent = std::abs(mirror.normal.z) < 0.9f ? Vector { 0, 0, 1 }.cross(mirror.normal).normal() : Vector { 1, 0, 0 }.cross(mirror.normal).normal();
            Vector bitangent = mirror.normal.cross(tangent);
            std::vector<std::pair<std::pair<float, float>, Vector>> projected;
            for (const GLVertex& vertex : vertices) {
                Vector point { vertex.x, vertex.y, vertex.z };
                projected.push_back({ { point.dot(tangent), point.dot(bitangent) }, point });
            }
            std::sort(projected.begin(), projected.end(), [](const auto& a, const auto& b) {
                return a.first < b.first;
            });
            auto turn = [](const std::pair<float, float>& o, const std::pair<float, float>& a, const std::pair<float, float>& b) {
                return (a.first - o.first) * (b.second - o.second) - (a.second - o.second) * (b.first - o.first);
            };
            std::vector<size_t> chain;
            for (int pass = 0; pass < 2; pass++) {
                size_t start = chain.size();
                for (size_t k = 0; k < projected.size(); k++) {
                    size_t i = pass == 0 ? k : projected.size() - 1 - k;
                    while (chain.size() >= start + 2 && turn(projected[chain[chain.size() - 2]].first, projected[chain.back()].first, projected[i].first) <= 0.01f) {
                        chain.pop_back();
                    }
                    chain.push_back(i);
                }
                chain.pop_back();
            }
            for (size_t i : chain) {
                mirror.hull.push_back(projected[i].second);
            }
            mirrorVertices.insert(mirrorVertices.end(), vertices.begin(), vertices.end());
            levelData.mirrors.push_back(mirror);
        }
        for (const auto& [node, key] : mirrorNodes) {
            levelData.nodeMirror[static_cast<size_t>(node)] = mirrorIndex[key];
        }
        if (!mirrorVertices.empty()) {
            levelData.mirrorVao = device->createStatic(mirrorVertices);
        }
    }

    void SceneRenderer::applyBlend(uint32_t polyFlags, int32_t extraFlags)
    {
        int32_t flags = SHADER_Textured | extraFlags;
        if (polyFlags & PF_Translucent) {
            device->setBlend(BlendMode::Translucent);
        } else if (polyFlags & PF_Modulated) {
            device->setBlend(BlendMode::Modulated);
        } else if (polyFlags & PF_Masked) {
            device->setBlend(BlendMode::Masked);
            flags |= SHADER_AlphaTest;
        } else {
            device->setBlend(BlendMode::Opaque);
        }
        device->setFlags(flags);
        if (polyFlags & PF_TwoSided) {
            device->setCulling(false);
        } else {
            device->setCulling(true);
        }
    }

    void SceneRenderer::drawLevelGeometry(const View& view, bool translucentPass)
    {
        if (!levelData.vao) {
            return;
        }
        device->useShader();
        device->setViewProj(view.viewProj);
        device->setEye(view.location, view.axes.xAxis);
        device->setTint(1, 1, 1, 1);
        uint32_t uploadedLights = ~0u;
        device->setDepthTest(true);
        device->setDepthFunc(DepthFunc::LessEqual);
        device->setFrontFace(!view.mirrored);
        device->bindStatic(levelData.vao);
        if (!translucentPass) {
            gatherVisible(view);
            lightmaps.animate(levelData.model, static_cast<float>(time), static_cast<uint64_t>(lightFrameCount), [&](size_t surf, uint64_t zones) {
                return (view.zoneMask & zones) && levelData.surfStamp[surf] == visibleStamp;
            });
        }
        bool skyActive = levelData.hasBackdrop;
        bool mirrorsActive = view.recursion < MaxFrameRecursion - 1;
        if (translucentPass) {
            std::vector<TranslucentSurface>& surfaces = scratchTranslucentSurfaces;
            surfaces.clear();
            for (size_t index = 0; index < levelData.batches.size(); index++) {
                const Batch& batch = levelData.batches[index];
                std::vector<std::array<int32_t, 3>>& ranges = levelData.batchRanges[index];
                if (ranges.empty() || !isTranslucentFlags(batch.polyFlags) || !(view.zoneMask & (1ull << (batch.zone & 63)))) {
                    continue;
                }
                if ((batch.polyFlags & PF_FakeBackdrop) && skyActive) {
                    continue;
                }
                std::sort(ranges.begin(), ranges.end());
                for (const auto& [first, count, node] : ranges) {
                    surfaces.push_back({ nodeOrder[static_cast<size_t>(node)], &batch, first, count, node });
                }
            }
            std::stable_sort(surfaces.begin(), surfaces.end(), [](const TranslucentSurface& a, const TranslucentSurface& b) {
                return a.order < b.order;
            });
            for (const TranslucentSurface& surface : surfaces) {
                drawBatch(*surface.batch, view, &surface.first, &surface.count, 1, surface.node, uploadedLights);
            }
            device->setPan(0, 0);
            device->setScale(1, 1);
            device->setBlend(BlendMode::Opaque);
            return;
        }
        auto collectRanges = [&](int32_t index) {
            std::vector<std::array<int32_t, 3>>& ranges = levelData.batchRanges[static_cast<size_t>(index)];
            std::sort(ranges.begin(), ranges.end());
            scratchFirsts.clear();
            scratchCounts.clear();
            for (const auto& [first, count, node] : ranges) {
                if (!scratchFirsts.empty() && scratchFirsts.back() + scratchCounts.back() == first) {
                    scratchCounts.back() += count;
                } else {
                    scratchFirsts.push_back(first);
                    scratchCounts.push_back(count);
                }
            }
        };
        if (mirrorsActive && !levelData.mirrors.empty()) {
            device->setBlend(BlendMode::Opaque);
            device->setFlags(0);
            device->setColorWrite(false);
            for (int32_t index : touchedBatches) {
                const Batch& batch = levelData.batches[static_cast<size_t>(index)];
                if (!(batch.polyFlags & PF_Mirrored) || !(view.zoneMask & (1ull << (batch.zone & 63)))) {
                    continue;
                }
                if (batch.polyFlags & PF_TwoSided) {
                    device->setCulling(false);
                } else {
                    device->setCulling(true);
                }
                collectRanges(index);
                device->drawStaticRanges(levelData.vao, scratchFirsts.data(), scratchCounts.data(), static_cast<int32_t>(scratchFirsts.size()));
            }
            device->setColorWrite(true);
        }
        for (int32_t index : touchedBatches) {
            const Batch& batch = levelData.batches[static_cast<size_t>(index)];
            if (isTranslucentFlags(batch.polyFlags) || !(view.zoneMask & (1ull << (batch.zone & 63)))) {
                continue;
            }
            if (mirrorsActive && (batch.polyFlags & PF_Mirrored)) {
                continue;
            }
            collectRanges(index);
            drawBatch(batch, view, scratchFirsts.data(), scratchCounts.data(), static_cast<int32_t>(scratchFirsts.size()), -1, uploadedLights);
        }
        device->setPan(0, 0);
        device->setScale(1, 1);
        device->setBlend(BlendMode::Opaque);
    }

    void SceneRenderer::drawBatch(const Batch& batch, const View& view, const int32_t* firsts, const int32_t* counts, int32_t ranges, int32_t onlyNode, uint32_t& uploadedLights)
    {
        bool backdropMask = (batch.polyFlags & PF_FakeBackdrop) && levelData.hasBackdrop;
        UTexture* texture = device->animated(batch.texture);
        const GLTextureInfo* info = device->texture(texture);
        uint32_t textureId = info ? info->id : device->whiteTexture();
        int32_t extra = SHADER_Surface;
        bool batchUnlit = (batch.polyFlags & (PF_Unlit | PF_Modulated)) != 0;
        uint32_t lightMask = batchUnlit ? 0u : litMask(batch.boundsMin, batch.boundsMax, (batch.polyFlags & PF_SpecialLit) != 0);
        if (lightMask) {
            extra |= SHADER_Dynamic;
            if (lightMask != uploadedLights) {
                batchLights.clear();
                for (size_t i = 0; i < dynamicLights.size(); i++) {
                    if (lightMask & (1u << i)) {
                        batchLights.push_back(dynamicLights[i]);
                    }
                }
                device->setDynamicLights(batchLights);
                uploadedLights = lightMask;
            }
        }
        if (batch.lightmapPage >= 0) {
            extra |= SHADER_Lightmap;
            device->bindLightmap(lightmaps.texture(batch.lightmapPage));
        }
        applyBlend(batch.polyFlags, extra);
        float panU = 0;
        float panV = 0;
        float levelTime = static_cast<float>(time);
        if (batch.polyFlags & (PF_AutoUPan | PF_AutoVPan)) {
            UObject* zone = zoneActor(batch.zone);
            if (zone && zone->isA(nameZoneInfo)) {
                if (batch.polyFlags & PF_AutoUPan) {
                    panU += autoPanTexels(levelTime, zoneinfo::TexUPanSpeed(zone));
                }
                if (batch.polyFlags & PF_AutoVPan) {
                    panV += autoPanTexels(levelTime, zoneinfo::TexVPanSpeed(zone));
                }
            }
        }
        device->setPan(panU / batch.uSize, panV / batch.vSize);
        device->bindTexture(textureId);
        if (backdropMask) {
            device->setBlend(BlendMode::Opaque);
            device->setFlags(0);
            device->setColorWrite(false);
            device->drawStaticRanges(levelData.vao, firsts, counts, ranges);
            device->setColorWrite(true);
            return;
        }
        bool sheet = (batch.polyFlags & PF_NotSolid) && !(batch.polyFlags & PF_Portal);
        if (sheet) {
            device->setPolygonOffset(true, -2.0f, -2.0f);
        }
        if ((batch.polyFlags & PF_Translucent) && batch.lightmapPage >= 0 && !device->lightsTranslucencyInOnePass()) {
            applyBlend(batch.polyFlags, 0);
            device->drawStaticRanges(levelData.vao, firsts, counts, ranges);
            device->setBlend(BlendMode::Modulated);
            device->setFlags(extra);
            device->setTint(0.5f, 0.5f, 0.5f, 1);
            device->drawStaticRanges(levelData.vao, firsts, counts, ranges);
            device->setTint(1, 1, 1, 1);
        } else {
            device->drawStaticRanges(levelData.vao, firsts, counts, ranges);
        }
        device->setScale(1, 1);
        drawFog(batch, view, onlyNode);
        drawDetail(batch, view, onlyNode);
        if (sheet) {
            device->setPolygonOffset(false, 0.0f, 0.0f);
        }
    }

    void SceneRenderer::applyClip(const View& view)
    {
        device->setClipPlanes(&view.clipPlanes[0].x, view.clipCount);
    }

    void SceneRenderer::drawFrameContent(const View& view, const std::vector<UObject*>& actors, UObject* viewActor, bool occlude)
    {
        device->useShader();
        applyClip(view);
        drawLevelGeometry(view, false);
        drawActors(actors, view, viewActor, occlude);
        flushTranslucent(view);
        drawLevelGeometry(view, true);
        if (view.clipCount) {
            View plain;
            applyClip(plain);
        }
    }

    void SceneRenderer::drawSky(const View& frame, const std::vector<UObject*>& actors, UObject* viewActor)
    {
        UObject* skyZone = frameSkyZone(zoneActor(pointZone(frame.location)));
        if (!skyZone) {
            levelData.hasBackdrop = false;
            return;
        }
        if (!frame.visibility || frame.visibility->backdropSpans.empty()) {
            return;
        }
        Coords skyAxes = rotationAxes(actor::Rotation(skyZone));
        Coords axes;
        axes.xAxis = untransformVector(skyAxes, frame.axes.xAxis);
        axes.yAxis = untransformVector(skyAxes, frame.axes.yAxis);
        axes.zAxis = untransformVector(skyAxes, frame.axes.zAxis);
        View sky = makeView(actor::Location(skyZone), axes, frame.fov, sceneNearPlane);
        uint8_t skyIndex = pointZone(actor::Location(skyZone));
        sky.zoneMask = skyIndex != 0 ? (1ull << (skyIndex & 63)) : ~0ull;
        sky.mirrored = frame.mirrored;
        sky.recursion = frame.recursion + 1;
        occludeView(sky, &frame.visibility->backdropSpans);
        View plain;
        applyClip(plain);
        drawLevelGeometry(sky, false);
        drawActors(actors, sky, viewActor, false);
        flushTranslucent(sky);
        drawLevelGeometry(sky, true);
    }

    void SceneRenderer::markMirror(const View& view, const MirrorSurface& mirror)
    {
        device->useShader();
        device->setViewProj(view.viewProj);
        device->setEye(view.location, view.axes.xAxis);
        applyClip(view);
        device->setBlend(BlendMode::Opaque);
        device->setFlags(0);
        device->setDepthWrite(true);
        device->clear(false, true, 0.0f);
        device->setDepthTest(true);
        device->setDepthFunc(DepthFunc::Always);
        device->setDepthRange(1.0f, 1.0f);
        device->setCulling(false);
        device->setColorWrite(false);
        device->drawStatic(levelData.mirrorVao, mirror.first, mirror.count);
        device->setColorWrite(true);
        device->setDepthRange(0.0f, 1.0f);
        device->setDepthFunc(DepthFunc::LessEqual);
    }

    void SceneRenderer::drawMirrors(const View& view, const std::vector<UObject*>& actors)
    {
        if (view.recursion >= MaxFrameRecursion - 1 || !levelData.mirrorVao) {
            return;
        }
        for (size_t mirrorIndex = 0; mirrorIndex < levelData.mirrors.size(); mirrorIndex++) {
            const MirrorSurface& mirror = levelData.mirrors[mirrorIndex];
            const std::vector<ScreenSpan>& open = view.visibility->mirrorSpans[mirrorIndex];
            if (open.empty() || !(view.zoneMask & (1ull << (mirror.zone & 63)))) {
                continue;
            }
            float side = mirror.normal.dot(view.location) - mirror.distance;
            if (side < -1.0f && !(mirror.polyFlags & PF_TwoSided)) {
                continue;
            }
            Vector normal = side >= 0 ? mirror.normal : -mirror.normal;
            float distance = side >= 0 ? mirror.distance : -mirror.distance;
            auto reflect = [&](const Vector& direction) {
                return direction - normal * (2.0f * normal.dot(direction));
            };
            auto reflectPoint = [&](const Vector& point) {
                return point - normal * (2.0f * (normal.dot(point) - distance));
            };
            Coords axes;
            axes.xAxis = reflect(view.axes.xAxis);
            axes.yAxis = reflect(view.axes.yAxis);
            axes.zAxis = reflect(view.axes.zAxis);
            View child = makeView(reflectPoint(view.location), axes, view.fov, sceneNearPlane);
            child.zoneMask = visibleZones(mirror.zone);
            child.recursion = view.recursion + 1;
            child.mirrored = !view.mirrored;
            child.fog = view.fog;
            child.clipPlanes[0] = { normal.x, normal.y, normal.z, -distance };
            child.clipCount = 1;
            auto pyramid = [&](const std::vector<Vector>& polygon) {
                std::vector<Plane> planes;
                if (polygon.size() < 3) {
                    return planes;
                }
                Vector middle;
                for (const Vector& point : polygon) {
                    middle += point;
                }
                middle = middle / static_cast<float>(polygon.size());
                for (size_t i = 0; i < polygon.size(); i++) {
                    Vector a = polygon[i] - child.location;
                    Vector b = polygon[(i + 1) % polygon.size()] - child.location;
                    Vector edgeNormal = a.cross(b).normal();
                    if (edgeNormal.dot(middle - child.location) < 0) {
                        edgeNormal = -edgeNormal;
                    }
                    planes.push_back({ edgeNormal.x, edgeNormal.y, edgeNormal.z, -edgeNormal.dot(child.location) });
                }
                return planes;
            };
            if (!view.portal.empty()) {
                std::vector<Vector> inherited;
                for (const Vector& point : view.portal) {
                    inherited.push_back(reflectPoint(point));
                }
                std::vector<Plane> planes = pyramid(inherited);
                if (child.clipCount + static_cast<int32_t>(planes.size()) <= 8) {
                    for (const Plane& plane : planes) {
                        child.clipPlanes[static_cast<size_t>(child.clipCount++)] = plane;
                    }
                }
            }
            child.cullPlanes = pyramid(mirror.hull);
            child.cullPlanes.push_back(child.clipPlanes[0]);
            child.portal = mirror.hull;
            occludeView(child, &open);
            drawMirrors(child, actors);
            if (levelData.hasBackdrop) {
                markMirror(view, mirror);
                drawSky(child, actors, nullptr);
            }
            markMirror(view, mirror);
            if (child.fog) {
                collectVolumetrics(child);
            }
            drawFrameContent(child, actors, nullptr, false);
        }
    }

    void SceneRenderer::collectVolumetrics(const View& view)
    {
        volumetrics.clear();
        volumetricPalettes.clear();
        volumetricColors.clear();
        fogStamp++;
        volumetricSignature = 1469598103934665603ull;
        auto mix = [&](float value) {
            uint32_t bits = std::bit_cast<uint32_t>(value);
            volumetricSignature = (volumetricSignature ^ bits) * 1099511628211ull;
        };
        std::vector<UObject*> found;
        auto outside = [&](const Vector& location, float radius) {
            return sphereOutsideView(view, location, radius);
        };
        auto add = [&](UObject* light) {
            float brightness = 0;
            Vector color = lightColor(light, static_cast<float>(time), &brightness);
            volumetrics.push_back(prepareVolumetric(actor::Location(light) - view.location, actor::VolumeRadius(light), brightness, actor::VolumeBrightness(light)));
            volumetricPalettes.push_back({ static_cast<int64_t>(std::floor(color.x * 65536.0f)), static_cast<int64_t>(std::floor(color.y * 65536.0f)), static_cast<int64_t>(std::floor(color.z * 65536.0f)) });
            volumetricColors.push_back(color);
            const Vector& location = actor::Location(light);
            mix(location.x);
            mix(location.y);
            mix(location.z);
            mix(volumetrics.back().radius);
            mix(volumetrics.back().brightness);
            mix(color.x);
            mix(color.y);
            mix(color.z);
            found.push_back(light);
        };
        std::vector<UObject*> dynamicVolumetrics;
        std::vector<int32_t> dynamicLeaves;
        for (UObject* light : lights) {
            if (((actor::bStatic(light) || actor::bNoDelete(light)) && !actor::bDynamicLight(light)) || actor::LightRadius(light) == 0) {
                continue;
            }
            if (!actor::VolumeRadius(light) || !actor::VolumeBrightness(light)) {
                continue;
            }
            UObject* zone = zoneForActor(light);
            if (!zone || !zone->data || !zone->isA(nameZoneInfo) || !zoneinfo::bFogZone(zone)) {
                continue;
            }
            float radius = 25.0f * (static_cast<float>(actor::VolumeRadius(light)) + 1.0f);
            if (outside(actor::Location(light), radius) || outside(actor::Location(light), std::max(radius, lightRadius(light)))) {
                continue;
            }
            dynamicVolumetrics.push_back(light);
            dynamicLeaves.push_back(pointLeaf(actor::Location(light)));
        }
        UModel* model = levelData.model;
        if (!model || model->nodes.empty() || !view.visibility || (!levelData.anyVolumetric && dynamicVolumetrics.empty())) {
            return;
        }
        auto visitLeaf = [&](int32_t leafIndex) {
            if (leafIndex < 0 || static_cast<size_t>(leafIndex) >= model->leaves.size()) {
                return;
            }
            const ConvexLeaf& leaf = model->leaves[static_cast<size_t>(leafIndex)];
            if (!(view.zoneMask & (1ull << (leaf.zone & 63)))) {
                return;
            }
            if (leaf.volumetric >= 0) {
                for (size_t i = static_cast<size_t>(leaf.volumetric); i < model->lights.size() && model->lights[i]; i++) {
                    UObject* light = model->lights[i];
                    if (!light->data || light->destroyed || std::find(found.begin(), found.end(), light) != found.end()) {
                        continue;
                    }
                    float radius = 25.0f * (static_cast<float>(actor::VolumeRadius(light)) + 1.0f);
                    if ((actor::Location(light) - view.location).sizeSquared() > radius * radius && outside(actor::Location(light), radius)) {
                        continue;
                    }
                    add(light);
                }
            }
            for (size_t i = 0; i < dynamicVolumetrics.size(); i++) {
                if (dynamicLeaves[i] == leafIndex && std::find(found.begin(), found.end(), dynamicVolumetrics[i]) == found.end()) {
                    add(dynamicVolumetrics[i]);
                }
            }
            levelData.leafVolumetrics[static_cast<size_t>(leafIndex)] = static_cast<int32_t>(volumetrics.size());
        };
        const ViewVisibility& visibility = *view.visibility;
        size_t nextLeaf = 0;
        for (size_t i = 0; i <= visibility.nodes.size(); i++) {
            int32_t sequence = i < visibility.nodes.size() ? visibility.nodeSequence[i] : std::numeric_limits<int32_t>::max();
            while (nextLeaf < visibility.leaves.size() && visibility.leaves[nextLeaf].second < sequence) {
                visitLeaf(visibility.leaves[nextLeaf++].first);
            }
            if (i < visibility.nodes.size()) {
                levelData.nodeVolumetrics[static_cast<size_t>(visibility.nodes[i])] = static_cast<int32_t>(volumetrics.size());
            }
        }
        mix(static_cast<float>(volumetrics.size()));
    }

    Vector SceneRenderer::meshFog(const Vector& point, const View& view, size_t count) const
    {
        Vector fog;
        for (size_t i = 0; i < count; i++) {
            float value = 2.0f * volumetricFog(volumetrics[i], point - view.location);
            if (i > 0 && value == 0) {
                continue;
            }
            Vector local = volumetricColors[i] * value;
            local = { std::min(local.x, 1.0f), std::min(local.y, 1.0f), std::min(local.z, 1.0f) };
            if (i == 0) {
                fog = local;
            } else {
                fog = fog + local - local * fog;
            }
        }
        return fog;
    }

    bool SceneRenderer::surfaceFog(int32_t surfIndex, const View& view)
    {
        size_t index = static_cast<size_t>(surfIndex);
        if (levelData.fogFrame[index] == fogStamp) {
            return levelData.fogged[index] != 0;
        }
        levelData.fogFrame[index] = fogStamp;
        levelData.fogged[index] = 0;
        FogCache& cache = levelData.fogCache[index];
        int32_t count = 0;
        for (int32_t node : levelData.surfNodes[index]) {
            if (levelData.nodeStamp[static_cast<size_t>(node)] == visibleStamp) {
                count = std::max(count, levelData.nodeVolumetrics[static_cast<size_t>(node)]);
            }
        }
        if (cache.valid && cache.signature == volumetricSignature && cache.count == count && sameVector(cache.eye, view.location)) {
            levelData.fogged[index] = cache.fogged ? 1 : 0;
            return cache.fogged;
        }
        cache.valid = true;
        cache.fogged = false;
        cache.signature = volumetricSignature;
        cache.eye = view.location;
        cache.count = count;
        UModel* model = levelData.model;
        const BspSurf& surf = model->surfs[index];
        if (surf.lightMap < 0 || static_cast<size_t>(surf.lightMap) >= model->lightMaps.size()) {
            return false;
        }
        const LightMapIndex& lightMap = model->lightMaps[static_cast<size_t>(surf.lightMap)];
        int32_t width = std::max(lightMap.uClamp, 1);
        int32_t height = std::max(lightMap.vClamp, 1);
        std::vector<Vector>& points = levelData.fogPoints[surfIndex];
        if (points.empty()) {
            points.resize(static_cast<size_t>(width * height));
            for (int32_t y = 0; y < height; y++) {
                for (int32_t x = 0; x < width; x++) {
                    points[static_cast<size_t>(y * width + x)] = lightmapTexelPoint(model, surf, x, y);
                }
            }
        }
        if (cache.extent < 0) {
            Vector low = points[0];
            Vector high = points[0];
            for (const Vector& point : points) {
                low = { std::min(low.x, point.x), std::min(low.y, point.y), std::min(low.z, point.z) };
                high = { std::max(high.x, point.x), std::max(high.y, point.y), std::max(high.z, point.z) };
            }
            cache.center = (low + high) * 0.5f;
            cache.extent = (high - low).size() * 0.5f;
        }
        std::vector<uint8_t>& bytes = scratchFog;
        bytes.assign(points.size() * 3, 0);
        bool any = false;
        for (size_t light = 0; light < static_cast<size_t>(count); light++) {
            const VolumetricLight& info = volumetrics[light];
            const std::array<int64_t, 3>& palette = volumetricPalettes[light];
            Vector toCenter = cache.center - view.location;
            float along = std::clamp(info.location.dot(toCenter) / std::max(toCenter.sizeSquared(), 1e-6f), 0.0f, 1.0f);
            if ((info.location - toCenter * along).size() - cache.extent > info.radius) {
                continue;
            }
            for (size_t i = 0; i < points.size(); i++) {
                float value = volumetricFog(info, points[i] - view.location);
                int64_t level = static_cast<int64_t>(std::nearbyint(value * 255.0f));
                if (level <= 0) {
                    continue;
                }
                for (size_t c = 0; c < 3; c++) {
                    uint8_t add = static_cast<uint8_t>(std::min<int64_t>((level * palette[c]) >> 16, 127));
                    bytes[i * 3 + c] = fogMerge(bytes[i * 3 + c], add);
                    any = any || bytes[i * 3 + c] != 0;
                }
            }
        }
        if (!any) {
            return false;
        }
        lightmaps.uploadFog(surf, width, height, bytes);
        levelData.fogged[index] = 1;
        cache.fogged = true;
        return true;
    }

    void SceneRenderer::drawFog(const Batch& batch, const View& view, int32_t onlyNode)
    {
        if (!view.fog || !batch.fogZone || volumetrics.empty() || batch.lightmapPage < 0) {
            return;
        }
        bool started = false;
        for (const auto& [first, count, surfIndex, backFog, frontFog, node] : batch.surfPolys) {
            if ((onlyNode >= 0 && node != onlyNode) || levelData.nodeStamp[static_cast<size_t>(node)] != visibleStamp) {
                continue;
            }
            const BspSurf& surf = levelData.model->surfs[static_cast<size_t>(surfIndex)];
            const Vector& normal = levelData.model->vectors[static_cast<size_t>(surf.vNormal)];
            bool front = normal.dot(view.location - levelData.model->points[static_cast<size_t>(surf.pBase)]) > 0;
            if (!(front ? frontFog : backFog) || !surfaceFog(surfIndex, view)) {
                continue;
            }
            if (!started) {
                device->setBlend(BlendMode::Highlight);
                device->setFlags(SHADER_Fog);
                device->bindLightmap(lightmaps.fogTexture(batch.lightmapPage));
                if (batch.polyFlags & PF_Masked) {
                    device->setDepthFunc(DepthFunc::Equal);
                }
                started = true;
            }
            device->drawStatic(levelData.vao, first, count);
        }
        if (started) {
            device->setDepthFunc(DepthFunc::LessEqual);
        }
    }

    void SceneRenderer::drawDetail(const Batch& batch, const View& view, int32_t onlyNode)
    {
        if (!batch.detail || batch.detailPolys.empty() || !levelData.detailVao) {
            return;
        }
        const Vector& forward = view.axes.xAxis;
        Vector nearest { forward.x > 0 ? batch.boundsMin.x : batch.boundsMax.x, forward.y > 0 ? batch.boundsMin.y : batch.boundsMax.y, forward.z > 0 ? batch.boundsMin.z : batch.boundsMax.z };
        if ((nearest - view.location).dot(forward) >= options.detailDistance) {
            return;
        }
        const GLTextureInfo* info = device->texture(device->animated(batch.detail));
        if (!info) {
            return;
        }
        bool started = false;
        for (const auto& [first, count, surfIndex, node] : batch.detailPolys) {
            if ((onlyNode >= 0 && node != onlyNode) || levelData.nodeStamp[static_cast<size_t>(node)] != visibleStamp) {
                continue;
            }
            if (batch.fogZone && view.fog && static_cast<size_t>(surfIndex) < levelData.fogFrame.size() && levelData.fogFrame[static_cast<size_t>(surfIndex)] == fogStamp && levelData.fogged[static_cast<size_t>(surfIndex)]) {
                continue;
            }
            bool near = false;
            for (int32_t i = first; i < first + count; i++) {
                if ((levelData.detailPoints[static_cast<size_t>(i)] - view.location).dot(forward) < options.detailDistance) {
                    near = true;
                    break;
                }
            }
            if (!near) {
                continue;
            }
            if (!started) {
                device->setBlend(BlendMode::Modulated);
                device->setFlags(SHADER_Detail);
                device->setPan(0, 0);
                device->bindTexture(info->id);
                float bias = (batch.polyFlags & PF_NotSolid) && !(batch.polyFlags & PF_Portal) ? -3.0f : -1.0f;
                device->setPolygonOffset(true, bias, bias);
                started = true;
            }
            device->drawStatic(levelData.detailVao, first, count);
        }
        if (started) {
            device->setPolygonOffset(false, 0.0f, 0.0f);
        }
    }

    uint32_t SceneRenderer::litMask(const Vector& low, const Vector& high, bool specialLit) const
    {
        uint32_t mask = 0;
        for (size_t i = 0; i < dynamicLights.size(); i++) {
            const DynamicLight& light = dynamicLights[i];
            if (light.specialLit != specialLit) {
                continue;
            }
            Vector closest { std::clamp(light.location.x, low.x, high.x), std::clamp(light.location.y, low.y, high.y), std::clamp(light.location.z, low.z, high.z) };
            if ((closest - light.location).sizeSquared() < light.radius * light.radius) {
                mask |= 1u << i;
            }
        }
        return mask;
    }

    void SceneRenderer::selectLights(const Vector& location, float extent)
    {
        actorLights.clear();
        actorLightPrepared.clear();
        actorLightColors.clear();
        for (size_t i = 0; i < lights.size(); i++) {
            const PreparedLight& prepared = lightPrepared[i];
            float reach = prepared.radius + extent;
            if ((prepared.location - location).sizeSquared() < reach * reach) {
                actorLights.push_back(lights[i]);
                actorLightPrepared.push_back(prepared);
                actorLightColors.push_back(lightColors[i]);
            }
        }
    }

    void SceneRenderer::selectActorLights(UObject* actor)
    {
        actorLights.clear();
        actorLightPrepared.clear();
        actorLightColors.clear();
        UModel* model = levelData.model;
        int32_t actorLeaf = *reinterpret_cast<int32_t*>(actor::Region(actor) + sizeof(UObject*));
        if (actor::bUnlit(actor) || !model || actorLeaf < 0 || model->leaves.empty() || static_cast<size_t>(actorLeaf) >= model->leaves.size()) {
            return;
        }
        auto cached = actorLightCache.find(actor);
        bool firstSee = cached == actorLightCache.end();
        std::vector<LightSender>& senders = actorLightCache[actor];
        senders.erase(std::remove_if(senders.begin(), senders.end(), [](const LightSender& sender) {
            return !sender.light || sender.light->destroyed;
        }),
            senders.end());
        lightConsider.clear();
        considerTag++;
        Vector actorLocation = actor::Location(actor);
        auto consider = [&](UObject* light, int32_t factor) {
            if (!light || light->destroyed || actor::ExtraTag(light) == considerTag) {
                return;
            }
            float distSquared = (actor::Location(light) - actorLocation).sizeSquared();
            float radius = 25.0f * (static_cast<float>(actor::LightRadius(light)) + 1.0f);
            if (!lightReachesActor(actor, light)) {
                return;
            }
            float bright = static_cast<float>(actor::LightBrightness(light));
            if (actor::LightEffect(light) == 17) {
                bright = static_cast<float>(static_cast<double>(bright) * 0.75);
                radius = static_cast<float>(static_cast<double>(radius) * 0.75);
            }
            float tag = static_cast<float>((1.0 - static_cast<double>(std::sqrt(distSquared) / radius)) * static_cast<double>(bright) * 1024.0);
            actor::LightingTag(light) = static_cast<int32_t>(std::nearbyint(tag));
            actor::ExtraTag(light) = considerTag;
            actor::SpecialTag(light) = factor;
            lightConsider.push_back(light);
        };
        for (const LightSender& sender : senders) {
            consider(sender.light, sender.factor);
        }
        int32_t permeating = model->leaves[static_cast<size_t>(actorLeaf)].permeating;
        if (permeating >= 0) {
            for (size_t i = static_cast<size_t>(permeating); i < model->lights.size() && model->lights[i] && lightConsider.size() < 120; i++) {
                consider(model->lights[i], 0);
            }
        }
        for (UObject* light : lights) {
            if (lightConsider.size() >= 120) {
                break;
            }
            if (!actor::bStatic(light)) {
                consider(light, 255);
            }
        }
        std::sort(lightConsider.begin(), lightConsider.end(), [](UObject* a, UObject* b) {
            return actor::LightingTag(a) > actor::LightingTag(b);
        });
        std::vector<UObject*> accepted;
        int32_t numVisible = 0;
        int32_t numStaticVisible = 0;
        int32_t threshold = -1;
        World* world = World::current;
        for (UObject* light : lightConsider) {
            bool isStatic = actor::bStatic(light) != 0;
            bool visible = false;
            if (actor::LightingTag(light) < threshold || (isStatic ? numStaticVisible : numVisible) >= 6) {
                visible = false;
            } else if (!isStatic && actor::bMovable(light)) {
                visible = true;
            } else if (firstSee || ((static_cast<int32_t>(light->handle) ^ lightFrameCount) & 0xf) == 0) {
                CheckResult hit;
                visible = !world || !world->bspLineCheck(hit, actorLocation, actor::Location(light), {});
                if (visible && firstSee) {
                    actor::SpecialTag(light) = 255;
                }
            } else {
                visible = (actor::SpecialTag(light) & 1) != 0;
            }
            if (visible) {
                numVisible++;
                if (isStatic) {
                    numStaticVisible++;
                }
                if (threshold == -1) {
                    threshold = actor::LightingTag(light) / 8;
                }
                accepted.push_back(light);
                actor::SpecialTag(light) = std::min(actor::SpecialTag(light) + lightDelta, 255) | 1;
            } else {
                actor::SpecialTag(light) = std::max(actor::SpecialTag(light) - lightDelta, 0) & ~1;
                if (actor::SpecialTag(light) > 0) {
                    accepted.push_back(light);
                }
            }
        }
        senders.clear();
        for (UObject* light : lightConsider) {
            if (senders.size() >= 16) {
                break;
            }
            if (actor::SpecialTag(light) > 1) {
                senders.push_back({ light, actor::SpecialTag(light) });
            }
        }
        for (UObject* light : accepted) {
            if (actor::LightType(light) == 0 || actor::LightBrightness(light) == 0 || light == actor || actorLights.size() >= 256) {
                continue;
            }
            auto found = lightIndex.find(light);
            PreparedLight prepared = found != lightIndex.end() ? lightPrepared[found->second] : prepareLight(light, actor::Location(light));
            Vector color = found != lightIndex.end() ? lightColors[found->second] : lightColor(light, static_cast<float>(time));
            float factor = static_cast<float>(static_cast<double>(actor::SpecialTag(light)) * (1.0 / 255.0));
            actorLights.push_back(light);
            actorLightPrepared.push_back(prepared);
            actorLightColors.push_back(color * factor);
        }
    }

    void SceneRenderer::collectLights(const std::vector<UObject*>& actors)
    {
        lights.clear();
        lightPrepared.clear();
        lightColors.clear();
        lightIndex.clear();
        for (UObject* actor : actors) {
            if (isLightSource(actor)) {
                lightIndex[actor] = lights.size();
                lights.push_back(actor);
                lightPrepared.push_back(prepareLight(actor, actor::Location(actor)));
                lightColors.push_back(lightColor(actor, static_cast<float>(time)));
            }
        }
    }

    void SceneRenderer::collectDynamicLights(const Vector& viewLocation)
    {
        std::vector<std::pair<float, UObject*>> candidates;
        for (UObject* light : lights) {
            if (!lightDynamicOnWorld(light)) {
                continue;
            }
            float radius = lightRadius(light);
            float distance = (actor::Location(light) - viewLocation).size() - radius;
            candidates.emplace_back(distance, light);
        }
        std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
            return a.first < b.first;
        });
        dynamicLights.clear();
        for (const auto& [distance, light] : candidates) {
            if (dynamicLights.size() >= MaxDynamicLights) {
                break;
            }
            DynamicLight entry;
            entry.location = actor::Location(light);
            entry.radius = lightRadius(light);
            entry.color = lightColor(light, static_cast<float>(time));
            entry.specialLit = actor::bSpecialLit(light) != 0;
            uint8_t effect = actor::LightEffect(light);
            if (effect == 8 || effect == 12) {
                entry.direction = -rotationVector(actor::Rotation(light));
                entry.cone = 1.0f - static_cast<float>(actor::LightCone(light)) / 256.0f;
            }
            dynamicLights.push_back(entry);
        }
    }

    bool SceneRenderer::sphereOutsideView(const View& view, const Vector& location, float radius) const
    {
        Vector delta = location - view.location;
        float forward = delta.dot(view.axes.xAxis);
        float right = delta.dot(view.axes.yAxis);
        float up = delta.dot(view.axes.zAxis);
        float widthScale = 1.0f / std::sqrt(1.0f + view.tanHalfWidth * view.tanHalfWidth);
        float heightScale = 1.0f / std::sqrt(1.0f + view.tanHalfHeight * view.tanHalfHeight);
        return (forward * view.tanHalfWidth - right) * widthScale < -radius || (forward * view.tanHalfWidth + right) * widthScale < -radius || (forward * view.tanHalfHeight - up) * heightScale < -radius || (forward * view.tanHalfHeight + up) * heightScale < -radius;
    }

    bool SceneRenderer::sphereReachesLeaf(const Vector& center, float radius, int32_t leaf) const
    {
        UModel* model = levelData.model;
        if (!model || model->nodes.empty() || leaf < 0) {
            return false;
        }
        std::vector<int32_t> stack { 0 };
        while (!stack.empty()) {
            const BspNode& node = model->nodes[static_cast<size_t>(stack.back())];
            stack.pop_back();
            float distance = node.plane.x * center.x + node.plane.y * center.y + node.plane.z * center.z - node.plane.w;
            if (distance > -radius) {
                if (node.front > 0) {
                    stack.push_back(node.front);
                } else if (node.leaf[1] == leaf) {
                    return true;
                }
            }
            if (distance < radius) {
                if (node.back > 0) {
                    stack.push_back(node.back);
                } else if (node.leaf[0] == leaf) {
                    return true;
                }
            }
        }
        return false;
    }

    void SceneRenderer::addCorona(UObject* light, const Vector& origin, float delta, size_t& free, UObject* viewportActor)
    {
        if (!light || !light->data || light->destroyed || !actor::bCorona(light) || !actor::Skin(light) || actor::bDeleteMe(light)) {
            return;
        }
        World* world = World::current;
        if (!world) {
            return;
        }
        CheckResult hit;
        if (world->lineCheck(hit, origin, actor::Location(light), {}, nullptr, TRACE_Level | TRACE_Movers)) {
            return;
        }
        if (world->lineCheck(hit, origin, actor::Location(light), {}, nullptr, TRACE_Pawns | TRACE_Others) && hit.actor != viewportActor) {
            return;
        }
        for (CoronaLight& corona : coronas) {
            if (corona.actor == light) {
                corona.bright = std::min(corona.bright + 2.0f * delta, 1.0f);
                return;
            }
        }
        while (free < std::size(coronas) && coronas[free].actor) {
            free++;
        }
        if (free < std::size(coronas)) {
            coronas[free].actor = light;
            coronas[free].bright = std::min(2.0f * delta, 1.0f);
        }
    }

    void SceneRenderer::drawCoronas(const View& view, int x, int y, int width, int height)
    {
        UModel* model = levelData.model;
        World* world = World::current;
        UObject* viewportActor = world ? world->player() : nullptr;
        if (!model || !viewportActor || viewportActor->destroyed || !*reinterpret_cast<UObject**>(actor::Region(viewportActor))) {
            return;
        }
        int32_t leaf = *reinterpret_cast<int32_t*>(actor::Region(viewportActor) + sizeof(UObject*));
        if (leaf < 0 || static_cast<size_t>(leaf) >= model->leaves.size()) {
            return;
        }
        if (coronaTime < 0) {
            coronaTime = currentTime;
        }
        float delta = static_cast<float>(3.0 * (currentTime - coronaTime));
        coronaTime = currentTime;
        for (CoronaLight& corona : coronas) {
            if (corona.actor) {
                corona.bright -= delta;
                if (corona.bright < 0) {
                    corona.actor = nullptr;
                }
            }
        }
        size_t free = 0;
        int32_t permeating = model->leaves[static_cast<size_t>(leaf)].permeating;
        if (permeating >= 0) {
            for (size_t i = static_cast<size_t>(permeating); i < model->lights.size() && model->lights[i]; i++) {
                addCorona(model->lights[i], view.location, delta, free, viewportActor);
            }
        }
        for (UObject* light : lights) {
            if ((actor::bStatic(light) || actor::bNoDelete(light)) && !actor::bDynamicLight(light)) {
                continue;
            }
            float maxRadius = std::max(lightRadius(light), 25.0f * (static_cast<float>(actor::VolumeRadius(light)) + 1.0f));
            if (actor::LightRadius(light) == 0 || sphereOutsideView(view, actor::Location(light), maxRadius) || !sphereReachesLeaf(actor::Location(light), lightRadius(light), leaf)) {
                continue;
            }
            addCorona(light, view.location, delta, free, viewportActor);
        }
        UObject* sprite = viewportActor->isA(namePlayerPawn) ? actor::Sprite(viewportActor) : nullptr;
        float halfWidth = static_cast<float>(width) * 0.5f;
        float halfHeight = static_cast<float>(height) * 0.5f;
        float projection = halfWidth / view.tanHalfWidth;
        for (const CoronaLight& corona : coronas) {
            UObject* light = corona.actor;
            if (!light || light->destroyed) {
                continue;
            }
            Vector delta3 = actor::Location(light) - view.location;
            float depth = delta3.dot(view.axes.xAxis);
            if (depth <= 1.0f) {
                continue;
            }
            uint8_t hue = actor::LightHue(light);
            float h = static_cast<float>(hue);
            Vector base;
            if (hue < 86) {
                base = { (85.0f - h) / 85.0f, h / 85.0f, 0.0f };
            } else if (hue < 171) {
                base = { 0.0f, (170.0f - h) / 85.0f, (h - 85.0f) / 85.0f };
            } else {
                base = { (h - 170.0f) / 85.0f, 0.0f, (255.0f - h) / 84.0f };
            }
            float alpha = static_cast<float>(actor::LightSaturation(light)) / 255.0f;
            Vector color = (base + (Vector { 1, 1, 1 } - base) * alpha) * corona.bright;
            float scale = static_cast<float>(width) * actor::DrawScale(light) * 0.2f;
            float screenX = delta3.dot(view.axes.yAxis) * projection / depth + halfWidth + static_cast<float>(x);
            float screenY = -delta3.dot(view.axes.zAxis) * projection / depth + halfHeight + static_cast<float>(y);
            if (UTexture* spriteTexture = asTexture(sprite)) {
                UTexture* frame = device->animated(spriteTexture);
                actor::Skin(light) = frame ? frame : spriteTexture;
            }
            UTexture* skin = asTexture(actor::Skin(light));
            if (!skin) {
                continue;
            }
            device->canvas().drawTile(skin, screenX - scale * 0.5f, screenY - scale * 0.5f, scale, scale, 0, 0, static_cast<float>(skin->width()), static_cast<float>(skin->height()), { color.x, color.y, color.z, 1 }, BlendStyle::Translucent, true);
        }
    }

    Vector SceneRenderer::actorLight(UObject* actor, const Vector& point, const Vector& normal, const Vector& ambient, float scaleGlow, bool unlit)
    {
        if (unlit) {
            return { 1, 1, 1 };
        }
        Vector lit;
        for (size_t i = 0; i < actorLights.size(); i++) {
            UObject* light = actorLights[i];
            if (light == actor) {
                continue;
            }
            const PreparedLight& prepared = actorLightPrepared[i];
            Vector delta = prepared.location - point;
            float distance = delta.size();
            if (distance >= prepared.radius || distance <= 0) {
                continue;
            }
            float incidence = 1.0f + delta.dot(normal) / distance;
            float g = std::max(incidence * incidence - 1.5f, 0.0f);
            g *= 1.0f - distance / prepared.radius;
            if (g > 0) {
                lit += actorLightColors[i] * g;
            }
        }
        Vector color = lit * (1.4f * scaleGlow) + ambient;
        return { std::min(color.x, 1.0f), std::min(color.y, 1.0f), std::min(color.z, 1.0f) };
    }

    const std::vector<MeshTriangle>& SceneRenderer::meshTriangles(UMesh* mesh)
    {
        auto it = triangleCache.find(mesh);
        if (it == triangleCache.end()) {
            it = triangleCache.emplace(mesh, mesh->triangles()).first;
        }
        return it->second;
    }

    void SceneRenderer::setActorOrder(UObject* actor, const View& view)
    {
        UModel* model = levelData.model;
        Vector location = actor::Location(actor);
        currentOrder = fallbackOrder;
        int32_t index = 0;
        for (int guard = 0; model && guard < 10000 && static_cast<size_t>(index) < model->nodes.size(); guard++) {
            if (levelData.cullStamp[static_cast<size_t>(index)] == visibleStamp) {
                currentOrder = levelData.cullOrder[static_cast<size_t>(index)];
                break;
            }
            const BspNode& node = model->nodes[static_cast<size_t>(index)];
            bool front = node.plane.x * location.x + node.plane.y * location.y + node.plane.z * location.z - node.plane.w >= 0;
            int32_t next = front ? node.front : node.back;
            if (next <= 0) {
                int32_t leaf = node.leaf[front ? 1 : 0];
                if (leaf >= 0 && static_cast<size_t>(leaf) < leafOrder.size() && levelData.leafStamp[static_cast<size_t>(leaf)] == visibleStamp) {
                    currentOrder = leafOrder[static_cast<size_t>(leaf)];
                }
                break;
            }
            index = next;
        }
        currentDistance = (actor::Location(actor) - view.location).size();
    }

    void SceneRenderer::queueTranslucent(uint32_t flags, uint32_t texture, const GroupExtra& extra, const std::vector<GLVertex>& vertices)
    {
        if (translucentCount == translucentItems.size()) {
            translucentItems.emplace_back();
        }
        TranslucentItem& item = translucentItems[translucentCount++];
        item.order = currentOrder;
        item.distance = currentDistance;
        item.flags = flags;
        item.texture = texture;
        item.extra = extra;
        item.vertices.assign(vertices.begin(), vertices.end());
    }

    void SceneRenderer::flushTranslucent(const View& view)
    {
        applyView(view);
        std::vector<size_t> order(translucentCount);
        for (size_t i = 0; i < translucentCount; i++) {
            order[i] = i;
        }
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            const TranslucentItem& first = translucentItems[a];
            const TranslucentItem& second = translucentItems[b];
            if (first.order != second.order) {
                return first.order < second.order;
            }
            return first.distance > second.distance;
        });
        for (size_t index : order) {
            TranslucentItem& item = translucentItems[index];
            if (item.flags & PF_AlphaBlend) {
                std::vector<std::pair<float, size_t>>& faces = scratchAlphaOrder;
                faces.clear();
                for (size_t first = 0; first + 2 < item.vertices.size(); first += 3) {
                    Vector a { item.vertices[first].x, item.vertices[first].y, item.vertices[first].z };
                    Vector b { item.vertices[first + 1].x, item.vertices[first + 1].y, item.vertices[first + 1].z };
                    Vector c { item.vertices[first + 2].x, item.vertices[first + 2].y, item.vertices[first + 2].z };
                    if (item.extra.cull && (b - a).cross(c - a).dot(view.location - a) <= 0) {
                        continue;
                    }
                    faces.push_back({ ((a + b + c) * (1.0f / 3.0f) - view.location).sizeSquared(), first });
                }
                std::stable_sort(faces.begin(), faces.end(), [](const std::pair<float, size_t>& left, const std::pair<float, size_t>& right) {
                    return left.first > right.first;
                });
                std::vector<GLVertex>& sorted = scratchAlphaVertices;
                sorted.clear();
                for (const std::pair<float, size_t>& face : faces) {
                    sorted.insert(sorted.end(), item.vertices.begin() + static_cast<std::ptrdiff_t>(face.second), item.vertices.begin() + static_cast<std::ptrdiff_t>(face.second + 3));
                }
                item.vertices.swap(sorted);
            }
            drawGroup(item.flags, item.texture, item.extra, item.vertices);
        }
        translucentCount = 0;
        device->setBlend(BlendMode::Opaque);
    }

    void SceneRenderer::flushMeshBatches()
    {
        flushBatches(meshBatches);
    }

    void SceneRenderer::drawGroup(uint32_t flags, uint32_t texture, const GroupExtra& extra, const std::vector<GLVertex>& vertices)
    {
        if (vertices.empty()) {
            return;
        }
        if (wireframeActors) {
            std::vector<GLVertex> lines;
            lines.reserve(vertices.size() * 2);
            for (size_t i = 0; i + 2 < vertices.size(); i += 3) {
                for (int edge = 0; edge < 3; edge++) {
                    for (int end = 0; end < 2; end++) {
                        GLVertex vertex = vertices[i + static_cast<size_t>((edge + end) % 3)];
                        vertex.r = 0.6f;
                        vertex.g = 0.4f;
                        vertex.b = 0.1f;
                        vertex.a = 1.0f;
                        lines.push_back(vertex);
                    }
                }
            }
            device->setBlend(BlendMode::Opaque);
            device->setFlags(0);
            device->setCulling(false);
            device->bindTexture(device->whiteTexture());
            device->draw(lines, Primitive::Lines);
            return;
        }
        if (!extra.lightmap) {
            applyBlend(flags, 0);
            bool alphaBlend = (flags & PF_AlphaBlend) && !(flags & (PF_Translucent | PF_Modulated));
            if (alphaBlend) {
                device->setBlend(BlendMode::Alpha);
                device->setDepthWrite(false);
            }
            device->setCulling(false);
            device->bindTexture(texture);
            device->draw(vertices, Primitive::Triangles);
            if (alphaBlend) {
                device->setDepthWrite(true);
            }
            return;
        }
        int32_t shaderFlags = SHADER_Lightmap | SHADER_Surface;
        if (extra.lightMask) {
            shaderFlags |= SHADER_Dynamic;
            batchLights.clear();
            for (size_t i = 0; i < dynamicLights.size(); i++) {
                if (extra.lightMask & (1u << i)) {
                    batchLights.push_back(dynamicLights[i]);
                }
            }
            device->setDynamicLights(batchLights);
        }
        device->bindLightmap(extra.lightmap);
        device->bindTexture(texture);
        if ((flags & PF_Translucent) && !device->lightsTranslucencyInOnePass()) {
            applyBlend(flags, 0);
            if (extra.cull) {
                device->setCulling(true);
            } else {
                device->setCulling(false);
            }
            device->draw(vertices, Primitive::Triangles);
            device->setBlend(BlendMode::Modulated);
            device->setFlags(shaderFlags);
            device->setTint(0.5f, 0.5f, 0.5f, 1);
            device->draw(vertices, Primitive::Triangles);
            device->setTint(1, 1, 1, 1);
            return;
        }
        applyBlend(flags, shaderFlags);
        if (extra.cull) {
            device->setCulling(true);
        } else {
            device->setCulling(false);
        }
        device->draw(vertices, Primitive::Triangles);
    }

    void SceneRenderer::flushBatches(std::unordered_map<uint64_t, std::vector<GLVertex>>& batches)
    {
        for (auto& [key, vertices] : batches) {
            drawGroup(meshBatchFlags[key], meshBatchTextures[key], meshBatchExtras[key], vertices);
            vertices.clear();
        }
        device->setBlend(BlendMode::Opaque);
    }

    bool SceneRenderer::sameLights(const LightState& state) const
    {
        if (state.actors.size() != actorLights.size()) {
            return false;
        }
        for (size_t i = 0; i < actorLights.size(); i++) {
            if (state.actors[i] != actorLights[i] || !sameVector(state.colors[i], actorLightColors[i]) || !samePrepared(state.prepared[i], actorLightPrepared[i])) {
                return false;
            }
        }
        return true;
    }

    void SceneRenderer::storeLights(LightState& state) const
    {
        state.actors = actorLights;
        state.prepared = actorLightPrepared;
        state.colors = actorLightColors;
    }

    void SceneRenderer::appendGroups(const std::vector<EmittedGroup>& groups, std::unordered_map<uint64_t, std::vector<GLVertex>>& opaque)
    {
        for (const EmittedGroup& group : groups) {
            if (group.translucent) {
                queueTranslucent(group.flags, group.texture, { group.lightmap, group.lightMask, group.cull }, group.vertices);
                continue;
            }
            meshBatchFlags[group.key] = group.flags;
            meshBatchTextures[group.key] = group.texture;
            meshBatchExtras[group.key] = { group.lightmap, group.lightMask, group.cull };
            std::vector<GLVertex>& out = opaque[group.key];
            out.insert(out.end(), group.vertices.begin(), group.vertices.end());
        }
    }

    void SceneRenderer::drawMesh(UObject* actor, const View& view)
    {
        UObject* meshObject = actor::Mesh(actor);
        if (!meshObject) {
            return;
        }
        objects::ensureLoaded(meshObject);
        UMesh* mesh = cast<UMesh>(meshObject);
        if (!mesh || mesh->frameVerts <= 0) {
            return;
        }
        UObject* animSource = actor;
        UObject* owner = actor::Owner(actor);
        if (actor::bAnimByOwner(actor) && owner && owner->data) {
            animSource = owner;
        }
        Coords rotation = rotationAxes(actor::Rotation(actor));
        float drawScale = actor::DrawScale(actor);
        MeshTransform transform;
        transform.origin = actor::Location(actor) + actor::PrePivot(actor);
        transform.x = rotation.xAxis * drawScale;
        transform.y = rotation.yAxis * drawScale;
        transform.z = rotation.zAxis * drawScale;
        drawMeshWith(actor, actor, mesh, animSource, transform, view);
    }

    void SceneRenderer::drawMeshWith(UObject* actor, UObject* lightActor, UMesh* mesh, UObject* animSource, const MeshTransform& transform, const View& view)
    {
        MeshAnimState animState;
        animState.sequence = actor::AnimSequence(animSource);
        animState.frame = actor::AnimFrame(animSource);
        MeshDrawCache& cache = meshDrawCaches[{ actor, mesh, lightActor }];
        bool sameGeometry = cache.valid && sameVector(cache.transform.origin, transform.origin) && sameVector(cache.transform.x, transform.x) && sameVector(cache.transform.y, transform.y) && sameVector(cache.transform.z, transform.z) && cache.sequence == animState.sequence && cache.frame == animState.frame;

        ULodMesh* lodMesh = cast<ULodMesh>(mesh);
        bool doLod = lodMesh && lodMesh->lodStrength != 0.0f && !lodMesh->collapsePointThus.empty();
        bool doMorph = false;
        float targetSubset = -1;
        int32_t vertexSubset = 0;
        int32_t nonMorph = 0;
        if (doLod) {
            int32_t modelVerts = static_cast<int32_t>(lodMesh->modelVerts);
            doMorph = lodMesh->lodMorph > 0.0f;
            float fovBias = static_cast<float>(std::tan(static_cast<double>(view.fov) * 0.008726646259971648));
            float meshVertLod = static_cast<float>(lodMesh->modelVerts) * 0.003f + 0.25f;
            float resFactor = static_cast<float>(static_cast<double>(frameWidth) * 0.00109375 + 0.3);
            float z = (actor::Location(actor) - view.location).dot(view.axes.xAxis);
            float dist = std::max(z - lodMesh->lodZDisplace, 1.0f);
            float den = 0.25f * lodMesh->lodStrength * dist * meshVertLod * fovBias;
            float num = lodMesh->meshScaleMax * actor::DrawScale(actor) * actor::LODBias(actor) * resFactor * 430.0f;
            float target = num / den * static_cast<float>(modelVerts);
            targetSubset = std::max(static_cast<float>(lodMesh->lodMinVerts), target);
            vertexSubset = std::min(static_cast<int32_t>(std::nearbyint(targetSubset)), modelVerts);
            if (doMorph) {
                nonMorph = static_cast<int32_t>(std::nearbyint((1.0f - lodMesh->lodMorph) * targetSubset));
                if (nonMorph >= vertexSubset) {
                    doMorph = false;
                }
            }
            if (!doMorph && vertexSubset >= modelVerts) {
                targetSubset = -1;
            }
        }
        if (cache.lodTarget != targetSubset) {
            sameGeometry = false;
        }
        const std::vector<MeshTriangle>* triangleSource = &meshTriangles(mesh);
        std::vector<MeshTriangle>& lodTriangles = scratchLodTriangles;
        std::vector<float>& lodAlphas = scratchLodAlphas;
        std::vector<Vector>& pose = scratchPose;
        std::vector<Vector>& normals = scratchNormals;
        std::vector<Vector>& world = scratchWorld;
        std::vector<Vector>& worldNormals = scratchWorldNormals;
        auto computeGeometry = [&]() {
            int32_t requested = lodMesh && doLod ? vertexSubset + static_cast<int32_t>(lodMesh->specialVerts) : mesh->frameVerts;
            int32_t returned = mesh->animatedPose(animState, poseCaches[{ animSource, mesh }], pose, requested);
            if (lodMesh && doLod) {
                vertexSubset = std::max(returned - static_cast<int32_t>(lodMesh->specialVerts), 0);
                if (doMorph && nonMorph >= vertexSubset) {
                    doMorph = false;
                }
                if (!doMorph && vertexSubset >= static_cast<int32_t>(lodMesh->modelVerts)) {
                    doLod = false;
                }
            }
            if (doLod) {
                lodAlphas.assign(lodMesh->modelVerts, 0.0f);
                if (doMorph) {
                    float inverse = 1.0f / (targetSubset * lodMesh->lodMorph);
                    for (int32_t i = vertexSubset - 1; i > nonMorph; i--) {
                        float alpha = std::min(1.0f, static_cast<float>(i - nonMorph) * inverse);
                        int32_t target = static_cast<size_t>(i) < lodMesh->collapsePointThus.size() ? lodMesh->collapsePointThus[static_cast<size_t>(i)] : 0;
                        size_t from = static_cast<size_t>(lodMesh->poseVertex(i));
                        size_t to = static_cast<size_t>(lodMesh->poseVertex(target));
                        if (target > 0 && from < pose.size() && to < pose.size()) {
                            pose[from] += (pose[to] - pose[from]) * alpha;
                        } else {
                            alpha = 0.0f;
                        }
                        lodAlphas[static_cast<size_t>(i)] = alpha;
                    }
                }
                lodMesh->lodTriangles(vertexSubset, doMorph, nonMorph, lodAlphas, lodTriangles);
                triangleSource = &lodTriangles;
            }
            normals.assign(pose.size(), Vector {});
            for (const MeshTriangle& triangle : *triangleSource) {
                if (static_cast<size_t>(triangle.vertex[0]) >= pose.size() || static_cast<size_t>(triangle.vertex[1]) >= pose.size() || static_cast<size_t>(triangle.vertex[2]) >= pose.size()) {
                    continue;
                }
                const Vector& a = pose[static_cast<size_t>(triangle.vertex[0])];
                const Vector& b = pose[static_cast<size_t>(triangle.vertex[1])];
                const Vector& c = pose[static_cast<size_t>(triangle.vertex[2])];
                Vector normal = (b - a).cross(c - a);
                normal = normal * (1.0f / std::sqrt(normal.sizeSquared() + 0.001f));
                for (int i = 0; i < 3; i++) {
                    normals[static_cast<size_t>(triangle.vertex[i])] += normal;
                }
            }
            world.resize(pose.size());
            worldNormals.resize(pose.size());
            for (size_t i = 0; i < pose.size(); i++) {
                world[i] = transform.origin + transform.x * pose[i].x + transform.y * pose[i].y + transform.z * pose[i].z;
                worldNormals[i] = (transform.x * normals[i].x + transform.y * normals[i].y + transform.z * normals[i].z).normal();
            }
        };
        if (!sameGeometry) {
            computeGeometry();
            boundingSphere(world, cache.sphereCenter, cache.sphereRadius);
            cache.valid = true;
            cache.transform = transform;
            cache.sequence = animState.sequence;
            cache.frame = animState.frame;
            cache.lodTarget = targetSubset;
            cache.world.swap(world);
            cache.worldNormals.swap(worldNormals);
            cache.lodTriangles = triangleSource == &lodTriangles;
            if (cache.lodTriangles) {
                cache.triangles.swap(lodTriangles);
            }
        }
        triangleSource = cache.lodTriangles ? &cache.triangles : &meshTriangles(mesh);

        std::vector<UTexture*>& skins = scratchSkins;
        skins.assign(std::max<size_t>(mesh->textures.size(), 8), nullptr);
        for (size_t i = 0; i < skins.size(); i++) {
            skins[i] = meshSkin(actor, mesh, i);
        }
        size_t environmentSlot = skins.size();
        skins.push_back(meshEnvironmentMap(actor, mesh, zoneForActor(actor), levelData.levelInfo));
        emitMesh(actor, lightActor, cache, *triangleSource, environmentSlot, sameGeometry, view);
    }

    void SceneRenderer::drawSkeletal(UObject* actor, const View& view)
    {
        USkelModel* model = skeletal::geometryModel(actor);
        float curve = skeletal::lodCurve(actor, view.location);
        skeletal::setLodCurve(actor, curve);
        const SkelPose* pose = skeletal::pose(actor);
        if (!model || !pose || model->meshes.empty()) {
            return;
        }
        const SkelMesh& mesh = model->meshes[std::min<size_t>(actor::SkelMesh(actor), model->meshes.size() - 1)];
        MeshDrawCache& cache = meshDrawCaches[{ actor, nullptr, actor }];
        std::vector<Vector>& world = cache.world;
        bool mirrored = actor::bMirrored(actor) != 0;
        skeletal::skin(*model, mesh, *pose, mirrored, curve < 0.1f, world);
        bool wire = objects::editorMode();
        const SkelDecimation& decimation = skeletal::decimate(mesh, skeletal::decimationLevel(actor, mesh, curve, skelApproximation, options.lodPercentage, wire));
        std::vector<MeshTriangle>& triangles = scratchLodTriangles;
        triangles.clear();
        std::vector<Vector>& normals = cache.worldNormals;
        normals.assign(world.size(), Vector {});
        for (size_t index = 0; index < decimation.tris.size(); index++) {
            if (decimation.removed[index]) {
                continue;
            }
            const SkelTriangle& tri = decimation.tris[index];
            if (tri.vertex[0] >= world.size() || tri.vertex[1] >= world.size() || tri.vertex[2] >= world.size()) {
                continue;
            }
            MeshTriangle triangle;
            for (int i = 0; i < 3; i++) {
                int source = mirrored ? 2 - i : i;
                triangle.vertex[i] = tri.vertex[source];
                triangle.u[i] = tri.uv[source][0];
                triangle.v[i] = tri.uv[source][1];
            }
            triangle.textureIndex = tri.group % SkelPolyGroups;
            triangle.polyFlags = static_cast<uint32_t>(actor::SkelGroupFlags(actor, triangle.textureIndex));
            triangles.push_back(triangle);
            uint16_t first = tri.vertex[mirrored ? 2 : 0];
            uint16_t last = tri.vertex[mirrored ? 0 : 2];
            Vector normal = (world[tri.vertex[1]] - world[first]).cross(world[last] - world[first]);
            normal = normal * (1.0f / std::sqrt(normal.sizeSquared() + 0.001f));
            for (uint16_t vertex : tri.vertex) {
                normals[vertex] += normal;
            }
        }
        for (Vector& normal : normals) {
            normal = normal.normal();
        }
        boundingSphere(world, cache.sphereCenter, cache.sphereRadius);
        cache.valid = true;

        std::array<UTexture*, SkelPolyGroups>& defaults = skelSkins[&mesh];
        if (!skelSkinsResolved.count(&mesh)) {
            skelSkinsResolved.insert(&mesh);
            for (size_t group = 0; group < defaults.size(); group++) {
                defaults[group] = mesh.groupSkins[group].none() ? nullptr : asTexture(objects::loadObject(model->outermost()->name.str() + "." + mesh.groupSkins[group].str(), objects::findClass("Engine.Texture")));
            }
        }
        std::vector<UTexture*>& skins = scratchSkins;
        skins.assign(SkelPolyGroups, nullptr);
        for (size_t group = 0; group < defaults.size(); group++) {
            UTexture* custom = asTexture(actor::SkelGroupSkins(actor, static_cast<int>(group)));
            skins[group] = custom ? custom : defaults[group];
        }
        skins.push_back(asTexture(actor::Texture(actor)));
        emitMesh(actor, actor, cache, triangles, SkelPolyGroups, false, view);
    }

    void SceneRenderer::emitMesh(UObject* actor, UObject* lightActor, MeshDrawCache& cache, const std::vector<MeshTriangle>& triangles, size_t environmentSlot, bool sameGeometry, const View& view)
    {
        const std::vector<MeshTriangle>* triangleSource = &triangles;
        const std::vector<Vector>& meshWorld = cache.world;
        const std::vector<Vector>& meshNormals = cache.worldNormals;
        std::vector<UTexture*>& skins = scratchSkins;

        selectActorLights(lightActor);
        shadow::updateVector(lightActor, actorLightPrepared);
        UObject* zone = zoneForActor(lightActor);
        Vector ambient = zoneAmbient(zone);
        ZoneFog zoneFog = actorDistanceFog(actor);
        uint8_t glow = actor::AmbientGlow(actor);
        float glowAmount = glow == 255 ? static_cast<float>(0.25 + 0.2 * std::sin(8.0 * currentTime)) : static_cast<float>(glow) / 255.0f;
        ambient += Vector { glowAmount, glowAmount, glowAmount };
        float scaleGlow = actor::ScaleGlow(actor);
        uint32_t actorFlags = styleFlags(actor);
        if (!device->blendsAlpha()) {
            actorFlags &= ~PF_AlphaBlend;
        }
        bool skeletalMesh = actor::DrawType(actor) == DT_SkeletalMesh;
        if (!skeletalMesh && actor::bMeshEnviroMap(actor)) {
            actorFlags |= PF_Environment;
        }
        bool unlitActor = actor::bUnlit(actor) != 0;
        float clampedGlow = std::clamp(0.5f * actor::ScaleGlow(actor) + static_cast<float>(glow) / 256.0f, 0.0f, 1.0f);

        std::vector<const GLTextureInfo*>& slotInfo = scratchSlotInfo;
        std::vector<uint32_t>& slotFlags = scratchSlotFlags;
        slotInfo.assign(skins.size(), nullptr);
        slotFlags.assign(skins.size(), 0);
        for (size_t i = 0; i < skins.size(); i++) {
            UTexture* texture = device->animated(skins[i]);
            if (texture) {
                slotInfo[i] = device->texture(texture);
                slotFlags[i] = textureFlags(texture) & (PF_Masked | PF_Unlit);
            }
        }

        Vector colorAdjust = actor::ColorAdjust(actor);
        float alpha = (actorFlags & PF_AlphaBlend) ? actor::AlphaScale(actor) : 1.0f;
        uint8_t fatness = actor::Fatness(actor);
        bool cacheHit = sameGeometry && cache.cacheable && cache.alpha == alpha && cache.fatness == fatness && sameVector(cache.colorAdjust, colorAdjust) && cache.slotInfo == slotInfo && cache.slotFlags == slotFlags && cache.actorFlags == actorFlags && cache.unlitActor == unlitActor && cache.scaleGlow == scaleGlow && cache.glowScale == clampedGlow && sameVector(cache.ambient, ambient) && sameLights(cache.lights);
        if (cacheHit) {
            appendGroups(cache.groups, meshBatches);
            return;
        }

        bool cacheable = glow != 255;
        for (const PreparedLight& prepared : actorLightPrepared) {
            if (timeDependentEffect(prepared.effect)) {
                cacheable = false;
            }
        }
        bool fogged = view.fog && !volumetrics.empty();
        if (fogged) {
            cacheable = false;
        }
        cache.slotInfo = slotInfo;
        cache.slotFlags = slotFlags;
        cache.actorFlags = actorFlags;
        cache.unlitActor = unlitActor;
        cache.scaleGlow = scaleGlow;
        cache.glowScale = clampedGlow;
        cache.ambient = ambient;
        cache.colorAdjust = colorAdjust;
        cache.alpha = alpha;
        cache.fatness = fatness;
        storeLights(cache.lights);
        cache.groups.clear();

        std::vector<MeshLight>& meshLights = scratchMeshLights;
        meshLights.clear();
        Vector centre = actor::Location(lightActor);
        for (size_t i = 0; i < actorLights.size(); i++) {
            if (actorLights[i] != lightActor) {
                meshLights.push_back(meshLight(actorLightPrepared[i].location, actorLightPrepared[i].radius, actorLightColors[i], centre));
            }
        }
        uint8_t zoneNumber = *(actor::Region(actor) + sizeof(UObject*) + sizeof(int32_t));
        bool singleColor = actor::bPointLight(actor) != 0 || zoneNumber == 0;
        Vector actorColor = zoneNumber == 0 ? Vector { 0.5f, 0.5f, 0.5f } : adjustColor(meshCentreLight(meshLights, ambient, scaleGlow), colorAdjust);
        bool fat = fatness != 128 && (!skeletalMesh || (!actor::bPreLight(actor) && !singleColor));
        float fatOffset = static_cast<float>(fatness) * 0.0625f - 8.0f;
        Vector unlitColor { clampedGlow, clampedGlow, clampedGlow };
        if (skeletalMesh && (colorAdjust.x != 0 || colorAdjust.y != 0 || colorAdjust.z != 0)) {
            unlitColor = colorAdjust * (0.6f / 255.0f) + unlitColor * 0.4f;
        }

        std::vector<size_t>& slotGroups = scratchSlotGroups;
        slotGroups.assign(skins.size() * 6, 0);
        std::vector<Vector>& lit = scratchLit;
        std::vector<uint8_t>& litDone = scratchLitDone;
        lit.resize(meshWorld.size());
        litDone.assign(meshWorld.size(), 0);
        std::vector<Vector>& fogAt = scratchFogLit;
        if (fogged) {
            int32_t leaf = *reinterpret_cast<int32_t*>(actor::Region(lightActor) + sizeof(UObject*));
            if (leaf < 0) {
                leaf = pointLeaf(actor::Location(lightActor));
            }
            size_t count = leaf >= 0 && static_cast<size_t>(leaf) < levelData.leafVolumetrics.size() && levelData.leafStamp[static_cast<size_t>(leaf)] == visibleStamp ? static_cast<size_t>(levelData.leafVolumetrics[static_cast<size_t>(leaf)]) : volumetrics.size();
            fogAt.resize(meshWorld.size());
            for (size_t i = 0; i < meshWorld.size(); i++) {
                fogAt[i] = meshFog(meshWorld[i], view, count);
            }
        }
        for (const MeshTriangle& triangle : *triangleSource) {
            uint32_t flags = triangle.polyFlags | actorFlags;
            if (skeletalMesh) {
                flags &= ~PF_Environment;
            }
            if (flags & PF_Invisible) {
                continue;
            }
            if (triangle.textureIndex < 0 || static_cast<size_t>(triangle.textureIndex) >= environmentSlot) {
                continue;
            }
            size_t slot = static_cast<size_t>(triangle.textureIndex);
            if ((flags & PF_Environment) || !skins[slot]) {
                slot = environmentSlot;
            }
            const GLTextureInfo* info = slotInfo[slot];
            if (!info) {
                continue;
            }
            flags |= slotFlags[slot];
            if (flags & PF_Environment) {
                cacheable = false;
            }
            if ((flags & PF_AlphaBlend) && (flags & (PF_Translucent | PF_Modulated))) {
                flags &= ~PF_AlphaBlend;
            }
            if (flags & PF_AlphaBlend) {
                flags &= ~PF_Masked;
            }
            bool translucent = isTranslucentFlags(flags) || (flags & PF_AlphaBlend);
            bool unlit = unlitActor || (flags & PF_Unlit);
            uint32_t mode = (flags & PF_Translucent) ? 1u : ((flags & PF_Modulated) ? 2u : ((flags & PF_Masked) ? 3u : ((flags & PF_AlphaBlend) ? ((flags & PF_TwoSided) ? 5u : 4u) : 0u)));
            size_t& groupNumber = slotGroups[slot * 6 + mode];
            if (!groupNumber) {
                EmittedGroup group;
                group.key = (static_cast<uint64_t>(info->id) << 8) | mode;
                group.flags = flags & (PF_Translucent | PF_Modulated | PF_Masked | PF_AlphaBlend);
                group.texture = info->id;
                group.translucent = translucent;
                group.cull = mode == 4;
                cache.groups.push_back(std::move(group));
                groupNumber = cache.groups.size();
            }
            std::vector<GLVertex>& out = cache.groups[groupNumber - 1].vertices;
            for (int i = 0; i < 3; i++) {
                size_t index = static_cast<size_t>(triangle.vertex[i]);
                if (index >= meshWorld.size()) {
                    break;
                }
                if (!litDone[index]) {
                    lit[index] = singleColor ? actorColor : adjustColor(meshVertexLight(meshLights, meshNormals[index], ambient, scaleGlow), colorAdjust);
                    litDone[index] = 1;
                }
                Vector position = meshWorld[index];
                if (fat) {
                    position += meshNormals[index].normal() * fatOffset;
                }
                GLVertex vertex;
                vertex.x = position.x;
                vertex.y = position.y;
                vertex.z = position.z;
                vertex.u = triangle.u[i] / 256.0f;
                vertex.v = triangle.v[i] / 256.0f;
                if (flags & PF_Environment) {
                    environmentCoords(meshWorld[index] - view.location, meshNormals[index], vertex.u, vertex.v);
                }
                Vector color = unlit ? unlitColor : lit[index];
                vertex.r = std::min(color.x, 1.0f);
                vertex.g = std::min(color.y, 1.0f);
                vertex.b = std::min(color.z, 1.0f);
                vertex.a = alpha;
                if (fogged) {
                    vertex.fogR = fogAt[index].x;
                    vertex.fogG = fogAt[index].y;
                    vertex.fogB = fogAt[index].z;
                }
                applyZoneFog(vertex, zoneFog);
                out.push_back(vertex);
            }
        }
        cache.cacheable = cacheable;
        appendGroups(cache.groups, meshBatches);
    }

    Vector SceneRenderer::spriteLight(UObject* actor)
    {
        float scaleGlow = actor::ScaleGlow(actor);
        if (actor::bUnlit(actor)) {
            return Vector { scaleGlow, scaleGlow, scaleGlow };
        }
        selectActorLights(actor);
        shadow::updateVector(actor, actorLightPrepared);
        Vector ambient = zoneAmbient(zoneForActor(actor));
        uint8_t glow = actor::AmbientGlow(actor);
        float glowAmount = glow == 255 ? static_cast<float>(0.25 + 0.2 * std::sin(8.0 * currentTime)) : static_cast<float>(glow) / 255.0f;
        ambient += Vector { glowAmount, glowAmount, glowAmount };
        std::vector<MeshLight>& spriteLights = scratchMeshLights;
        spriteLights.clear();
        Vector centre = actor::Location(actor);
        for (size_t i = 0; i < actorLights.size(); i++) {
            if (actorLights[i] != actor) {
                spriteLights.push_back(meshLight(actorLightPrepared[i].location, actorLightPrepared[i].radius, actorLightColors[i], centre));
            }
        }
        uint8_t zoneNumber = *(actor::Region(actor) + sizeof(UObject*) + sizeof(int32_t));
        Vector lit = zoneNumber == 0 ? Vector { std::min(0.5f * scaleGlow + ambient.x, 1.0f), std::min(0.5f * scaleGlow + ambient.y, 1.0f), std::min(0.5f * scaleGlow + ambient.z, 1.0f) } : meshCentreLight(spriteLights, ambient, scaleGlow);
        return adjustColor(lit, actor::ColorAdjust(actor)) * (0.75f * scaleGlow);
    }

    void SceneRenderer::drawSprite(UObject* actor, const View& view)
    {
        UTexture* texture = asTexture(actor::Texture(actor));
        if (!texture) {
            return;
        }
        texture = actor::DrawType(actor) == DT_SpriteAnimOnce ? spriteAnimOnceFrame(actor, texture) : device->animated(texture);
        if (!texture || actor::Style(actor) == STY_None) {
            return;
        }
        uint32_t flags = styleFlags(actor) | (textureFlags(texture) & (PF_Masked | PF_Translucent | PF_Modulated));
        if ((flags & PF_AlphaBlend) && (flags & (PF_Translucent | PF_Modulated))) {
            flags &= ~PF_AlphaBlend;
        }
        if ((flags & PF_AlphaBlend) && !device->blendsAlpha()) {
            if (actor::AlphaScale(actor) < 0.5f) {
                return;
            }
            flags &= ~PF_AlphaBlend;
        }
        if (flags & PF_AlphaBlend) {
            flags &= ~PF_Masked;
        }
        float alpha = (flags & PF_AlphaBlend) ? actor::AlphaScale(actor) : 1.0f;
        const GLTextureInfo* info = device->texture(texture);
        if (!info) {
            return;
        }
        float drawScale = actor::DrawScale(actor);
        float halfWidth = static_cast<float>(texture->width()) * drawScale * 0.5f;
        float halfHeight = static_cast<float>(texture->height()) * drawScale * 0.5f;
        bool vertical = actor::DrawType(actor) == DT_VerticalSprite;
        Vector center = actor::Location(actor) + (vertical ? Vector {} : actor::PrePivot(actor));
        Vector right = view.axes.yAxis;
        Vector up = view.axes.zAxis;
        float pull = 1.0f;
        if (vertical) {
            Coords axes = rotationAxes(actor::Rotation(actor));
            right = axes.yAxis;
            up = axes.zAxis;
            flags |= PF_TwoSided;
        } else {
            float depth = (actor::Location(actor) - view.location).dot(view.axes.xAxis);
            float projectForward = actor::SpriteProjForward(actor);
            float pulled = depth - projectForward;
            if (!(pulled > -2.0f * projectForward)) {
                return;
            }
            pull = depth > 0 ? std::max(pulled, sceneNearPlane * 1.01f) / depth : 1.0f;
        }
        Vector light = vertical && actor::bUnlit(actor) ? Vector { 1, 1, 1 } : spriteLight(actor);
        Vector corners[4] = {
            center - right * halfWidth + up * halfHeight,
            center + right * halfWidth + up * halfHeight,
            center + right * halfWidth - up * halfHeight,
            center - right * halfWidth - up * halfHeight,
        };
        for (Vector& corner : corners) {
            corner = view.location + (corner - view.location) * pull;
        }
        float us[4] = { 0, 1, 1, 0 };
        float vs[4] = { 0, 0, 1, 1 };
        std::vector<GLVertex> vertices;
        ZoneFog zoneFog = actorDistanceFog(actor);
        const int order[6] = { 0, 1, 2, 0, 2, 3 };
        for (int index : order) {
            GLVertex vertex;
            vertex.x = corners[index].x;
            vertex.y = corners[index].y;
            vertex.z = corners[index].z;
            vertex.u = us[index];
            vertex.v = vs[index];
            vertex.r = std::min(light.x, 1.0f);
            vertex.g = std::min(light.y, 1.0f);
            vertex.b = std::min(light.z, 1.0f);
            vertex.a = alpha;
            applyZoneFog(vertex, zoneFog);
            vertices.push_back(vertex);
        }
        if (isTranslucentFlags(flags) || (flags & PF_AlphaBlend)) {
            queueTranslucent(flags & (PF_Translucent | PF_Modulated | PF_Masked | PF_AlphaBlend), info->id, {}, vertices);
            return;
        }
        applyBlend(flags, 0);
        device->setCulling(false);
        device->bindTexture(info->id);
        device->draw(vertices, Primitive::Triangles);
        device->setBlend(BlendMode::Opaque);
    }

    void SceneRenderer::drawParticleSystem(UObject* actor, const View& view)
    {
        if (!asTexture(particlesystem::ParticleTexture(actor, 0))) {
            return;
        }
        particlesystem::LastTime(actor) = particlesystem::CurrentTime(actor);
        uint8_t& count = particlesystem::ParticleCount(actor);
        if (count == 0) {
            return;
        }
        if (count > particles::MaxRenderParticles) {
            count = static_cast<uint8_t>(particles::MaxRenderParticles);
        }
        particlesystem::HasValidCoords(actor) = 1;
        uint8_t spriteType = particlesystem::ParticleSpriteType(actor);
        ZoneFog zoneFog = actorDistanceFog(actor);
        bool relative = particlesystem::bRelativeToSystem(actor) && !actor::bCarriedItem(actor);
        float glow = actor::ScaleGlow(actor);
        Vector systemLocation = actor::Location(actor);
        std::map<std::pair<uint32_t, uint32_t>, std::vector<GLVertex>> groups;
        const Particle* list = particles::particlesOf(actor);
        for (int32_t i = 0; i < static_cast<int32_t>(count); i++) {
            const Particle& particle = list[i];
            if (!particle.valid || particle.textureIndex >= 4) {
                continue;
            }
            UTexture* texture = asTexture(particlesystem::ParticleTexture(actor, particle.textureIndex));
            if (!texture) {
                continue;
            }
            texture = device->animated(texture);
            const GLTextureInfo* info = texture ? device->texture(texture) : nullptr;
            if (!info) {
                continue;
            }
            Vector location = relative ? systemLocation + particle.location : particle.location;
            if ((location - view.location).dot(view.axes.xAxis) < particleNearDepth) {
                continue;
            }
            uint32_t flags = (textureFlags(texture) & (PF_Masked | PF_Translucent | PF_Modulated)) | particleStyleFlags(particle.style);
            if (flags & PF_AlphaBlend) {
                flags &= ~PF_Masked;
            }
            Plane color { glow * particle.alpha.x, glow * particle.alpha.y, glow * particle.alpha.z, 1 };
            if (particle.style == STY_AlphaBlend) {
                color = { glow, glow, glow, particle.alpha.x };
                if ((flags & PF_AlphaBlend) && !device->blendsAlpha()) {
                    if (particle.alpha.x < 0.5f) {
                        continue;
                    }
                    flags &= ~PF_AlphaBlend;
                    color.w = 1;
                }
            }
            if (spriteType != particles::PSPRITE_Normal && (flags & PF_Modulated)) {
                color.x = 1;
                color.y = 1;
                color.z = 1;
            }
            Vector corners[4];
            float us[4] = { 0, 1, 1, 0 };
            float vs[4] = { 0, 0, 1, 1 };
            if (spriteType == particles::PSPRITE_QuadUV) {
                for (int k = 0; k < 4; k++) {
                    corners[k] = particle.points[k];
                }
                us[0] = particle.u0;
                us[1] = particle.u1;
                us[2] = particle.u1;
                us[3] = particle.u0;
                vs[0] = particle.v0;
                vs[1] = particle.v0;
                vs[2] = particle.v1;
                vs[3] = particle.v1;
            } else {
                Vector right = view.axes.yAxis;
                Vector up = view.axes.zAxis;
                if (spriteType == particles::PSPRITE_Vertical || spriteType == particles::PSPRITE_Flat) {
                    Rotator rotation { 16384, 0, actor::Rotation(actor).yaw };
                    if (spriteType == particles::PSPRITE_Vertical) {
                        Vector toView = view.location - location;
                        toView.z = 0;
                        rotation = vectorRotation(toView);
                    }
                    Coords axes = rotationAxes(rotation);
                    right = axes.yAxis;
                    up = axes.zAxis;
                }
                Vector halfWidth = right * (static_cast<float>(texture->width()) * particle.xScale * 0.5f);
                Vector halfHeight = up * (static_cast<float>(texture->height()) * particle.yScale * 0.5f);
                corners[0] = location - halfWidth + halfHeight;
                corners[1] = location + halfWidth + halfHeight;
                corners[2] = location + halfWidth - halfHeight;
                corners[3] = location - halfWidth - halfHeight;
            }
            std::vector<GLVertex>& out = groups[{ info->id, flags }];
            const int order[6] = { 0, 1, 2, 0, 2, 3 };
            for (int index : order) {
                GLVertex vertex;
                vertex.x = corners[index].x;
                vertex.y = corners[index].y;
                vertex.z = corners[index].z;
                vertex.u = us[index];
                vertex.v = vs[index];
                vertex.r = std::clamp(color.x, 0.0f, 1.0f);
                vertex.g = std::clamp(color.y, 0.0f, 1.0f);
                vertex.b = std::clamp(color.z, 0.0f, 1.0f);
                vertex.a = std::clamp(color.w, 0.0f, 1.0f);
                applyZoneFog(vertex, zoneFog);
                out.push_back(vertex);
            }
        }
        for (const auto& [key, vertices] : groups) {
            if (key.second & (PF_Translucent | PF_Modulated | PF_AlphaBlend)) {
                queueTranslucent(key.second, key.first, {}, vertices);
                continue;
            }
            applyBlend(key.second, 0);
            device->setCulling(false);
            device->bindTexture(key.first);
            device->draw(vertices, Primitive::Triangles);
        }
        device->setBlend(BlendMode::Opaque);
    }

    void SceneRenderer::drawMoverSurfaces(UObject* actor, UModel* brush)
    {
        BrushDrawCache& cache = brushDrawCaches[actor];
        bool polyobj = actor->isA(namePolyobj);
        const std::vector<Poly>& polys = brushRenderPolys(actor, brush);
        if (!cache.frameModel || cache.brush != brush) {
            cache.frameModel = brushFrameModel(brush, polys);
            cache.atlas.reset();
            cache.valid = false;
        }
        UModel* frame = cache.frameModel.get();
        LightmapTransform transform;
        transform.identity = false;
        transform.location = actor::Location(actor);
        transform.rotation = rotationAxes(actor::Rotation(actor));
        transform.scale = scaleOf(brush::MainScale(actor));
        transform.prePivot = actor::PrePivot(actor);
        Rotator actorRotation = actor::Rotation(actor);
        bool moved = !cache.valid || cache.brush != brush || !sameVector(cache.location, transform.location) || cache.rotation.pitch != actorRotation.pitch || cache.rotation.yaw != actorRotation.yaw || cache.rotation.roll != actorRotation.roll || !sameVector(cache.scale, transform.scale) || !sameVector(cache.prePivot, transform.prePivot);
        bool dynamicMover = polyobj ? polyobj::bDynamicLightPolyobj(actor) != 0 : mover::bDynamicLightMover(actor) != 0;
        if (!cache.atlas) {
            cache.atlas = std::make_shared<LightmapAtlas>(*device, 256);
        }
        std::vector<Vector> surfAmbient;
        if (moved || !cache.atlas->built()) {
            surfAmbient.resize(polys.size());
            for (size_t i = 0; i < polys.size(); i++) {
                if (polys[i].vertices.empty()) {
                    continue;
                }
                Vector center;
                for (const Vector& point : polys[i].vertices) {
                    center += point;
                }
                center = transform.point(center / static_cast<float>(polys[i].vertices.size()));
                surfAmbient[i] = zoneAmbient(zoneActor(pointZone(center + transform.direction(polys[i].normal) * 2.0f)));
            }
        }
        if (!cache.atlas->built() || moved) {
            if (dynamicMover) {
                std::vector<UObject*> moverLights;
                UModel* model = levelData.model;
                int32_t leaf = *reinterpret_cast<int32_t*>(actor::Region(actor) + sizeof(UObject*));
                if (leaf < 0) {
                    leaf = pointLeaf(transform.location);
                }
                if (model && leaf >= 0 && static_cast<size_t>(leaf) < model->leaves.size() && model->leaves[static_cast<size_t>(leaf)].permeating >= 0) {
                    for (size_t i = static_cast<size_t>(model->leaves[static_cast<size_t>(leaf)].permeating); i < model->lights.size() && model->lights[i]; i++) {
                        UObject* light = model->lights[i];
                        if (light->data && actor::bAffectWorld(light) && (actor::bSpecialLit(light) != 0) == (actor::bSpecialLit(actor) != 0)) {
                            moverLights.push_back(light);
                        }
                    }
                }
                cache.atlas->build(frame, surfAmbient, static_cast<float>(time), transform, &moverLights);
            } else {
                cache.atlas->build(frame, surfAmbient, static_cast<float>(time), transform, nullptr);
            }
            moved = true;
        }
        if (cache.litFrame != static_cast<uint64_t>(lightFrameCount)) {
            cache.litFrame = static_cast<uint64_t>(lightFrameCount);
            cache.atlas->animate(frame, static_cast<float>(time), static_cast<uint64_t>(lightFrameCount), [](size_t, uint64_t) {
                return true;
            });
        }
        if (moved) {
            cache.valid = true;
            cache.brush = brush;
            cache.location = transform.location;
            cache.rotation = actorRotation;
            cache.scale = transform.scale;
            cache.prePivot = transform.prePivot;
            cache.groups.clear();
            std::unordered_map<uint64_t, size_t> groupIndex;
            std::vector<GLVertex> polygon;
            for (size_t polyNumber = 0; polyNumber < polys.size(); polyNumber++) {
                const Poly& poly = polys[polyNumber];
                const BspSurf& surf = frame->surfs[polyNumber];
                if (poly.vertices.size() < 3) {
                    continue;
                }
                UTexture* texture = asTexture(poly.texture);
                uint32_t flags = poly.polyFlags | textureFlags(texture);
                if (flags & (PF_Invisible | PF_Portal)) {
                    continue;
                }
                const GLTextureInfo* info = device->texture(device->animated(texture));
                float texWidth = texture && texture->width() ? static_cast<float>(texture->width()) : 64.0f;
                float texHeight = texture && texture->height() ? static_cast<float>(texture->height()) : 64.0f;
                float drawScale = texture && texture->data && texture::DrawScale(texture) > 0 ? texture::DrawScale(texture) : 1.0f;
                Vector normal = transform.direction(poly.normal);
                bool unlit = (flags & (PF_Unlit | PF_Modulated)) != 0;
                ZoneFog fog = polyobj || !(flags & (PF_Translucent | PF_Modulated | PF_AlphaBlend)) ? zoneDistanceFog(zoneForActor(actor)) : ZoneFog {};
                int32_t page = -1;
                polygon.clear();
                for (const Vector& point : poly.vertices) {
                    Vector world = transform.point(point);
                    GLVertex vertex;
                    vertex.x = world.x;
                    vertex.y = world.y;
                    vertex.z = world.z;
                    vertex.nx = normal.x;
                    vertex.ny = normal.y;
                    vertex.nz = normal.z;
                    vertex.u = ((point - poly.base).dot(poly.textureU) + static_cast<float>(poly.panU)) / (texWidth * drawScale);
                    vertex.v = ((point - poly.base).dot(poly.textureV) + static_cast<float>(poly.panV)) / (texHeight * drawScale);
                    if (!unlit) {
                        int32_t vertexPage = -1;
                        if (cache.atlas->texCoord(frame, surf, point, vertex.lu, vertex.lv, vertexPage)) {
                            page = vertexPage;
                        } else {
                            const Vector& surfaceAmbient = surfAmbient[polyNumber];
                            Vector level = Vector { std::floor(surfaceAmbient.x * 64.0f), std::floor(surfaceAmbient.y * 64.0f), std::floor(surfaceAmbient.z * 64.0f) } * (2.0f / 127.5f);
                            vertex.r = std::min(level.x, 1.0f);
                            vertex.g = std::min(level.y, 1.0f);
                            vertex.b = std::min(level.z, 1.0f);
                        }
                    }
                    applyZoneFog(vertex, fog);
                    polygon.push_back(vertex);
                }
                uint32_t textureId = info ? info->id : device->whiteTexture();
                uint32_t lightmapId = page >= 0 ? cache.atlas->texture(page) : 0;
                bool translucent = isTranslucentFlags(flags);
                uint32_t mode = translucent ? ((flags & PF_Translucent) ? 1u : 2u) : ((flags & PF_Masked) ? 3u : 0u);
                uint64_t key = (1ull << 63) | (static_cast<uint64_t>(textureId) << 32) | (static_cast<uint64_t>(lightmapId) << 8) | mode | ((flags & PF_TwoSided) ? 4u : 0u) | ((flags & PF_SpecialLit) ? 8u : 0u);
                auto found = groupIndex.find(key);
                if (found == groupIndex.end()) {
                    EmittedGroup group;
                    group.key = key;
                    group.flags = flags & (PF_Translucent | PF_Modulated | PF_Masked | PF_SpecialLit);
                    group.texture = textureId;
                    group.translucent = translucent;
                    group.lightmap = lightmapId;
                    group.cull = !(flags & PF_TwoSided);
                    cache.groups.push_back(std::move(group));
                    found = groupIndex.emplace(key, cache.groups.size() - 1).first;
                }
                std::vector<GLVertex>& out = cache.groups[found->second].vertices;
                for (size_t i = 1; i + 1 < polygon.size(); i++) {
                    out.push_back(polygon[0]);
                    out.push_back(polygon[i]);
                    out.push_back(polygon[i + 1]);
                }
            }
            for (EmittedGroup& group : cache.groups) {
                group.low = { group.vertices[0].x, group.vertices[0].y, group.vertices[0].z };
                group.high = group.low;
                for (const GLVertex& vertex : group.vertices) {
                    group.low = { std::min(group.low.x, vertex.x), std::min(group.low.y, vertex.y), std::min(group.low.z, vertex.z) };
                    group.high = { std::max(group.high.x, vertex.x), std::max(group.high.y, vertex.y), std::max(group.high.z, vertex.z) };
                }
            }
        }
        for (EmittedGroup& group : cache.groups) {
            group.lightMask = group.lightmap && !polyobj ? litMask(group.low, group.high, (group.flags & PF_SpecialLit) != 0) : 0;
        }
        appendGroups(cache.groups, brushBatches);
    }

    std::shared_ptr<UModel> brushFrameModel(UModel* brush, const std::vector<Poly>& polys)
    {
        std::shared_ptr<UModel> model = std::make_shared<UModel>();
        UModel& frame = *model;
        frame.lightMaps = brush->lightMaps;
        frame.lightBits = brush->lightBits;
        frame.lights = brush->lights;
        for (const Poly& poly : polys) {
            BspSurf surf;
            surf.pBase = static_cast<int32_t>(frame.points.size());
            frame.points.push_back(poly.base);
            surf.vNormal = static_cast<int32_t>(frame.vectors.size());
            frame.vectors.push_back(poly.normal);
            surf.vTextureU = static_cast<int32_t>(frame.vectors.size());
            frame.vectors.push_back(poly.textureU);
            surf.vTextureV = static_cast<int32_t>(frame.vectors.size());
            frame.vectors.push_back(poly.textureV);
            surf.lightMap = poly.brushPoly >= 0 && static_cast<size_t>(poly.brushPoly) < brush->lightMaps.size() ? poly.brushPoly : -1;
            surf.texture = poly.texture;
            surf.polyFlags = poly.polyFlags;
            surf.panU = poly.panU;
            surf.panV = poly.panV;
            frame.surfs.push_back(surf);
        }
        return model;
    }

    std::vector<Poly> polyobjRenderPolys(UModel* level, UModel* brush)
    {
        std::vector<Poly> polys;
        for (const BspNode& node : brush->nodes) {
            if (node.numVertices < 3 || node.surf < 0 || static_cast<size_t>(node.surf) >= level->surfs.size()) {
                continue;
            }
            const BspSurf& surf = level->surfs[static_cast<size_t>(node.surf)];
            if (surf.brushPoly < 0 || static_cast<size_t>(surf.brushPoly) >= brush->polys->polys.size()) {
                continue;
            }
            Poly poly = brush->polys->polys[static_cast<size_t>(surf.brushPoly)];
            poly.texture = surf.texture;
            poly.polyFlags = surf.polyFlags;
            poly.brushPoly = surf.lightMap;
            poly.vertices.clear();
            for (int32_t i = 0; i < node.numVertices; i++) {
                poly.vertices.push_back(brush->points[static_cast<size_t>(brush->verts[static_cast<size_t>(node.vertPool + i)].vertex)]);
            }
            polys.push_back(std::move(poly));
        }
        return polys;
    }

    const std::vector<Poly>& SceneRenderer::brushRenderPolys(UObject* actor, UModel* brush)
    {
        UModel* model = levelData.model;
        if (!actor->isA(namePolyobj) || !model) {
            return brush->polys->polys;
        }
        auto found = polyobjPolys.find(brush);
        if (found == polyobjPolys.end()) {
            found = polyobjPolys.emplace(brush, polyobjRenderPolys(model, brush)).first;
        }
        return found->second;
    }

    void SceneRenderer::drawBrush(UObject* actor, const View&)
    {
        UObject* brushObject = actor::Brush(actor);
        if (!brushObject) {
            return;
        }
        objects::ensureLoaded(brushObject);
        UModel* brush = cast<UModel>(brushObject);
        if (!brush) {
            return;
        }
        if (!brush->polys) {
            return;
        }
        objects::ensureLoaded(brush->polys);
        if (!brush->lightMaps.empty() || actor->isA(namePolyobj)) {
            drawMoverSurfaces(actor, brush);
            return;
        }
        objects::ensureLoaded(brush->polys);
        Rotator actorRotation = actor::Rotation(actor);
        Vector scale = scaleOf(brush::MainScale(actor));
        Vector location = actor::Location(actor);
        Vector prePivot = actor::PrePivot(actor);
        BrushDrawCache& cache = brushDrawCaches[actor];
        bool sameGeometry = cache.valid && cache.brush == brush && sameVector(cache.location, location) && cache.rotation.pitch == actorRotation.pitch && cache.rotation.yaw == actorRotation.yaw && cache.rotation.roll == actorRotation.roll && sameVector(cache.scale, scale) && sameVector(cache.prePivot, prePivot);
        Coords rotation = rotationAxes(actorRotation);
        std::vector<Vector>& world = scratchWorld;
        auto computePoints = [&]() {
            world.clear();
            for (const Poly& poly : brush->polys->polys) {
                for (const Vector& point : poly.vertices) {
                    world.push_back(location + untransformVector(rotation, (point - prePivot) * scale));
                }
            }
        };
        Vector sphereCenter = cache.sphereCenter;
        float sphereRadius = cache.sphereRadius;
        if (!sameGeometry) {
            computePoints();
            boundingSphere(world, sphereCenter, sphereRadius);
        }
        selectLights(sphereCenter, sphereRadius);
        UObject* zone = zoneForActor(actor);
        Vector ambient = zoneAmbient(zone);

        std::vector<const GLTextureInfo*>& polyInfo = scratchSlotInfo;
        std::vector<uint32_t>& polyFlags = scratchSlotFlags;
        polyInfo.assign(brush->polys->polys.size(), nullptr);
        polyFlags.assign(brush->polys->polys.size(), 0);
        for (size_t i = 0; i < brush->polys->polys.size(); i++) {
            const Poly& poly = brush->polys->polys[i];
            if (poly.vertices.size() < 3) {
                continue;
            }
            UTexture* texture = device->animated(asTexture(poly.texture));
            polyFlags[i] = poly.polyFlags | textureFlags(texture);
            if (polyFlags[i] & (PF_Invisible | PF_Portal)) {
                continue;
            }
            polyInfo[i] = device->texture(texture);
        }

        bool cacheable = true;
        for (const PreparedLight& prepared : actorLightPrepared) {
            if (timeDependentEffect(prepared.effect)) {
                cacheable = false;
            }
        }
        if (cacheable && sameGeometry && cache.polyInfo == polyInfo && cache.polyFlags == polyFlags && sameVector(cache.ambient, ambient) && sameLights(cache.lights)) {
            appendGroups(cache.groups, brushBatches);
            return;
        }
        if (sameGeometry) {
            computePoints();
        }
        cache.valid = cacheable;
        cache.location = location;
        cache.rotation = actorRotation;
        cache.scale = scale;
        cache.prePivot = prePivot;
        cache.brush = brush;
        cache.sphereCenter = sphereCenter;
        cache.sphereRadius = sphereRadius;
        cache.polyInfo = polyInfo;
        cache.polyFlags = polyFlags;
        cache.ambient = ambient;
        storeLights(cache.lights);
        cache.groups.clear();
        std::unordered_map<uint64_t, size_t> groupIndex;

        size_t pointIndex = 0;
        std::vector<GLVertex> polygon;
        for (size_t polyNumber = 0; polyNumber < brush->polys->polys.size(); polyNumber++) {
            const Poly& poly = brush->polys->polys[polyNumber];
            size_t firstPoint = pointIndex;
            pointIndex += poly.vertices.size();
            if (poly.vertices.size() < 3) {
                continue;
            }
            uint32_t flags = polyFlags[polyNumber];
            if (flags & (PF_Invisible | PF_Portal)) {
                continue;
            }
            const GLTextureInfo* info = polyInfo[polyNumber];
            float width = info ? static_cast<float>(info->width) : 64.0f;
            float height = info ? static_cast<float>(info->height) : 64.0f;
            UTexture* texture = device->animated(asTexture(poly.texture));
            float drawScale = texture && texture->data ? std::max(texture::DrawScale(texture), 0.01f) : 1.0f;
            Vector normal = untransformVector(rotation, poly.normal * Vector { 1.0f / scale.x, 1.0f / scale.y, 1.0f / scale.z }).normal();
            bool translucent = isTranslucentFlags(flags);
            bool unlit = (flags & PF_Unlit) || translucent;
            ZoneFog fog = (flags & (PF_Translucent | PF_Modulated | PF_AlphaBlend)) ? ZoneFog {} : zoneDistanceFog(zone);
            polygon.clear();
            for (size_t i = 0; i < poly.vertices.size(); i++) {
                const Vector& point = poly.vertices[i];
                const Vector& worldPoint = world[firstPoint + i];
                GLVertex vertex;
                vertex.x = worldPoint.x;
                vertex.y = worldPoint.y;
                vertex.z = worldPoint.z;
                vertex.u = ((point - poly.base).dot(poly.textureU) + static_cast<float>(poly.panU)) / (width * drawScale);
                vertex.v = ((point - poly.base).dot(poly.textureV) + static_cast<float>(poly.panV)) / (height * drawScale);
                Vector color = actorLight(actor, worldPoint, normal, ambient, 1.0f, unlit);
                vertex.r = color.x;
                vertex.g = color.y;
                vertex.b = color.z;
                applyZoneFog(vertex, fog);
                polygon.push_back(vertex);
            }
            uint32_t textureId = info ? info->id : device->whiteTexture();
            uint32_t mode = translucent ? ((flags & PF_Translucent) ? 1u : 2u) : ((flags & PF_Masked) ? 3u : 0u);
            uint64_t key = (static_cast<uint64_t>(textureId) << 8) | mode;
            auto found = groupIndex.find(key);
            if (found == groupIndex.end()) {
                EmittedGroup group;
                group.key = key;
                group.flags = flags & (PF_Translucent | PF_Modulated | PF_Masked);
                group.texture = textureId;
                group.translucent = translucent;
                cache.groups.push_back(std::move(group));
                found = groupIndex.emplace(key, cache.groups.size() - 1).first;
            }
            std::vector<GLVertex>& out = cache.groups[found->second].vertices;
            for (size_t i = 1; i + 1 < polygon.size(); i++) {
                out.push_back(polygon[0]);
                out.push_back(polygon[i]);
                out.push_back(polygon[i + 1]);
            }
        }
        appendGroups(cache.groups, brushBatches);
    }

    void SceneRenderer::drawDecal(UObject* actor, const View& view)
    {
        if (!device->drawsDecals()) {
            return;
        }
        World* world = World::current;
        UModel* model = levelData.model;
        if (!world || !model || world->level != levelData.level) {
            return;
        }
        auto found = world->decals.pieces.find(actor);
        if (found == world->decals.pieces.end()) {
            return;
        }
        UTexture* texture = asTexture(actor::Texture(actor));
        if (!texture && levelData.levelInfo) {
            texture = asTexture(levelinfo::DefaultTexture(levelData.levelInfo));
        }
        texture = device->animated(texture);
        uint32_t flags = decalPolyFlags(actor);
        const GLTextureInfo* info = (flags & PF_Masked) ? device->maskedTexture(texture) : device->texture(texture);
        if (!info) {
            return;
        }
        ZoneFog fog = zoneDistanceFog(zoneForActor(actor));
        if (decalHiddenByFog(actor, fog)) {
            return;
        }
        bool unlit = (flags & PF_Modulated) || actor::bUnlit(actor);
        Vector ambient;
        if (!unlit) {
            selectLights(actor::Location(actor), actorRadius(actor));
            ambient = zoneAmbient(zoneForActor(actor));
        }
        float alpha = (flags & PF_AlphaBlend) ? actor::AlphaScale(actor) : 1.0f;
        float textureScale = texture->data && texture::DrawScale(texture) > 0 ? texture::DrawScale(texture) : 1.0f;
        float uSize = static_cast<float>(info->width) * textureScale * actor::DrawScale(actor);
        float vSize = static_cast<float>(info->height) * textureScale * actor::DrawScale(actor);
        std::vector<GLVertex> vertices;
        std::vector<Vector> polygon;
        for (const DecalPiece& piece : found->second) {
            if (piece.surf < 0 || static_cast<size_t>(piece.surf) >= model->surfs.size()) {
                continue;
            }
            const BspSurf& surf = model->surfs[static_cast<size_t>(piece.surf)];
            Vector normal = model->vectors[static_cast<size_t>(surf.vNormal)].normal();
            Vector lift = normal * 0.75f;
            Vector uAxis = (piece.vertices[1] - piece.vertices[0]).normal();
            Vector vAxis = (piece.vertices[3] - piece.vertices[0]).normal();
            const std::vector<int32_t>& nodes = piece.nodes.empty() ? levelData.surfNodes[static_cast<size_t>(piece.surf)] : piece.nodes;
            for (int32_t node : nodes) {
                const BspNode& bspNode = model->nodes[static_cast<size_t>(node)];
                if (bspNode.numVertices < 3 || !(view.zoneMask & (1ull << (bspNode.zone[1] & 63)))) {
                    continue;
                }
                polygon.assign(piece.vertices.begin(), piece.vertices.end());
                clipDecalToNode(model, node, polygon);
                if (polygon.size() < 3) {
                    continue;
                }
                for (size_t i = 1; i + 1 < polygon.size(); i++) {
                    for (size_t corner : { size_t { 0 }, i, i + 1 }) {
                        const Vector& point = polygon[corner];
                        GLVertex vertex;
                        vertex.x = point.x + lift.x;
                        vertex.y = point.y + lift.y;
                        vertex.z = point.z + lift.z;
                        vertex.u = (point - piece.vertices[0]).dot(uAxis) / uSize;
                        vertex.v = (point - piece.vertices[0]).dot(vAxis) / vSize;
                        Vector color = actorLight(actor, point, normal, ambient, actor::ScaleGlow(actor), unlit);
                        vertex.r = color.x;
                        vertex.g = color.y;
                        vertex.b = color.z;
                        vertex.a = alpha;
                        applyZoneFog(vertex, fog);
                        vertices.push_back(vertex);
                    }
                }
            }
        }
        if (vertices.empty()) {
            return;
        }
        if (levelData.levelInfo) {
            decal::LastRenderedTime(actor) = levelinfo::TimeSeconds(levelData.levelInfo);
        }
        applyBlend(flags, 0);
        if (flags & PF_AlphaBlend) {
            device->setBlend(BlendMode::Alpha);
        }
        device->setCulling(false);
        device->setDepthWrite(false);
        device->bindTexture(info->id);
        device->draw(vertices, Primitive::Triangles);
        device->setBlend(BlendMode::Opaque);
    }

    bool SceneRenderer::rasterizeShadowMesh(UObject* actor, UTexture* texture, const shadow::Projection& projection, const View& view)
    {
        USkelModel* model = skeletal::geometryModel(actor);
        const SkelPose* pose = skeletal::pose(actor);
        if (!model || !pose || model->meshes.empty()) {
            return false;
        }
        const SkelMesh& mesh = model->meshes[std::min<size_t>(actor::SkelMesh(actor), model->meshes.size() - 1)];
        std::vector<std::array<uint16_t, 3>>& triangles = shadowTriangles[&mesh];
        if (triangles.empty()) {
            float savedPercent = actor::LODPercentMax(actor);
            actor::LODPercentMax(actor) = 0.3f;
            float shadowCurve = skeletal::lodCurve(actor, view.location);
            actor::LODPercentMax(actor) = savedPercent;
            const SkelDecimation& decimation = skeletal::decimate(mesh, skeletal::decimationLevel(actor, mesh, shadowCurve, 0, 1.0f, false));
            for (size_t index = 0; index < decimation.tris.size(); index++) {
                const SkelTriangle& tri = decimation.tris[index];
                if (decimation.removed[index] || (actor::SkelGroupFlags(actor, tri.group % SkelPolyGroups) & 1)) {
                    continue;
                }
                triangles.push_back({ tri.vertex[0], tri.vertex[1], tri.vertex[2] });
            }
            if (triangles.empty()) {
                return false;
            }
        }
        bool mirrored = actor::bMirrored(actor) != 0;
        skeletal::skin(*model, mesh, *pose, mirrored, skeletal::lodCurve(actor, view.location) < 0.1f, shadowWorld);
        for (const std::array<uint16_t, 3>& tri : triangles) {
            if (tri[0] >= shadowWorld.size() || tri[1] >= shadowWorld.size() || tri[2] >= shadowWorld.size()) {
                continue;
            }
            Vector corners[3] = { shadowWorld[tri[mirrored ? 2 : 0]], shadowWorld[tri[1]], shadowWorld[tri[mirrored ? 0 : 2]] };
            shadow::drawTriangle(texture, projection, corners);
        }
        return true;
    }

    void SceneRenderer::drawActorShadow(UObject* actor, const View& view)
    {
        if ((actor::Location(actor) - view.location).size() > 2000.0f) {
            return;
        }
        UModel* model = levelData.model;
        UTexture* texture = shadow::texture(actor);
        if (!model || !texture) {
            return;
        }
        shadow::Projection projection = shadow::projection(actor, texture);
        shadow::clear(texture);
        bool drawn = rasterizeShadowMesh(actor, texture, projection, view);
        USkelModel* skeleton = cast<USkelModel>(actor::Skeletal(actor));
        size_t joints = skeleton ? std::min<size_t>(skeleton->joints.size(), SkelMaxJoints) : 0;
        for (size_t j = 0; j < joints; j++) {
            UObject* child = actor::JointChild(actor, static_cast<int>(j));
            if (child && !child->destroyed && actor::Skeletal(child)) {
                drawn = rasterizeShadowMesh(child, texture, projection, view) || drawn;
            }
        }
        if (!drawn) {
            return;
        }
        std::vector<shadow::Polygon> polygons;
        shadow::receivers(*model, projection, view.location, polygons);
        const GLTextureInfo* info = polygons.empty() ? nullptr : device->texture(texture);
        if (!info) {
            return;
        }
        std::vector<GLVertex> vertices;
        for (const shadow::Polygon& polygon : polygons) {
            for (size_t i = 1; i + 1 < polygon.points.size(); i++) {
                for (size_t corner : { size_t { 0 }, i, i + 1 }) {
                    GLVertex vertex;
                    vertex.x = polygon.points[corner].x;
                    vertex.y = polygon.points[corner].y;
                    vertex.z = polygon.points[corner].z;
                    vertex.u = polygon.u[corner] / static_cast<float>(shadow::TextureSize);
                    vertex.v = polygon.v[corner] / static_cast<float>(shadow::TextureSize);
                    vertex.r = 1;
                    vertex.g = 1;
                    vertex.b = 1;
                    vertex.a = 1;
                    vertices.push_back(vertex);
                }
            }
        }
        if (vertices.empty()) {
            return;
        }
        applyBlend(PF_Modulated, 0);
        device->setCulling(false);
        device->setDepthWrite(false);
        device->bindTexture(info->id);
        device->draw(vertices, Primitive::Triangles);
        device->setBlend(BlendMode::Opaque);
        device->setDepthWrite(true);
    }

    void SceneRenderer::drawActor(UObject* actor, const View& view)
    {
        if (actor->isA(nameDecal)) {
            drawDecal(actor, view);
            return;
        }
        switch (actor::DrawType(actor)) {
        case DT_Mesh:
            drawMesh(actor, view);
            break;
        case DT_SkeletalMesh: {
            drawSkeletal(actor, view);
            if (actor->isA(namePawn) && (actor->isA(namePlayerPawn) ? options.playerShadows : options.actorShadows) == 1) {
                shadowUpdates.push_back(actor);
            }
            if (!objects::editorMode() && actor->isA(namePawn) && (actor->isA(namePlayerPawn) ? options.playerShadows : options.actorShadows) == 2) {
                drawActorShadow(actor, view);
            }
            USkelModel* skeleton = cast<USkelModel>(actor::Skeletal(actor));
            size_t joints = skeleton ? std::min<size_t>(skeleton->joints.size(), SkelMaxJoints) : 0;
            const SkelPose* pose = joints ? skeletal::pose(actor) : nullptr;
            World* world = World::of(actor);
            if (pose && world) {
                std::vector<Vector>& origins = world->renderedJoints[actor];
                origins.clear();
                for (const Coords& coords : pose->joints) {
                    origins.push_back(coords.origin);
                }
            }
            for (size_t j = 0; j < joints; j++) {
                UObject* child = actor::JointChild(actor, static_cast<int>(j));
                if (child && !child->destroyed && child->data && !actor::bHidden(child)) {
                    if (pose && world && j < pose->joints.size() && (actor::DrawType(child) == DT_Sprite || particles::isParticleSystem(child))) {
                        world->farMoveActor(child, pose->joints[j].origin, false, true);
                    }
                    setActorOrder(child, view);
                    drawActor(child, view);
                }
            }
            break;
        }
        case DT_Brush:
            if (actor->isA(nameMover) || actor->isA(namePolyobj)) {
                drawBrush(actor, view);
            }
            break;
        case DT_VerticalSprite:
            drawSprite(actor, view);
            break;
        default:
            if (drawnAsSprite(actor::DrawType(actor))) {
                drawSprite(actor, view);
            }
            break;
        }
        if (actor::DrawType(actor) != DT_None && particles::isParticleSystem(actor)) {
            drawParticleSystem(actor, view);
        }
    }

    void SceneRenderer::applyView(const View& view)
    {
        device->useShader();
        device->setViewProj(view.viewProj);
        device->setEye(view.location, view.axes.xAxis);
        device->setPan(0, 0);
        device->setTint(1, 1, 1, 1);
        device->setDepthTest(true);
        device->setDepthFunc(DepthFunc::LessEqual);
        device->setFrontFace(!view.mirrored);
    }

    bool SceneRenderer::actorBounds(UObject* actor, Vector& center, float& radius) const
    {
        UObject* meshObject = actor::Mesh(actor);
        if (!meshObject) {
            return false;
        }
        UMesh* mesh = cast<UMesh>(meshObject);
        auto found = meshDrawCaches.find({ actor, mesh, actor });
        if (found == meshDrawCaches.end() || !found->second.valid) {
            return false;
        }
        const MeshDrawCache& cache = found->second;
        if (cache.world.size() < 64) {
            return false;
        }
        float scale = std::abs(actor::DrawScale(actor));
        float cachedScale = cache.transform.x.size();
        if (cachedScale <= 0 || std::abs(scale - cachedScale) > cachedScale * 0.01f) {
            return false;
        }
        center = actor::Location(actor) + actor::PrePivot(actor);
        radius = cache.sphereRadius + (cache.sphereCenter - cache.transform.origin).size();
        return true;
    }

    void SceneRenderer::cullOccluded(std::vector<UObject*>& candidates, const View& view)
    {
        occlusionBoxes.clear();
        occlusionIssued.clear();
        occlusionUnconfirmed.clear();
        size_t kept = 0;
        for (UObject* actor : candidates) {
            bool keep = true;
            Vector center;
            float radius = 0;
            if (actor::DrawType(actor) == DT_Mesh && !actorBounds(actor, center, radius)) {
                occlusionUnconfirmed.insert(actor);
            } else if (actor::DrawType(actor) == DT_Mesh) {
                float extent = radius + 48.0f;
                Vector delta = view.location - center;
                float near = extent + 32.0f;
                bool inside = std::abs(delta.x) < near && std::abs(delta.y) < near && std::abs(delta.z) < near;
                if (!inside) {
                    OcclusionState& state = occlusion[actor];
                    bool visible = false;
                    if (state.pending && device->queryResult(state.query, visible)) {
                        state.pending = false;
                        state.known = true;
                        state.visible = visible;
                        state.resultFrame = state.issuedFrame;
                    }
                    keep = !state.known || state.visible || sceneFrame - state.resultFrame > 2;
                    if (keep && !(state.known && state.visible)) {
                        occlusionUnconfirmed.insert(actor);
                    }
                    if (!state.pending) {
                        if (!state.query) {
                            state.query = device->createQuery();
                        }
                        Vector low = center - Vector { extent, extent, extent };
                        Vector high = center + Vector { extent, extent, extent };
                        Vector corners[8];
                        for (int c = 0; c < 8; c++) {
                            corners[c] = { (c & 1) ? high.x : low.x, (c & 2) ? high.y : low.y, (c & 4) ? high.z : low.z };
                        }
                        static const int faces[36] = { 0, 1, 3, 0, 3, 2, 4, 6, 7, 4, 7, 5, 0, 4, 5, 0, 5, 1, 2, 3, 7, 2, 7, 6, 0, 2, 6, 0, 6, 4, 1, 5, 7, 1, 7, 3 };
                        for (int index : faces) {
                            GLVertex vertex;
                            vertex.x = corners[index].x;
                            vertex.y = corners[index].y;
                            vertex.z = corners[index].z;
                            occlusionBoxes.push_back(vertex);
                        }
                        state.issuedFrame = sceneFrame;
                        state.pending = true;
                        occlusionIssued.push_back(&state);
                    }
                }
            }
            if (keep) {
                candidates[kept++] = actor;
            }
        }
        candidates.resize(kept);
        if (occlusionIssued.empty()) {
            return;
        }
        device->setBlend(BlendMode::Opaque);
        device->setFlags(0);
        device->setDepthWrite(false);
        device->setColorWrite(false);
        device->setCulling(false);
        device->upload(occlusionBoxes);
        for (size_t i = 0; i < occlusionIssued.size(); i++) {
            device->beginQuery(occlusionIssued[i]->query);
            device->drawRange(static_cast<int32_t>(i * 36), 36);
            device->endQuery();
        }
        device->setColorWrite(true);
        device->setDepthWrite(true);
    }

    void SceneRenderer::stampZone(uint8_t zone)
    {
        if (levelData.model && levelData.levelInfo && zone < levelData.model->zones.size()) {
            levelData.model->zones[zone].lastRenderTime = levelinfo::TimeSeconds(levelData.levelInfo);
        }
    }

    bool SceneRenderer::zoneRecentlyRendered(UObject* actor) const
    {
        uint8_t zone = *(actor::Region(actor) + sizeof(UObject*) + sizeof(int32_t));
        if (zone == 0 || !levelData.model || !levelData.levelInfo) {
            return true;
        }
        float now = levelinfo::TimeSeconds(levelData.levelInfo);
        auto recent = [&](uint8_t number) {
            return number < levelData.model->zones.size() && now - levelData.model->zones[number].lastRenderTime < 2.0f;
        };
        if (!actor->isA(namePawn)) {
            return now - (zone < levelData.model->zones.size() ? levelData.model->zones[zone].lastRenderTime : 0.0f) <= 2.0f;
        }
        uint8_t foot = *(pawn::FootRegion(actor) + sizeof(UObject*) + sizeof(int32_t));
        uint8_t head = *(pawn::HeadRegion(actor) + sizeof(UObject*) + sizeof(int32_t));
        return recent(zone) || recent(foot) || recent(head);
    }

    void SceneRenderer::drawActors(const std::vector<UObject*>& actors, const View& view, UObject* viewActor, bool occlude)
    {
        device->useShader();
        device->setViewProj(view.viewProj);
        device->setEye(view.location, view.axes.xAxis);
        device->setPan(0, 0);
        device->setTint(1, 1, 1, 1);
        device->setDepthTest(true);
        device->setDepthFunc(DepthFunc::LessEqual);
        device->setFrontFace(!view.mirrored);
        bool behindView = false;
        if (viewActor && viewActor->data && viewActor->isA(namePawn)) {
            behindView = pawn::bBehindView(viewActor) != 0;
        }
        UObject* viewer = World::current ? World::current->player() : nullptr;
        bool viewerBehind = viewer && !viewer->destroyed && pawn::bBehindView(viewer);
        std::vector<UObject*>& candidates = scratchCandidates;
        candidates.clear();
        for (UObject* actor : actors) {
            if (!actor || actor->destroyed) {
                continue;
            }
            objects::ensureLoaded(actor);
            if (!actor->data || !actor->isA(nameActor)) {
                continue;
            }
            if (actor::bHidden(actor) || actor::bDeleteMe(actor) || actor::bCarriedItem(actor)) {
                continue;
            }
            if (actor == viewActor && !behindView) {
                continue;
            }
            Vector offset = actor::Location(actor) - view.location;
            float visibilityRadius = actor::VisibilityRadius(actor);
            if (visibilityRadius != 0 && !(visibilityRadius > std::sqrt(offset.x * offset.x + offset.y * offset.y))) {
                continue;
            }
            float visibilityHeight = actor::VisibilityHeight(actor);
            if (visibilityHeight != 0 && !(visibilityHeight > std::fabs(offset.z))) {
                continue;
            }
            bool ownedByViewer = false;
            for (UObject* owner = actor; owner && viewer; owner = actor::Owner(owner)) {
                if (owner == viewer) {
                    ownedByViewer = true;
                    break;
                }
            }
            if (actor::bOnlyOwnerSee(actor) && (!ownedByViewer || viewerBehind)) {
                continue;
            }
            if (actor::bOwnerNoSee(actor) && ownedByViewer && !viewerBehind) {
                continue;
            }
            bool beam = particles::isParticleSystem(actor) && particlesystem::ParticleType(actor) == 3;
            if (!objects::editorMode() && !beam && offset.size() - actor::SpriteProjForward(actor) > previousBspDepth * 1.25f) {
                continue;
            }
            bool special = hiddenInGame(actor);
            bool iterated = !special && iteratorHook && iteratorHook(actor, [&](UObject* item) {
                uint8_t itemZone = actorZone(item);
                if (itemZone != 0 && !(view.zoneMask & (1ull << (itemZone & 63)))) {
                    return;
                }
                float radius = actorRadius(item);
                if (!inFrustum(view, actor::Location(item), radius) || !sphereReachesVisible(view, actor::Location(item), radius)) {
                    return;
                }
                setActorOrder(item, view);
                drawActor(item, view);
            });
            if (iterated) {
                continue;
            }
            if (!objects::editorMode() && !beam && !zoneRecentlyRendered(actor)) {
                continue;
            }
            float radius = actorRadius(actor);
            if (!inFrustum(view, actor::Location(actor), radius) || !sphereReachesVisible(view, actor::Location(actor), radius)) {
                continue;
            }
            candidates.push_back(actor);
        }
        if (occlude) {
            cullOccluded(candidates, view);
        }
        std::vector<UObject*>& decals = scratchDecals;
        decals.clear();
        std::vector<UObject*>& sighted = scratchSighted;
        sighted.clear();
        for (UObject* actor : candidates) {
            if (actor::DrawType(actor) != DT_SkeletalMesh || hiddenInGame(actor)) {
                continue;
            }
            USkelModel* model = skeletal::geometryModel(actor);
            if (model && !model->meshes.empty()) {
                const SkelMesh& mesh = model->meshes[std::min<size_t>(actor::SkelMesh(actor), model->meshes.size() - 1)];
                skelApproximation += skeletal::lodApproximation(mesh, skeletal::lodCurve(actor, view.location));
            }
        }
        for (UObject* actor : candidates) {
            if (!objects::editorMode() && actor->isA(nameTrigger)) {
                sighted.push_back(actor);
            }
            if (hiddenInGame(actor)) {
                continue;
            }
            if (actor->isA(nameDecal)) {
                decals.push_back(actor);
                continue;
            }
            setActorOrder(actor, view);
            drawActor(actor, view);
        }
        flushBatches(brushBatches);
        for (UObject* actor : decals) {
            drawDecal(actor, view);
        }
        flushMeshBatches();
        for (UObject* actor : sighted) {
            checkSightTrigger(actor, view.location, view.axes.xAxis);
        }
        std::vector<UObject*> shadowed;
        shadowed.swap(shadowUpdates);
        for (UObject* actor : shadowed) {
            if (!actor->destroyed) {
                vm::Call(actor, nameShadowUpdate).i(1).run();
            }
        }
    }

    void SceneRenderer::drawScene(ULevel* level, const std::vector<UObject*>& actors, const Vector& location, const Rotator& rotation, float fov, UObject* viewActor)
    {
        device->flushCanvas();
        device->beginScene(frameWidth, frameHeight, options.multisample);
        drawSceneRect(level, actors, location, rotation, fov, viewActor, 0, 0, frameWidth, frameHeight, false);
        device->endScene(frameWidth, frameHeight, options.fxaa);
        device->useShader();
    }

    void SceneRenderer::drawPortal(ULevel* level, const std::vector<UObject*>& actors, const Vector& location, const Rotator& rotation, float fov, UObject* viewActor, int x, int y, int width, int height, bool clearZ)
    {
        drawSceneRect(level, actors, location, rotation, fov, viewActor, x, y, width, height, clearZ);
        device->useShader();
    }

    void SceneRenderer::drawSceneRect(ULevel* level, const std::vector<UObject*>& actors, const Vector& location, const Rotator& rotation, float fov, UObject* viewActor, int x, int y, int width, int height, bool clearZ)
    {
        device->flushCanvas();
        if (!level || width <= 0 || height <= 0) {
            return;
        }
        if (levelData.level != level) {
            buildLevel(level, actors);
        }
        bool fullFrame = x == 0 && y == 0 && width == frameWidth && height == frameHeight;
        sceneFrame++;
        previousBspDepth = bspDepth;
        bspDepth = -1;
        collectLights(actors);
        collectDynamicLights(location);
        int glY = frameHeight - y - height;
        device->setViewport(x, glY, width, height);
        viewAspect = static_cast<float>(width) / static_cast<float>(height);
        if (!fullFrame) {
            device->enableScissor(x, glY, width, height);
            if (clearZ) {
                device->setDepthWrite(true);
                device->clear(false, true, 1.0f);
            }
        }

        viewportWidth = width;
        viewportHeight = height;
        View view = makeView(location, rotation, fov, sceneNearPlane);
        view.zoneMask = visibleZones(pointZone(location));
        UObject* viewer = World::current ? World::current->player() : nullptr;
        Vector clipColor;
        float clipDistance = 0;
        if (viewer && farClipZone(*reinterpret_cast<UObject**>(actor::Region(viewer)), clipColor, clipDistance)) {
            device->clearColor(clipColor.x, clipColor.y, clipColor.z);
            view.farClip = clipDistance * options.fogDistance;
            if (view.farClip > 0 && view.clipCount < static_cast<int32_t>(view.clipPlanes.size())) {
                const Vector& ahead = view.axes.xAxis;
                view.clipPlanes[static_cast<size_t>(view.clipCount++)] = { -ahead.x, -ahead.y, -ahead.z, ahead.dot(view.location) + 1.5f * view.farClip };
            }
        }
        view.fog = true;
        occludeView(view, nullptr);
        if (levelData.hasBackdrop) {
            drawSky(view, actors, viewActor);
            device->setDepthWrite(true);
            device->clear(false, true, 1.0f);
        }
        if (!levelData.mirrors.empty()) {
            drawMirrors(view, actors);
            device->setDepthWrite(true);
            device->clear(false, true, 1.0f);
        }
        collectVolumetrics(view);
        drawFrameContent(view, actors, viewActor, fullFrame);
        if (device->drawsCoronas()) {
            drawCoronas(view, x, y, width, height);
        }
        device->setCulling(false);
        device->setDepthTest(false);
        if (!fullFrame) {
            device->disableScissor();
            device->setViewport(0, 0, frameWidth, frameHeight);
        }
        viewAspect = static_cast<float>(frameWidth) / static_cast<float>(frameHeight);
    }

    void SceneRenderer::drawActorOverlay(UObject* actor, const Vector& viewLocation, const Rotator& viewRotation, float fov, bool wireframe, bool clearZ, int x, int y, int width, int height)
    {
        device->flushCanvas();
        if (!actor || !actor->data) {
            return;
        }
        bool region = width > 0 && height > 0;
        if (!region) {
            x = 0;
            y = 0;
            width = frameWidth;
            height = frameHeight;
        }
        int glY = frameHeight - y - height;
        device->setViewport(x, glY, width, height);
        viewAspect = static_cast<float>(width) / static_cast<float>(height);
        if (region) {
            device->enableScissor(x, glY, width, height);
        }
        if (clearZ) {
            device->setDepthWrite(true);
            device->clear(false, true, 1.0f);
        }
        wireframeActors = wireframe;
        View view = makeView(viewLocation, viewRotation, fov, clearZ ? overlayNearPlane : sceneNearPlane);
        device->useShader();
        device->setViewProj(view.viewProj);
        device->setEye(view.location, view.axes.xAxis);
        device->setPan(0, 0);
        device->setTint(1, 1, 1, 1);
        device->setDepthTest(true);
        device->setDepthFunc(DepthFunc::LessEqual);
        drawActor(actor, view);
        flushMeshBatches();
        flushTranslucent(view);
        wireframeActors = false;
        device->setCulling(false);
        device->setDepthTest(false);
        if (region) {
            device->disableScissor();
        }
        device->setViewport(0, 0, frameWidth, frameHeight);
        viewAspect = static_cast<float>(frameWidth) / static_cast<float>(frameHeight);
    }
}
