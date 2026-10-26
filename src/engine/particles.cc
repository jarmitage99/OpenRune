#include "engine/particles.h"

#include "core/objects.h"
#include "core/vm.h"
#include "engine/actor.h"
#include "engine/props_engine.h"
#include "engine/skeletal.h"
#include "engine/world.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace rune {
    namespace particles {
        namespace {
            const Name nameParticleSystem("ParticleSystem");
            const Name nameSystemInit("SystemInit");
            const Name nameSystemTick("SystemTick");
            const Name nameParticleTick("ParticleTick");

            std::mt19937& randomEngine()
            {
                static std::mt19937 engine(2000);
                return engine;
            }

            float frand()
            {
                return std::uniform_real_distribution<float>(0.0f, 1.0f)(randomEngine());
            }

            float signedRandom()
            {
                return (frand() - 0.5f) * 2.0f;
            }

            float byteFraction(uint8_t value)
            {
                return static_cast<float>(value) / 255.0f;
            }

            int32_t particleCount(UObject* system)
            {
                return static_cast<int32_t>(particlesystem::ParticleCount(system));
            }

            Vector jointLocation(UObject* actor, int32_t joint)
            {
                const SkelPose* pose = skeletal::pose(actor);
                if (pose && joint >= 0 && static_cast<size_t>(joint) < pose->joints.size()) {
                    return pose->joints[static_cast<size_t>(joint)].origin;
                }
                return actor::Location(actor);
            }

            float lifeFraction(const Particle& particle, float lifeSpanMax)
            {
                if (lifeSpanMax == 0) {
                    return 0;
                }
                return 1.0f - particle.lifeSpan / lifeSpanMax;
            }

            void spawnGeneric(UObject* system, Particle& particle)
            {
                if (particlesystem::bRelativeToSystem(system)) {
                    particle.location = {};
                } else {
                    particle.location = actor::Location(system) + particlesystem::OriginOffset(system);
                }
                particle.velocity = {};
                particle.xScale = 1;
                particle.yScale = 1;
                particle.alpha = { 1, 1, 1 };
                particle.lifeSpan = -1;
                particle.textureIndex = 0;
                particle.style = actor::Style(system);
                particle.valid = 1;
            }

            void spawnEmitter(UObject* system, Particle& particle)
            {
                Coords axes = rotationAxes(actor::Rotation(system));
                Vector shape = particlesystem::ShapeVector(system);
                float rz = signedRandom();
                float ry = signedRandom();
                float rx = signedRandom();
                Vector offset;
                if (particlesystem::SpawnShape(system) == PSHAPE_Ellipsoid) {
                    offset = { shape.x * rx, shape.y * ry, shape.z * rz };
                } else {
                    offset = axes.xAxis * shape.x * rx + axes.yAxis * shape.y * ry + axes.zAxis * shape.z * rz;
                }
                Vector origin = actor::Location(system) + particlesystem::OriginOffset(system);
                particle.location = origin + offset;
                Coords joint;
                if (skeletal::attachmentCoords(system, joint)) {
                    particle.location = actor::Location(system) + untransformVector(axes, particlesystem::OriginOffset(system) + offset);
                    offset = particle.location - origin;
                }
                Vector low = particlesystem::VelocityMin(system);
                Vector range = particlesystem::VelocityMax(system) - low;
                if (actor::bDirectional(system)) {
                    float vz = frand();
                    float vy = frand();
                    float vx = frand();
                    particle.velocity = axes.xAxis * range.x * vx + axes.yAxis * range.y * vy + axes.zAxis * range.z * vz;
                } else {
                    float vx = frand() * range.x + low.x;
                    float vy = frand() * range.y + low.y;
                    float vz = frand() * range.z + low.z;
                    particle.velocity = { vx, vy, vz };
                }
                if (particlesystem::bConvergeX(system)) {
                    particle.velocity.x = (frand() * range.x + low.x) * -offset.x;
                }
                if (particlesystem::bConvergeY(system)) {
                    particle.velocity.y = (frand() * range.y + low.y) * -offset.y;
                }
                if (particlesystem::bConvergeZ(system)) {
                    particle.velocity.z = (frand() * range.z + low.z) * -offset.z;
                }
                float scaleMin = particlesystem::ScaleMin(system);
                float scale = frand() * (particlesystem::ScaleMax(system) - scaleMin) + scaleMin;
                particle.xScale = scale;
                particle.yScale = scale;
                particle.scaleStartX = scale;
                particle.scaleStartY = scale;
                float alpha = byteFraction(particlesystem::AlphaStart(system));
                particle.alpha = { alpha, alpha, alpha };
                float lifeMin = particlesystem::LifeSpanMin(system);
                particle.lifeSpan = frand() * (particlesystem::LifeSpanMax(system) - lifeMin) + lifeMin;
                particle.textureIndex = 0;
                particle.style = actor::Style(system);
                particle.valid = 1;
            }

            void spawn(UObject* system, uint8_t type, Particle& particle)
            {
                if (type == PART_Emitter) {
                    spawnEmitter(system, particle);
                } else if (type == PART_Generic || type == PART_Beam) {
                    spawnGeneric(system, particle);
                }
            }

            void expire(UObject* system, uint8_t type, Particle& particle)
            {
                if (particlesystem::bOneShot(system)) {
                    particle.valid = 0;
                    return;
                }
                spawn(system, type, particle);
            }

            void initEmitter(UObject* system)
            {
                Particle* list = particlesOf(system);
                bool delayed = particlesystem::SpawnDelay(system) > 0;
                for (int32_t i = 0; i < particleCount(system); i++) {
                    if (delayed) {
                        list[i].valid = 0;
                    } else {
                        spawnEmitter(system, list[i]);
                    }
                }
                particlesystem::IsLoaded(system) = 1;
                particlesystem::HasValidCoords(system) = 0;
                float randomDelay = particlesystem::RandomDelay(system);
                if (randomDelay != 0) {
                    particlesystem::CurrentDelay(system) = frand() * randomDelay + randomDelay;
                }
            }

            void initGeneric(UObject* system)
            {
                Particle* list = particlesOf(system);
                for (int32_t i = 0; i < particleCount(system); i++) {
                    spawnGeneric(system, list[i]);
                }
                particlesystem::IsLoaded(system) = 1;
            }

            void initSwipe(UObject* system)
            {
                UObject* owner = actor::Owner(system);
                if (!owner || !skeletal::pose(owner)) {
                    particlesystem::IsLoaded(system) = 0;
                    return;
                }
                Particle* list = particlesOf(system);
                for (int32_t i = 0; i < particleCount(system); i++) {
                    list[i].valid = 0;
                }
                particlesystem::OldBaseLocation(system) = jointLocation(owner, particlesystem::BaseJointIndex(system));
                particlesystem::OldOffsetLocation(system) = jointLocation(owner, particlesystem::OffsetJointIndex(system));
                particlesystem::IsLoaded(system) = 1;
                particlesystem::HasValidCoords(system) = 0;
            }

            void initBeam(UObject* system)
            {
                Particle* list = particlesOf(system);
                float alpha = byteFraction(particlesystem::AlphaStart(system));
                Vector origin = actor::Location(system) + particlesystem::OriginOffset(system);
                for (int32_t i = 0; i < particleCount(system); i++) {
                    Particle& particle = list[i];
                    particle.valid = 1;
                    particle.style = actor::Style(system);
                    particle.alpha = { alpha, alpha, alpha };
                    particle.velocity = {};
                    particle.location = origin;
                    particle.xScale = 1;
                    particle.yScale = 1;
                    particle.textureIndex = 0;
                    particle.lifeSpan = particlesystem::LifeSpanMax(system);
                }
                const float strips[4] = { 0.0f, 0.33f, 0.66f, 1.0f };
                for (int32_t j = 0; j < static_cast<int32_t>(particlesystem::NumConPts(system)); j++) {
                    for (int32_t k = 0; k < 3; k++) {
                        int32_t index = j * 3 + k;
                        if (index >= MaxParticles) {
                            break;
                        }
                        list[index].u0 = strips[k];
                        list[index].v0 = 0;
                        list[index].u1 = strips[k + 1];
                        list[index].v1 = 1;
                    }
                }
                particlesystem::IsLoaded(system) = 1;
            }

            void init(UObject* system, uint8_t type)
            {
                switch (type) {
                case PART_Emitter:
                    initEmitter(system);
                    break;
                case PART_SwipeEffect:
                    initSwipe(system);
                    break;
                case PART_Generic:
                    initGeneric(system);
                    break;
                case PART_Beam:
                    initBeam(system);
                    break;
                default:
                    break;
                }
            }

            bool countDown(float& value, float deltaSeconds)
            {
                value -= deltaSeconds;
                return value <= 0;
            }

            void systemTickEmitter(World& world, UObject* system, float deltaSeconds)
            {
                float& spawnDelay = particlesystem::SpawnDelay(system);
                if (spawnDelay > 0) {
                    if (countDown(spawnDelay, deltaSeconds)) {
                        Particle* list = particlesOf(system);
                        for (int32_t i = 0; i < particleCount(system); i++) {
                            spawnEmitter(system, list[i]);
                        }
                    }
                    return;
                }
                if (particlesystem::RandomDelay(system) != 0 && countDown(particlesystem::CurrentDelay(system), deltaSeconds)) {
                    initEmitter(system);
                }
                if (particlesystem::bSystemOneShot(system)) {
                    Particle* list = particlesOf(system);
                    for (int32_t i = 0; i < particleCount(system); i++) {
                        if (list[i].valid) {
                            return;
                        }
                    }
                    if (!objects::editorMode()) {
                        world.destroy(system);
                        return;
                    }
                }
                float& systemLifeSpan = particlesystem::SystemLifeSpan(system);
                if (systemLifeSpan > 0 && countDown(systemLifeSpan, deltaSeconds)) {
                    world.destroy(system);
                }
            }

            void systemTickSwipe(World& world, UObject* system, float deltaSeconds)
            {
                UObject* owner = actor::Owner(system);
                if (!owner || !skeletal::pose(owner)) {
                    particlesystem::IsLoaded(system) = 0;
                    return;
                }
                float& systemLifeSpan = particlesystem::SystemLifeSpan(system);
                if (systemLifeSpan > 0) {
                    if (countDown(systemLifeSpan, deltaSeconds)) {
                        world.destroy(system);
                    }
                    return;
                }
                Vector base = jointLocation(owner, particlesystem::BaseJointIndex(system));
                Vector tip = jointLocation(owner, particlesystem::OffsetJointIndex(system));
                Vector& oldBase = particlesystem::OldBaseLocation(system);
                Vector& oldTip = particlesystem::OldOffsetLocation(system);
                Particle* list = particlesOf(system);
                for (int32_t i = 0; i < particleCount(system); i++) {
                    Particle& particle = list[i];
                    if (particle.valid) {
                        continue;
                    }
                    particle.valid = 1;
                    particle.style = actor::Style(system);
                    particle.textureIndex = 0;
                    float alpha = byteFraction(particlesystem::AlphaStart(system));
                    particle.alpha = { alpha, alpha, alpha };
                    particle.points[0] = tip;
                    particle.points[1] = oldTip;
                    particle.points[2] = oldBase;
                    particle.points[3] = base;
                    particle.u0 = -deltaSeconds * particlesystem::SwipeSpeed(system);
                    if (static_cast<double>(particle.u0) < -0.99) {
                        particle.u0 = -0.99f;
                    }
                    particle.v0 = 0;
                    particle.u1 = 0;
                    particle.v1 = 0.99f;
                    particle.location = (tip + oldTip + oldBase + base) * 0.25f;
                    break;
                }
                oldTip = tip;
                oldBase = base;
            }

            void systemTick(World& world, UObject* system, uint8_t type, float deltaSeconds)
            {
                if (type == PART_Emitter) {
                    systemTickEmitter(world, system, deltaSeconds);
                } else if (type == PART_SwipeEffect) {
                    systemTickSwipe(world, system, deltaSeconds);
                }
            }

            void particleTickEmitter(UObject* system, float deltaSeconds)
            {
                bool alphaFade = particlesystem::bAlphaFade(system) != 0;
                float alphaStart = alphaFade ? byteFraction(particlesystem::AlphaStart(system)) : 0.0f;
                float alphaEnd = alphaFade ? byteFraction(particlesystem::AlphaEnd(system)) : 0.0f;
                float lifeSpanMax = particlesystem::LifeSpanMax(system);
                float deltaX = particlesystem::ScaleDeltaX(system);
                float deltaY = particlesystem::ScaleDeltaY(system);
                bool gravity = particlesystem::bApplyGravity(system) != 0;
                bool zoneVelocity = particlesystem::bApplyZoneVelocity(system) != 0;
                UObject* zone = zoneOf(system);
                Particle* list = particlesOf(system);
                for (int32_t i = 0; i < particleCount(system); i++) {
                    Particle& particle = list[i];
                    if (!particle.valid) {
                        continue;
                    }
                    float t = lifeFraction(particle, lifeSpanMax);
                    if (alphaFade) {
                        float alpha = alphaStart + (alphaEnd - alphaStart) * t;
                        particle.alpha = { alpha, alpha, alpha };
                    }
                    if (deltaX != 0) {
                        particle.xScale = particle.scaleStartX + (deltaX * particle.scaleStartX - particle.scaleStartX) * t;
                    }
                    if (deltaY != 0) {
                        particle.yScale = particle.scaleStartY + (deltaY * particle.scaleStartY - particle.scaleStartY) * t;
                    }
                    particle.location += particle.velocity * deltaSeconds;
                    if ((gravity || zoneVelocity) && zone) {
                        Vector acceleration;
                        if (gravity) {
                            acceleration += zoneinfo::ZoneGravity(zone) * particlesystem::GravityScale(system);
                        }
                        if (zoneVelocity) {
                            acceleration += zoneinfo::ZoneVelocity(zone) * particlesystem::ZoneVelocityScale(system);
                        }
                        particle.velocity += acceleration * deltaSeconds;
                    }
                    if (particle.lifeSpan > 0 && countDown(particle.lifeSpan, deltaSeconds)) {
                        expire(system, PART_Emitter, particle);
                    }
                    if (particlesystem::bConstrainToBounds(system)) {
                        Vector delta = particle.location;
                        if (!particlesystem::bRelativeToSystem(system)) {
                            delta = particle.location - (actor::Location(system) + particlesystem::OriginOffset(system));
                        }
                        float radius = actor::CollisionRadius(system);
                        if (std::abs(delta.z) > actor::CollisionHeight(system) || delta.x * delta.x + delta.y * delta.y > radius * radius) {
                            expire(system, PART_Emitter, particle);
                        }
                    }
                    if (particlesystem::bWaterOnly(system) && !(zone && zoneinfo::bWaterZone(zone))) {
                        expire(system, PART_Emitter, particle);
                    }
                }
            }

            void particleTickGeneric(UObject* system, uint8_t type, float deltaSeconds)
            {
                UObject* zone = zoneOf(system);
                bool gravity = particlesystem::bApplyGravity(system) && zone;
                Particle* list = particlesOf(system);
                for (int32_t i = 0; i < particleCount(system); i++) {
                    Particle& particle = list[i];
                    particle.location += particle.velocity * deltaSeconds;
                    if (gravity) {
                        particle.velocity += zoneinfo::ZoneGravity(zone) * deltaSeconds;
                    }
                    if (particle.lifeSpan > 0 && countDown(particle.lifeSpan, deltaSeconds)) {
                        expire(system, type, particle);
                    }
                }
            }

            void particleTickSwipe(UObject* system, float deltaSeconds)
            {
                float advance = particlesystem::SwipeSpeed(system) * deltaSeconds;
                Particle* list = particlesOf(system);
                for (int32_t i = 0; i < particleCount(system); i++) {
                    Particle& particle = list[i];
                    if (!particle.valid) {
                        continue;
                    }
                    particle.u0 += advance;
                    particle.u1 += advance;
                    if (static_cast<double>(particle.u1) > 0.99) {
                        particle.u1 = 0.99f;
                    }
                    if (static_cast<double>(particle.u0) > 0.99) {
                        particle.valid = 0;
                    }
                }
            }

            Vector beamStep(UObject* system, const Vector& origin, bool& fixedPoints)
            {
                UObject* target = actor::Target(system);
                fixedPoints = !particlesystem::bUseTargetLocation(system) && !target;
                if (fixedPoints) {
                    return particlesystem::ConnectionPoint(system, 1) - particlesystem::ConnectionPoint(system, 0);
                }
                float segments = static_cast<float>(static_cast<int32_t>(particlesystem::NumConPts(system)) - 1);
                if (particlesystem::bUseTargetLocation(system)) {
                    return (particlesystem::TargetLocation(system) - origin) / segments;
                }
                int32_t joint = particlesystem::TargetJointIndex(system);
                if (joint != 0 && skeletal::pose(target)) {
                    return (jointLocation(target, joint) - origin) / segments;
                }
                return (actor::Location(target) - origin) / segments;
            }

            void beamFace(Particle* list, int32_t index, const Vector& location, const Vector& p0, const Vector& p1, const Vector& p2, const Vector& p3)
            {
                if (index >= MaxParticles) {
                    return;
                }
                Particle& particle = list[index];
                particle.points[0] = p0;
                particle.points[1] = p1;
                particle.points[2] = p2;
                particle.points[3] = p3;
                particle.location = location;
            }

            void particleTickBeam(UObject* system, float deltaSeconds)
            {
                Particle* list = particlesOf(system);
                int32_t points = static_cast<int32_t>(particlesystem::NumConPts(system));
                Vector origin = actor::Location(system) + particlesystem::OriginOffset(system);
                bool fixedPoints = false;
                Vector step = beamStep(system, origin, fixedPoints);
                float textureScale = particlesystem::BeamTextureScale(system);
                Vector cursor = origin;
                for (int32_t j = 0; j < points; j++) {
                    if (!fixedPoints) {
                        particlesystem::ConnectionPoint(system, j) = cursor + particlesystem::ConnectionOffset(system, j);
                    }
                    if (textureScale > 0 && j < points - 1) {
                        float nearV = (cursor - origin).size() * textureScale;
                        float farV = ((cursor + step) - origin).size() * textureScale;
                        for (int32_t k = 0; k < 3 && j * 3 + k < MaxParticles; k++) {
                            list[j * 3 + k].v0 = farV;
                            list[j * 3 + k].v1 = nearV;
                        }
                    }
                    cursor += step;
                }
                Coords axes = rotationAxes(vectorRotation(step));
                float thickness = particlesystem::BeamThickness(system);
                Vector side = axes.yAxis * thickness * 0.75f;
                Vector up = axes.zAxis * static_cast<float>(static_cast<double>(thickness) / 1.732);
                bool taperStart = particlesystem::bTaperStartPoint(system) != 0;
                bool taperEnd = particlesystem::bTaperEndPoint(system) != 0;
                for (int32_t k = 0; k < points - 1; k++) {
                    Vector a = particlesystem::ConnectionPoint(system, k);
                    Vector b = particlesystem::ConnectionPoint(system, k + 1);
                    Vector aLeft = a + side - up;
                    Vector aRight = a - side - up;
                    Vector aTop = a + up;
                    Vector bLeft = b + side - up;
                    Vector bRight = b - side - up;
                    Vector bTop = b + up;
                    if (taperStart && k == 0) {
                        Vector start = particlesystem::ConnectionPoint(system, 0);
                        beamFace(list, k * 3, a, bLeft, bTop, start, start);
                        beamFace(list, k * 3 + 1, a, bTop, bRight, start, start);
                        beamFace(list, k * 3 + 2, b, bRight, bLeft, start, start);
                        continue;
                    }
                    if (taperEnd && k == points - 2) {
                        beamFace(list, k * 3, a, b, b, aTop, aLeft);
                        beamFace(list, k * 3 + 1, a, b, b, aRight, aTop);
                        beamFace(list, k * 3 + 2, b, b, b, aLeft, aRight);
                        continue;
                    }
                    beamFace(list, k * 3, a, bLeft, bTop, aTop, aLeft);
                    beamFace(list, k * 3 + 1, a, bTop, bRight, aRight, aTop);
                    beamFace(list, k * 3 + 2, b, bRight, bLeft, aLeft, aRight);
                }
                bool alphaFade = particlesystem::bAlphaFade(system) != 0;
                float lifeSpanMax = particlesystem::LifeSpanMax(system);
                for (int32_t i = 0; i < particleCount(system); i++) {
                    Particle& particle = list[i];
                    if (!particle.valid) {
                        continue;
                    }
                    if (countDown(particle.lifeSpan, deltaSeconds)) {
                        particle.lifeSpan = 0;
                    }
                    if (alphaFade) {
                        float alphaStart = byteFraction(particlesystem::AlphaStart(system));
                        float alphaEnd = byteFraction(particlesystem::AlphaEnd(system));
                        float alpha = alphaStart + (alphaEnd - alphaStart) * lifeFraction(particle, lifeSpanMax);
                        particle.alpha = { alpha, alpha, alpha };
                    }
                }
            }

            void particleTick(UObject* system, uint8_t type, float deltaSeconds)
            {
                switch (type) {
                case PART_Emitter:
                    particleTickEmitter(system, deltaSeconds);
                    break;
                case PART_SwipeEffect:
                    particleTickSwipe(system, deltaSeconds);
                    break;
                case PART_Generic:
                    particleTickGeneric(system, type, deltaSeconds);
                    break;
                case PART_Beam:
                    particleTickBeam(system, deltaSeconds);
                    break;
                default:
                    break;
                }
            }
        }

        bool isParticleSystem(UObject* actor)
        {
            return actor && actor->isA(nameParticleSystem);
        }

        Particle* particlesOf(UObject* system)
        {
            return reinterpret_cast<Particle*>(particlesystem::ParticleArray(system));
        }

        void tick(World& world, UObject* system, float deltaSeconds)
        {
            float& currentTime = particlesystem::CurrentTime(system);
            currentTime += deltaSeconds;
            particlesystem::LastTime(system) = currentTime;
            uint8_t& count = particlesystem::ParticleCount(system);
            if (count == 0) {
                return;
            }
            if (count > MaxParticles) {
                count = static_cast<uint8_t>(MaxParticles);
            }
            uint8_t type = particlesystem::ParticleType(system);
            if (!particlesystem::IsLoaded(system)) {
                if (particlesystem::bEventSystemInit(system)) {
                    vm::event(system, nameSystemInit);
                } else {
                    init(system, type);
                }
                if (system->destroyed) {
                    return;
                }
            }
            if (world.levelInfo && !levelinfo::Pauser(world.levelInfo).empty()) {
                return;
            }
            if (particlesystem::bEventSystemTick(system)) {
                vm::Call call(system, nameSystemTick);
                if (call.valid()) {
                    call.f(deltaSeconds).run();
                }
            } else {
                systemTick(world, system, type, deltaSeconds);
            }
            if (system->destroyed) {
                return;
            }
            if (particlesystem::bEventParticleTick(system)) {
                vm::Call call(system, nameParticleTick);
                if (call.valid()) {
                    call.f(deltaSeconds).run();
                }
                return;
            }
            particleTick(system, type, deltaSeconds);
        }

        float boundsRadius(UObject* system)
        {
            float collisionRadius = actor::CollisionRadius(system) + 1.0f;
            float radius = Vector { collisionRadius, collisionRadius, actor::CollisionHeight(system) }.size();
            if (!particlesystem::IsLoaded(system)) {
                return radius;
            }
            Vector center = actor::Location(system);
            Particle* list = particlesOf(system);
            for (int32_t i = 0; i < particleCount(system); i++) {
                if (!list[i].valid) {
                    continue;
                }
                Vector delta = list[i].location - center;
                Vector extent { std::abs(delta.x) + 30.0f, std::abs(delta.y) + 30.0f, std::abs(delta.z) + 30.0f };
                radius = std::max(radius, extent.size());
            }
            return radius;
        }
    }
}
