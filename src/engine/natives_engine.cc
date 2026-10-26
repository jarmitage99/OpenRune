#include "audio/audio.h"
#include "core/config.h"
#include "core/objects.h"
#include "core/vm.h"
#include "engine/actor.h"
#include "engine/net.h"
#include "engine/bsp.h"
#include "engine/console.h"
#include "engine/mesh.h"
#include "engine/resources.h"
#include "engine/world.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace rune {
    void registerAnimNatives();
    void registerPawnNatives();
    void registerMiscNatives();
    void registerSkeletalNatives();
    void registerRuneINatives();
    void registerIpDrvNatives();
    void registerUWebNatives();
    void registerScriptedTextureNatives();
    int32_t clientHearSound(UObject* hearer, UObject* source, int32_t id, UObject* sound, const Vector& location, const Vector& parameters);
    Vector transformPointBy(const Vector& point, const Coords& coords);
    Coords coordsTranspose(const Coords& coords);
    Coords coordsTransform(const Coords& coords, const Coords& by);
    Coords coordsRotate(Coords coords, const Rotator& rotation);
    Coords coordsUnrotate(Coords coords, const Rotator& rotation);

    namespace {
        World& world()
        {
            return *World::current;
        }

        UClass* classParam(Frame& stack)
        {
            return cast<UClass>(vm::get<UObject*>(stack));
        }

        bool matches(UObject* a, UClass* cls)
        {
            return a && !a->destroyed && (!cls || a->isA(cls));
        }

        Color mulColor(const Color& c, float f)
        {
            auto scale = [f](uint8_t v) {
                return static_cast<uint8_t>(static_cast<int64_t>(static_cast<double>(v) * static_cast<double>(f)));
            };
            return { scale(c.r), scale(c.g), scale(c.b), c.a };
        }

        void nativeMultiplyColorFloat(UObject*, Frame& stack, void* result)
        {
            Color a = vm::get<Color>(stack);
            float b = vm::get<float>(stack);
            vm::finish(stack);
            vm::ret<Color>(result, mulColor(a, b));
        }

        void nativeMultiplyFloatColor(UObject*, Frame& stack, void* result)
        {
            float a = vm::get<float>(stack);
            Color b = vm::get<Color>(stack);
            vm::finish(stack);
            vm::ret<Color>(result, mulColor(b, a));
        }

        void nativeAddColorColor(UObject*, Frame& stack, void* result)
        {
            Color a = vm::get<Color>(stack);
            Color b = vm::get<Color>(stack);
            vm::finish(stack);
            auto add = [](uint8_t x, uint8_t y) {
                return static_cast<uint8_t>(x + y);
            };
            vm::ret<Color>(result, { add(a.r, b.r), add(a.g, b.g), add(a.b, b.b), a.a });
        }

        void nativeSubtractColorColor(UObject*, Frame& stack, void* result)
        {
            Color a = vm::get<Color>(stack);
            Color b = vm::get<Color>(stack);
            vm::finish(stack);
            auto sub = [](uint8_t x, uint8_t y) {
                return static_cast<uint8_t>(x - y);
            };
            vm::ret<Color>(result, { sub(a.r, b.r), sub(a.g, b.g), sub(a.b, b.b), a.a });
        }

        void nativeSpawn(UObject* self, Frame& stack, void* result)
        {
            UClass* cls = objects::replacementClass(classParam(stack));
            UObject* owner = vm::getOpt<UObject*>(stack, nullptr);
            Name tag = vm::getOpt<Name>(stack, Name());
            Vector location = vm::getOpt<Vector>(stack, actor::Location(self));
            Rotator rotation = vm::getOpt<Rotator>(stack, actor::Rotation(self));
            vm::finish(stack);
            UObject* spawned = nullptr;
            if (cls && World::current) {
                spawned = world().spawn(cls, owner, Name(), location, rotation, nullptr, false, actor::Instigator(self));
            }
            if (spawned) {
                actor::Tag(spawned) = tag;
            }
            vm::ret<UObject*>(result, spawned);
        }

        void nativeDestroy(UObject* self, Frame& stack, void* result)
        {
            vm::finish(stack);
            vm::retBool(result, World::current && world().destroy(self));
        }

        void nativeSetTimer(UObject* self, Frame& stack, void*)
        {
            float rate = vm::get<float>(stack);
            bool loop = vm::getBool(stack);
            vm::finish(stack);
            actor::TimerCounter(self) = 0;
            actor::TimerRate(self) = rate;
            actor::bTimerLoop(self) = loop ? 1 : 0;
        }

        void nativeTrace(UObject* self, Frame& stack, void* result)
        {
            Vector* hitLocation = vm::getRef<Vector>(stack);
            Vector* hitNormal = vm::getRef<Vector>(stack);
            Vector end = vm::get<Vector>(stack);
            Vector start = vm::getOpt<Vector>(stack, actor::Location(self));
            bool traceActors = vm::getBoolOpt(stack, actor::bCollideActors(self) != 0);
            Vector extent = vm::getOpt<Vector>(stack, Vector {});
            vm::finish(stack);
            uint32_t flags = traceActors ? (TRACE_AllColliding | TRACE_OnlyProjActor) : TRACE_World;
            CheckResult hit;
            UObject* hitActor = nullptr;
            if (World::current && world().lineCheck(hit, start, end, extent, self, flags)) {
                *hitLocation = hit.location;
                *hitNormal = hit.normal;
                hitActor = hit.actor;
            } else {
                *hitLocation = {};
                *hitNormal = {};
            }
            vm::ret<UObject*>(result, hitActor);
        }

        void nativeFastTrace(UObject* self, Frame& stack, void* result)
        {
            Vector end = vm::get<Vector>(stack);
            Vector start = vm::getOpt<Vector>(stack, actor::Location(self));
            vm::finish(stack);
            vm::retBool(result, !World::current || world().fastLineCheck(end, start));
        }

        void nativeSetLocation(UObject* self, Frame& stack, void* result)
        {
            Vector location = vm::get<Vector>(stack);
            vm::finish(stack);
            vm::retBool(result, World::current && world().farMoveActor(self, location, false, false));
        }

        void nativeSetRotation(UObject* self, Frame& stack, void* result)
        {
            Rotator rotation = vm::get<Rotator>(stack);
            vm::finish(stack);
            CheckResult hit;
            vm::retBool(result, World::current && world().moveActor(self, {}, rotation, hit, false, false, false, false));
        }

        void nativeMove(UObject* self, Frame& stack, void* result)
        {
            Vector delta = vm::get<Vector>(stack);
            vm::finish(stack);
            CheckResult hit;
            vm::retBool(result, World::current && world().moveActor(self, delta, actor::Rotation(self), hit, false, false, false, false));
        }

        void nativeMoveSmooth(UObject* self, Frame& stack, void* result)
        {
            Vector delta = vm::get<Vector>(stack);
            vm::finish(stack);
            actor::bJustTeleported(self) = 0;
            vm::retBool(result, World::current && moveSmooth(self, delta));
        }

        void nativeSetCollision(UObject* self, Frame& stack, void*)
        {
            bool collide = vm::getBoolOpt(stack, actor::bCollideActors(self) != 0);
            bool blockActors = vm::getBoolOpt(stack, actor::bBlockActors(self) != 0);
            bool blockPlayers = vm::getBoolOpt(stack, actor::bBlockPlayers(self) != 0);
            vm::finish(stack);
            setCollision(self, collide, blockActors, blockPlayers);
        }

        void nativeSetCollisionSize(UObject* self, Frame& stack, void* result)
        {
            float radius = vm::get<float>(stack);
            float height = vm::get<float>(stack);
            vm::finish(stack);
            actor::CollisionRadius(self) = radius;
            actor::CollisionHeight(self) = height;
            if (World::current) {
                World::current->collisionHash.update(self);
            }
            vm::retBool(result, true);
        }

        void nativeSetBase(UObject* self, Frame& stack, void*)
        {
            UObject* base = vm::get<UObject*>(stack);
            vm::finish(stack);
            setBase(self, base, true);
        }

        void nativeSetOwner(UObject* self, Frame& stack, void*)
        {
            UObject* owner = vm::get<UObject*>(stack);
            vm::finish(stack);
            setOwner(self, owner);
        }

        void nativeSetPhysics(UObject* self, Frame& stack, void*)
        {
            uint8_t physics = vm::get<uint8_t>(stack);
            UObject* floor = vm::getOpt<UObject*>(stack, nullptr);
            vm::finish(stack);
            setPhysics(self, physics, floor);
        }

        void nativeAutonomousPhysics(UObject* self, Frame& stack, void*)
        {
            float delta = vm::get<float>(stack);
            vm::finish(stack);
            Vector& acceleration = actor::Acceleration(self);
            acceleration.x = static_cast<float>(static_cast<int32_t>(static_cast<double>(acceleration.x) * 10.0) * 0.1);
            acceleration.y = static_cast<float>(static_cast<int32_t>(static_cast<double>(acceleration.y) * 10.0) * 0.1);
            acceleration.z = static_cast<float>(static_cast<int32_t>(static_cast<double>(acceleration.z) * 10.0) * 0.1);
            if (actor::Physics(self) != PHYS_None) {
                performPhysics(self, delta);
            }
        }

        void nativeError(UObject* self, Frame& stack, void*)
        {
            std::string message = vm::get<std::string>(stack);
            vm::finish(stack);
            std::fprintf(stderr, "script error from %s: %s\n", self->name.c_str(), message.c_str());
            if (World::current) {
                world().destroy(self);
            }
        }

        template <typename Filter>
        void liveIterator(Frame& stack, UObject** out, Filter filter)
        {
            size_t index = 0;
            vm::iterate(stack, [&]() {
                while (World::current && index < world().actors.size()) {
                    UObject* a = world().actors[index++];
                    if (a && !a->destroyed && filter(a)) {
                        *out = a;
                        return true;
                    }
                }
                *out = nullptr;
                return false;
            });
        }

        void nativeAllActors(UObject*, Frame& stack, void*)
        {
            UClass* cls = classParam(stack);
            UObject** out = vm::getRef<UObject*>(stack);
            Name tag = vm::getOpt<Name>(stack, Name());
            vm::finish(stack);
            liveIterator(stack, out, [&](UObject* a) {
                return matches(a, cls) && (tag.none() || actor::Tag(a) == tag);
            });
        }

        bool isOwnedBy(UObject* a, UObject* owner)
        {
            for (UObject* current = a; current; current = actor::Owner(current)) {
                if (current == owner) {
                    return true;
                }
            }
            return false;
        }

        void nativeChildActors(UObject* self, Frame& stack, void*)
        {
            UClass* cls = classParam(stack);
            UObject** out = vm::getRef<UObject*>(stack);
            vm::finish(stack);
            liveIterator(stack, out, [&](UObject* a) {
                return matches(a, cls) && isOwnedBy(a, self);
            });
        }

        void nativeBasedActors(UObject* self, Frame& stack, void*)
        {
            UClass* cls = classParam(stack);
            UObject** out = vm::getRef<UObject*>(stack);
            vm::finish(stack);
            liveIterator(stack, out, [&](UObject* a) {
                return matches(a, cls) && actor::Base(a) == self;
            });
        }

        void nativeTouchingActors(UObject* self, Frame& stack, void*)
        {
            UClass* cls = classParam(stack);
            UObject** out = vm::getRef<UObject*>(stack);
            vm::finish(stack);
            int index = 0;
            vm::iterate(stack, [&]() {
                while (index < 4) {
                    UObject* a = actor::Touching(self, index++);
                    if (matches(a, cls)) {
                        *out = a;
                        return true;
                    }
                }
                *out = nullptr;
                return false;
            });
        }

        void nativeRadiusActors(UObject* self, Frame& stack, void*)
        {
            UClass* cls = classParam(stack);
            UObject** out = vm::getRef<UObject*>(stack);
            float radius = vm::get<float>(stack);
            Vector location = vm::getOpt<Vector>(stack, actor::Location(self));
            vm::finish(stack);
            liveIterator(stack, out, [&](UObject* a) {
                float reach = radius + actor::CollisionRadius(a);
                return matches(a, cls) && (actor::Location(a) - location).sizeSquared() < reach * reach;
            });
        }

        void nativeVisibleActors(UObject* self, Frame& stack, void*)
        {
            UClass* cls = classParam(stack);
            UObject** out = vm::getRef<UObject*>(stack);
            float radius = vm::getOpt<float>(stack, 0.0f);
            Vector location = vm::getOpt<Vector>(stack, actor::Location(self));
            vm::finish(stack);
            liveIterator(stack, out, [&](UObject* a) {
                if (actor::bHidden(a) || !matches(a, cls)) {
                    return false;
                }
                if (radius != 0 && !((actor::Location(a) - location).sizeSquared() < radius * radius)) {
                    return false;
                }
                return world().fastLineCheck(actor::Location(a), location);
            });
        }

        void nativeVisibleCollidingActors(UObject* self, Frame& stack, void*)
        {
            UClass* cls = classParam(stack);
            UObject** out = vm::getRef<UObject*>(stack);
            float radius = vm::getOpt<float>(stack, 0.0f);
            Vector location = vm::getOpt<Vector>(stack, actor::Location(self));
            bool ignoreHidden = vm::getBoolOpt(stack, false);
            vm::finish(stack);
            if (radius == 0) {
                radius = 1000.0f;
            }
            std::vector<UObject*> list;
            if (World::current) {
                Vector reach { radius, radius, radius };
                for (UObject* a : world().collisionHash.query(location - reach, location + reach)) {
                    if (a && !a->destroyed && actor::bCollideActors(a) && (actor::Location(a) - location).sizeSquared() < radius * radius) {
                        list.push_back(a);
                    }
                }
            }
            std::reverse(list.begin(), list.end());
            size_t index = 0;
            vm::iterate(stack, [&]() {
                while (index < list.size()) {
                    UObject* a = list[index++];
                    if (a->destroyed || !matches(a, cls) || (ignoreHidden && actor::bHidden(a))) {
                        continue;
                    }
                    if (world().fastLineCheck(actor::Location(a), location)) {
                        *out = a;
                        return true;
                    }
                }
                *out = nullptr;
                return false;
            });
        }

        void nativeTraceActors(UObject* self, Frame& stack, void*)
        {
            classParam(stack);
            UObject** out = vm::getRef<UObject*>(stack);
            Vector* hitLocation = vm::getRef<Vector>(stack);
            Vector* hitNormal = vm::getRef<Vector>(stack);
            Vector end = vm::get<Vector>(stack);
            Vector start = vm::getOpt<Vector>(stack, actor::Location(self));
            Vector extent = vm::getOpt<Vector>(stack, Vector {});
            bool traceLevel = vm::getBoolOpt(stack, true);
            vm::finish(stack);
            std::vector<CheckResult> hits;
            if (World::current) {
                uint32_t flags = traceLevel ? TRACE_AllColliding : (TRACE_Pawns | TRACE_Movers | TRACE_Others);
                hits = world().multiLineCheck(start, end, extent, nullptr, flags);
            }
            size_t index = 0;
            vm::iterate(stack, [&]() {
                while (index < hits.size()) {
                    const CheckResult& hit = hits[index++];
                    if (hit.actor) {
                        *out = hit.actor;
                        *hitLocation = hit.location;
                        *hitNormal = hit.normal;
                        return true;
                    }
                }
                *out = nullptr;
                return false;
            });
        }

        void nativeCalcArcVelocity(UObject* self, Frame& stack, void* result)
        {
            int32_t trajectory = vm::get<int32_t>(stack);
            Vector source = vm::get<Vector>(stack);
            Vector destination = vm::get<Vector>(stack);
            vm::finish(stack);
            UObject* zone = zoneOf(self);
            float gravity = zone ? zoneinfo::ZoneGravity(zone).size() : 950.0f;
            double angle = static_cast<double>(static_cast<float>(static_cast<double>(trajectory) * 3.141592653589793 * 3.0517578125e-05));
            Vector delta = destination - source;
            Rotator aim = unrealRotation(delta);
            float height = delta.z;
            delta.z = 0;
            float distance = delta.size();
            float spread = distance * distance * gravity;
            double cosine = std::cos(angle);
            float twoCosSquared = static_cast<float>(cosine * cosine * 2.0);
            float rise = static_cast<float>(std::tan(angle) * static_cast<double>(distance) - static_cast<double>(height));
            float speed = 0;
            if (twoCosSquared != 0 && rise != 0) {
                float speedSquared = spread / (rise * twoCosSquared);
                if (!(speedSquared < 0)) {
                    speed = std::sqrt(speedSquared);
                }
            }
            aim.pitch = trajectory;
            vm::ret<Vector>(result, rotationAxes(aim).xAxis.normal() * speed);
        }

        void nativeLipSyncString(UObject*, Frame& stack, void* result)
        {
            USound* sound = cast<USound>(vm::get<UObject*>(stack));
            vm::get<float>(stack);
            vm::finish(stack);
            if (sound) {
                objects::ensureLoaded(sound);
                std::fprintf(stderr, "Number samples in %s = %d\n", sound->name.c_str(), static_cast<int>(sound->bytes.size()));
            }
            vm::ret<std::string>(result, std::string());
        }

        int32_t surfaceAt(const UModel& model, int32_t iNode, const Vector& point)
        {
            while (iNode >= 0 && static_cast<size_t>(iNode) < model.nodes.size()) {
                const BspNode& node = model.nodes[static_cast<size_t>(iNode)];
                if (node.numVertices > 0) {
                    const Vector& normal = model.vectors[static_cast<size_t>(model.surfs[static_cast<size_t>(node.surf)].vNormal)];
                    const BspVert* verts = &model.verts[static_cast<size_t>(node.vertPool)];
                    Vector previous = model.points[static_cast<size_t>(verts[node.numVertices - 1].vertex)];
                    bool inside = true;
                    for (int32_t i = 0; i < node.numVertices; i++) {
                        const Vector& current = model.points[static_cast<size_t>(verts[i].vertex)];
                        Vector edge = normal.cross(current - previous);
                        if (point.dot(edge) - current.dot(edge) < 0.0f) {
                            inside = false;
                            break;
                        }
                        previous = current;
                    }
                    if (inside) {
                        return node.surf;
                    }
                }
                iNode = node.iPlane;
            }
            return -1;
        }

        void nativeTraceTexture(UObject* self, Frame& stack, void* result)
        {
            Vector end = vm::get<Vector>(stack);
            Vector start = vm::get<Vector>(stack);
            int32_t* flags = vm::getRef<int32_t>(stack);
            Vector* scrollDir = vm::getRef<Vector>(stack);
            vm::finish(stack);
            *flags = 0;
            *scrollDir = {};
            UObject* texture = nullptr;
            CheckResult hit;
            UModel* model = World::current && world().level ? world().level->model : nullptr;
            if (model && world().bspLineCheck(hit, start, end, Vector {}) && hit.item >= 0 && static_cast<size_t>(hit.item) < model->nodes.size()) {
                const BspSurf& hitSurf = model->surfs[static_cast<size_t>(model->nodes[static_cast<size_t>(hit.item)].surf)];
                const Vector& base = model->points[static_cast<size_t>(hitSurf.pBase)];
                const Vector& normal = model->vectors[static_cast<size_t>(hitSurf.vNormal)];
                Vector point = start + (end - start) * ((base - start).dot(normal) / (end - start).dot(normal));
                int32_t surf = surfaceAt(*model, hit.item, point);
                if (surf >= 0) {
                    const BspSurf& found = model->surfs[static_cast<size_t>(surf)];
                    texture = found.texture;
                    *flags = static_cast<int32_t>(found.polyFlags);
                    UObject* zone = zoneOf(self);
                    if ((found.polyFlags & bsp::PF_AutoUPan) && zone) {
                        *scrollDir = *scrollDir + model->vectors[static_cast<size_t>(found.vTextureU)] * zoneinfo::TexUPanSpeed(zone);
                    }
                    if ((found.polyFlags & bsp::PF_AutoVPan) && zone) {
                        *scrollDir = *scrollDir + model->vectors[static_cast<size_t>(found.vTextureV)] * zoneinfo::TexVPanSpeed(zone);
                    }
                }
            }
            vm::ret<UObject*>(result, texture);
        }

        struct SoundRequest {
            UObject* sound = nullptr;
            uint8_t slot = 1;
            float volume = 0;
            bool noOverride = false;
            float radius = 0;
            float pitch = 1;
        };

        SoundRequest soundRequest(UObject* self, Frame& stack)
        {
            SoundRequest request;
            request.sound = vm::get<UObject*>(stack);
            request.slot = vm::getOpt<uint8_t>(stack, 1);
            request.volume = vm::getOpt<float>(stack, actor::TransientSoundVolume(self));
            request.noOverride = vm::getBoolOpt(stack, false);
            request.radius = vm::getOpt<float>(stack, actor::TransientSoundRadius(self));
            request.pitch = vm::getOpt<float>(stack, 1.0f);
            vm::finish(stack);
            return request;
        }

        int32_t soundId(UObject* self, const SoundRequest& request)
        {
            return static_cast<int32_t>(self->handle * 16u + request.slot * 2u + (request.noOverride ? 1u : 0u));
        }

        int32_t hearSound(UObject* hearer, UObject* self, const SoundRequest& request, float radiusSquared)
        {
            int32_t id = soundId(self, request);
            Vector parameters { 100.0f * request.volume, request.radius, 100.0f * request.pitch };
            UObject* viewTarget = isPlayerPawn(hearer) ? playerpawn::ViewTarget(hearer) : nullptr;
            Vector hearLocation = viewTarget ? actor::Location(viewTarget) : actor::Location(hearer);
            float threshold = radiusSquared * 0.7692308f;
            float distanceSquared = (hearLocation - actor::Location(self)).sizeSquared();
            if (!(distanceSquared < threshold)) {
                return 0;
            }
            if (!world().fastLineCheck(hearLocation, actor::Location(self))) {
                if (actor::Instigator(self) != hearer) {
                    threshold *= 0.6f;
                }
                parameters.x *= 0.35f;
                if (distanceSquared > threshold) {
                    return 0;
                }
            }
            if (hearer != world().localPlayer) {
                static const Name nameClientHearSound("ClientHearSound");
                vm::Call(hearer, nameClientHearSound).obj(self).i(id).obj(request.sound).v(actor::Location(self)).v(parameters).run();
                return id;
            }
            return clientHearSound(hearer, self, id, request.sound, actor::Location(self), parameters);
        }

        void checkHearSound(UObject* self, const SoundRequest& request, float radiusSquared, UObject* skip)
        {
            if (!World::current) {
                return;
            }
            UObject* local = world().localPlayer;
            if (local && !local->destroyed) {
                hearSound(local, self, request, radiusSquared);
            }
            if (world().netMode == net::NM_DedicatedServer || world().netMode == net::NM_ListenServer) {
                for (UObject* hearer = levelinfo::PawnList(world().levelInfo); hearer; hearer = pawn::nextPawn(hearer)) {
                    if (hearer != local && hearer != skip && !hearer->destroyed && isPlayerPawn(hearer) && playerpawn::Player(hearer)) {
                        hearSound(hearer, self, request, radiusSquared);
                    }
                }
            }
        }

        void nativePlaySound(UObject* self, Frame& stack, void* result)
        {
            SoundRequest request = soundRequest(self, stack);
            if (!request.sound) {
                return;
            }
            float radius = request.radius != 0 ? request.radius : 1600.0f;
            checkHearSound(self, request, radius * radius, nullptr);
            vm::ret<int32_t>(result, soundId(self, request));
        }

        void nativePlayOwnedSound(UObject* self, Frame& stack, void*)
        {
            SoundRequest request = soundRequest(self, stack);
            if (!request.sound) {
                return;
            }
            UObject* skip = nullptr;
            if (World::current && world().netMode != net::NM_Standalone) {
                UObject* owner = actor::Owner(self);
                UObject* candidate = isPlayerPawn(self) ? self : (owner && isPlayerPawn(owner) ? owner : nullptr);
                UObject* player = candidate ? playerpawn::Player(candidate) : nullptr;
                if (player && player != world().viewport) {
                    skip = candidate;
                }
            }
            float radius = request.radius != 0 ? request.radius : 1600.0f;
            checkHearSound(self, request, radius * radius, skip);
        }

        void nativeStopSound(UObject*, Frame& stack, void*)
        {
            int32_t id = vm::get<int32_t>(stack);
            vm::finish(stack);
            audio::stopSound(id);
        }

        void nativeGetSoundDuration(UObject*, Frame& stack, void* result)
        {
            UObject* sound = vm::get<UObject*>(stack);
            vm::finish(stack);
            USound* data = cast<USound>(sound);
            float length = 0;
            if (data) {
                objects::ensureLoaded(data);
                length = audio::duration(data);
            }
            vm::ret<float>(result, length);
        }

        void nativeStopAllSound(UObject*, Frame& stack, void*)
        {
            vm::finish(stack);
            audio::stopAllSounds();
        }

        bool canHearNoise(UObject* listener, const Vector& noiseLocation, float loudness, UObject* other)
        {
            float distanceSquared = (actor::Location(listener) - noiseLocation).sizeSquared();
            UObject* game = levelinfo::Game(world().levelInfo);
            UObject* replication = pawn::PlayerReplicationInfo(listener);
            UObject* otherReplication = pawn::bIsPlayer(other) ? pawn::PlayerReplicationInfo(other) : nullptr;
            if (pawn::bIsPlayer(listener) && game && gameinfo::bTeamGame(game) && replication && otherReplication && playerreplicationinfo::Team(replication) == playerreplicationinfo::Team(otherReplication)) {
                if (!(loudness * loudness * 16000000.0f > distanceSquared)) {
                    return false;
                }
                return world().fastLineCheck(noiseLocation, actor::Location(listener));
            }
            if (loudness * loudness * 4000000.0f < distanceSquared) {
                return false;
            }
            if (distanceSquared == 0) {
                return true;
            }
            float perceived = 1200000.0f / distanceSquared;
            float alerted = perceived;
            if (perceived > 2.0f) {
                perceived = 2.0f;
                alerted = 0.5f;
            } else if (!(0.5f > perceived)) {
                alerted = 0.5f;
            }
            pawn::Stimulus(listener) = loudness * perceived + alerted * pawn::Alertness(listener);
            if (pawn::Stimulus(listener) < pawn::HearingThreshold(listener)) {
                return false;
            }
            return world().fastLineCheck(noiseLocation, actor::Location(listener));
        }

        bool rememberNoise(UObject* self, UObject* instigator, float loudness)
        {
            float now = world().timeSeconds;
            Vector location = actor::Location(self);
            auto near = [&](const Vector& spot) {
                return (spot - location).sizeSquared() < 2500.0f;
            };
            double recent = static_cast<double>(now) - 0.2;
            if (static_cast<double>(pawn::noise1time(instigator)) > recent && near(pawn::noise1spot(instigator)) && !(static_cast<double>(loudness) * 0.9 > static_cast<double>(pawn::noise1loudness(instigator)))) {
                return false;
            }
            if (static_cast<double>(pawn::noise2time(instigator)) > recent && near(pawn::noise2spot(instigator)) && !(static_cast<double>(loudness) * 0.9 > static_cast<double>(pawn::noise2loudness(instigator)))) {
                return false;
            }
            double stale = static_cast<double>(now) - 0.18;
            bool first = static_cast<double>(pawn::noise1time(instigator)) < stale;
            bool second = !first && static_cast<double>(pawn::noise2time(instigator)) < stale;
            if (!first && !second) {
                first = (near(pawn::noise1spot(instigator)) && !(pawn::noise1loudness(instigator) > loudness)) || !(pawn::noise2loudness(instigator) > loudness);
            }
            if (first) {
                pawn::noise1time(instigator) = now;
                pawn::noise1spot(instigator) = location;
                pawn::noise1loudness(instigator) = loudness;
            } else if (second) {
                pawn::noise2time(instigator) = now;
                pawn::noise2spot(instigator) = location;
                pawn::noise2loudness(instigator) = loudness;
            }
            return true;
        }

        void nativeMakeNoise(UObject* self, Frame& stack, void*)
        {
            float loudness = vm::get<float>(stack);
            vm::finish(stack);
            UObject* instigator = actor::Instigator(self);
            if (!World::current || levelinfo::NetMode(world().levelInfo) == net::NM_Client || !instigator || !rememberNoise(self, instigator, loudness)) {
                return;
            }
            UObject* enemy = pawn::Enemy(instigator);
            bool fromPlayer = pawn::bIsPlayer(instigator) || (enemy && pawn::bIsPlayer(enemy));
            static const Name hearNoise("HearNoise");
            for (UObject* listener = levelinfo::PawnList(world().levelInfo); listener; listener = pawn::nextPawn(listener)) {
                if (listener == instigator || !vm::isProbing(listener, hearNoise)) {
                    continue;
                }
                if (fromPlayer && isPlayerPawn(listener)) {
                    continue;
                }
                if (!fromPlayer && !listener->isA(self->cls) && !self->isA(listener->cls)) {
                    continue;
                }
                if (canHearNoise(listener, actor::Location(self), loudness, instigator)) {
                    vm::Call(listener, hearNoise).f(loudness).obj(self).run();
                }
            }
        }

        void nativePlayerCanSeeMe(UObject* self, Frame& stack, void* result)
        {
            vm::finish(stack);
            bool visible = false;
            UObject* viewer = World::current ? world().localPlayer : nullptr;
            if (viewer && playerpawn::ViewTarget(viewer) == self) {
                visible = true;
            } else if (viewer) {
                Vector direction = actor::Location(self) - pawn::ViewLocation(viewer);
                float distanceSquared = direction.sizeSquared();
                bool inRange = (static_cast<double>(actor::CollisionRadius(self)) + 3.6) * 100000.0 > static_cast<double>(distanceSquared);
                float facing = rotationVector(pawn::ViewRotation(viewer)).dot(direction);
                bool inView = !(static_cast<double>(distanceSquared) * 0.25 > static_cast<double>(facing * facing));
                visible = inRange && inView && lineOfSightTo(viewer, self, false, false);
            }
            vm::retBool(result, visible);
        }

        void nativeGetURLMap(UObject*, Frame& stack, void* result)
        {
            vm::finish(stack);
            std::string map;
            if (World::current) {
                map = world().url;
                size_t cut = map.find_first_of("?#");
                if (cut != std::string::npos) {
                    map = map.substr(0, cut);
                }
                size_t dot = map.rfind('.');
                if (dot != std::string::npos) {
                    map = map.substr(0, dot);
                }
            }
            vm::ret<std::string>(result, map);
        }

        void nativeGetMapName(UObject*, Frame& stack, void* result)
        {
            std::string prefix = vm::get<std::string>(stack);
            std::string current = vm::get<std::string>(stack);
            int32_t direction = vm::get<int32_t>(stack);
            vm::finish(stack);
            std::vector<std::string> maps;
            std::filesystem::path directory = objects::gameRoot() / "Maps";
            if (std::filesystem::is_directory(directory)) {
                for (const auto& item : std::filesystem::directory_iterator(directory)) {
                    std::string file = item.path().filename().string();
                    if (!iequals(item.path().extension().string(), ".run")) {
                        continue;
                    }
                    if (prefix.empty() || iequals(file.substr(0, prefix.size()), prefix)) {
                        maps.push_back(file);
                    }
                }
            }
            std::sort(maps.begin(), maps.end(), [](const std::string& a, const std::string& b) {
                return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(), [](char x, char y) {
                    return std::toupper(static_cast<unsigned char>(x)) < std::toupper(static_cast<unsigned char>(y));
                });
            });
            std::string chosen;
            if (!maps.empty()) {
                auto it = std::find(maps.begin(), maps.end(), current);
                if (it == maps.end()) {
                    chosen = maps.front();
                } else {
                    ptrdiff_t index = (it - maps.begin()) + direction;
                    if (index < 0) {
                        index = static_cast<ptrdiff_t>(maps.size()) - 1;
                    } else if (index >= static_cast<ptrdiff_t>(maps.size())) {
                        index = 0;
                    }
                    chosen = maps[static_cast<size_t>(index)];
                }
            }
            vm::ret<std::string>(result, chosen);
        }

        std::string intField(const std::string& entry, std::string_view key)
        {
            for (size_t at = 0; at < entry.size(); at++) {
                if ((at == 0 || entry[at - 1] == '(' || entry[at - 1] == ',') && entry.size() > at + key.size() && entry[at + key.size()] == '=' && iequals(entry.substr(at, key.size()), key)) {
                    size_t start = at + key.size() + 1;
                    if (start < entry.size() && entry[start] == '"') {
                        size_t end = entry.find('"', start + 1);
                        return entry.substr(start + 1, end == std::string::npos ? std::string::npos : end - start - 1);
                    }
                    size_t end = entry.find_first_of(",)", start);
                    return entry.substr(start, end == std::string::npos ? std::string::npos : end - start);
                }
            }
            return {};
        }

        template <typename Accept>
        std::vector<std::pair<std::string, std::string>> registryEntries(Accept accept)
        {
            std::vector<std::filesystem::path> files;
            std::error_code error;
            for (const auto& item : std::filesystem::directory_iterator(objects::gameRoot() / "System", error)) {
                if (iequals(item.path().extension().string(), ".int")) {
                    files.push_back(item.path());
                }
            }
            std::sort(files.begin(), files.end());
            std::vector<std::pair<std::string, std::string>> entries;
            for (const std::filesystem::path& file : files) {
                Ini* ini = config::localization(file.stem().string());
                for (const auto& [key, value] : ini->section("Public")) {
                    if (iequals(key, "Object") && accept(value)) {
                        entries.emplace_back(intField(value, "Name"), intField(value, "Description"));
                    }
                }
            }
            return entries;
        }

        std::vector<std::pair<std::string, std::string>> intEntries(const std::string& metaClass)
        {
            bool shortName = metaClass.find('.') == std::string::npos;
            return registryEntries([&](const std::string& value) {
                std::string entryClass = intField(value, "MetaClass");
                return iequals(entryClass, metaClass) || (shortName && iequals(entryClass.substr(entryClass.rfind('.') + 1), metaClass));
            });
        }

        std::vector<std::pair<std::string, std::string>> textureEntries()
        {
            return registryEntries([](const std::string& value) {
                return iequals(intField(value, "Class"), "Texture");
            });
        }

        void nativeGetNextSkin(UObject*, Frame& stack, void*)
        {
            std::string prefix = vm::get<std::string>(stack);
            std::string current = vm::get<std::string>(stack);
            int32_t direction = vm::get<int32_t>(stack);
            std::string* skin = vm::getRef<std::string>(stack);
            std::string* description = vm::getRef<std::string>(stack);
            vm::finish(stack);
            std::vector<std::pair<std::string, std::string>> list = textureEntries();
            int32_t useSkin = -1;
            int32_t firstSkin = -1;
            int32_t prevSkin = -1;
            bool useNext = false;
            for (int32_t i = 0; i < static_cast<int32_t>(list.size()); i++) {
                const std::string& object = list[static_cast<size_t>(i)].first;
                if (!iequals(object.substr(0, prefix.size()), prefix)) {
                    continue;
                }
                if (useNext) {
                    useSkin = i;
                    useNext = false;
                    break;
                }
                if (firstSkin == -1) {
                    firstSkin = i;
                }
                if (iequals(object, current)) {
                    if (direction == -1) {
                        useSkin = prevSkin;
                        break;
                    } else if (direction == 0) {
                        useSkin = i;
                        break;
                    } else {
                        useNext = true;
                    }
                }
                prevSkin = i;
            }
            if (useNext) {
                useSkin = firstSkin;
            }
            if (useSkin == -1) {
                useSkin = prevSkin;
            }
            if (useSkin >= 0 && useSkin < static_cast<int32_t>(list.size())) {
                *skin = list[static_cast<size_t>(useSkin)].first;
                *description = list[static_cast<size_t>(useSkin)].second;
            } else {
                skin->clear();
                description->clear();
            }
        }

        void nativeGetNextInt(UObject*, Frame& stack, void* result)
        {
            std::string className = vm::get<std::string>(stack);
            int32_t number = vm::get<int32_t>(stack);
            vm::finish(stack);
            std::vector<std::pair<std::string, std::string>> entries = intEntries(className);
            vm::ret<std::string>(result, number >= 0 && static_cast<size_t>(number) < entries.size() ? entries[static_cast<size_t>(number)].first : std::string());
        }

        void nativeGetNextIntDesc(UObject*, Frame& stack, void*)
        {
            std::string className = vm::get<std::string>(stack);
            int32_t number = vm::get<int32_t>(stack);
            std::string* entry = vm::getRef<std::string>(stack);
            std::string* description = vm::getRef<std::string>(stack);
            vm::finish(stack);
            std::vector<std::pair<std::string, std::string>> entries = intEntries(className);
            bool found = number >= 0 && static_cast<size_t>(number) < entries.size();
            *entry = found ? entries[static_cast<size_t>(number)].first : std::string();
            *description = found ? entries[static_cast<size_t>(number)].second : std::string();
        }

        float sheerSnap(float sheer)
        {
            double value = static_cast<double>(sheer);
            if (value < -0.65) {
                return static_cast<float>(value + 0.15);
            }
            if (value > 0.65) {
                return static_cast<float>(value - 0.15);
            }
            if (value < -0.55) {
                return -0.5f;
            }
            if (value > 0.55) {
                return 0.5f;
            }
            if (value < -0.05) {
                return static_cast<float>(value + 0.05);
            }
            if (value > 0.05) {
                return static_cast<float>(value - 0.05);
            }
            return 0.0f;
        }

        Coords coordsScale(Coords coords, UObject* self, const char* property)
        {
            UProperty* scaleProperty = self->cls->findProperty(Name(property));
            uint8_t* data = self->data + scaleProperty->offset;
            UStruct* type = scaleProperty->structType;
            Vector scale = *reinterpret_cast<Vector*>(data + type->findProperty(Name("Scale"))->offset);
            float sheer = sheerSnap(*reinterpret_cast<float*>(data + type->findProperty(Name("SheerRate"))->offset));
            uint8_t axis = *(data + type->findProperty(Name("SheerAxis"))->offset);
            Coords sheering { {}, { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
            if (axis == 1) {
                sheering.xAxis.y = sheer;
            } else if (axis == 2) {
                sheering.xAxis.z = sheer;
            } else if (axis == 3) {
                sheering.yAxis.x = sheer;
            } else if (axis == 4) {
                sheering.yAxis.z = sheer;
            } else if (axis == 5) {
                sheering.zAxis.x = sheer;
            } else if (axis == 6) {
                sheering.zAxis.y = sheer;
            }
            coords = coordsTransform(coords, sheering);
            coords.xAxis = coords.xAxis * scale;
            coords.yAxis = coords.yAxis * scale;
            coords.zAxis = coords.zAxis * scale;
            coords.origin = { coords.origin.x / scale.x, coords.origin.y / scale.y, coords.origin.z / scale.z };
            return coords;
        }

        void includePoint(Vector& minimum, Vector& maximum, bool& valid, const Vector& point)
        {
            if (!valid) {
                minimum = point;
                maximum = point;
                valid = true;
                return;
            }
            minimum = { std::min(minimum.x, point.x), std::min(minimum.y, point.y), std::min(minimum.z, point.z) };
            maximum = { std::max(maximum.x, point.x), std::max(maximum.y, point.y), std::max(maximum.z, point.z) };
        }

        void includeBox(Vector& minimum, Vector& maximum, bool& valid, const Vector& boxMin, const Vector& boxMax, const Coords& coords)
        {
            for (int i = 0; i < 8; i++) {
                Vector corner { (i & 4) ? boxMax.x : boxMin.x, (i & 2) ? boxMax.y : boxMin.y, (i & 1) ? boxMax.z : boxMin.z };
                includePoint(minimum, maximum, valid, transformPointBy(corner, coords));
            }
        }

        bool renderBounds(UObject* self, bool exact, const Vector& location, const Rotator& rotation, Vector& minimum, Vector& maximum)
        {
            Vector boxMin;
            Vector boxMax;
            bool valid = false;
            UModel* model = cast<UModel>(actor::Brush(self));
            UMesh* mesh = cast<UMesh>(actor::Mesh(self));
            const Coords unit { {}, { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
            if (model) {
                objects::ensureLoaded(model);
                Coords coords = unit;
                coords.origin = coords.origin - location;
                if (isBrush(self)) {
                    coords = coordsScale(coords, self, "PostScale");
                    coords = coordsRotate(coords, rotation);
                    coords = coordsScale(coords, self, "MainScale");
                    coords.origin = coords.origin + actor::PrePivot(self);
                } else {
                    coords = coordsRotate(coords, rotation);
                }
                if (!exact || !model->polys) {
                    includeBox(boxMin, boxMax, valid, model->bounds.min, model->bounds.max, coords);
                } else {
                    for (const Poly& poly : model->polys->polys) {
                        for (const Vector& vertex : poly.vertices) {
                            includePoint(boxMin, boxMax, valid, transformPointBy(vertex, coords));
                        }
                    }
                }
            } else if (mesh) {
                objects::ensureLoaded(mesh);
                Vector frameMin = mesh->meshBounds.min;
                Vector frameMax = mesh->meshBounds.max;
                const AnimSequence* sequence = mesh->findSequence(actor::AnimSequence(self));
                if (sequence && sequence->numFrames > 0 && !(actor::AnimFrame(self) < 0)) {
                    float scaledFrame = static_cast<float>((static_cast<double>(actor::AnimFrame(self)) + 1.0) * sequence->numFrames);
                    int32_t frame = static_cast<int32_t>(std::nearbyint(static_cast<double>(scaledFrame) - 0.5));
                    size_t first = static_cast<size_t>(sequence->startFrame + frame % sequence->numFrames);
                    size_t second = static_cast<size_t>(sequence->startFrame + (frame + 1) % sequence->numFrames);
                    if (first < mesh->frameBounds.size() && second < mesh->frameBounds.size()) {
                        const BoundingBox& a = mesh->frameBounds[first];
                        const BoundingBox& b = mesh->frameBounds[second];
                        frameMin = { std::min(a.min.x, b.min.x), std::min(a.min.y, b.min.y), std::min(a.min.z, b.min.z) };
                        frameMax = { std::max(a.max.x, b.max.x), std::max(a.max.y, b.max.y), std::max(a.max.z, b.max.z) };
                    }
                }
                float drawScale = actor::bParticles(self) ? 1.5f : actor::DrawScale(self);
                Vector scale = mesh->scale * drawScale;
                Vector one { 1, 1, 1 };
                Vector scaledMin = scale * (frameMin - mesh->origin) - one;
                Vector scaledMax = scale * (frameMax - mesh->origin) + one;
                Coords coords = coordsUnrotate(coordsUnrotate(unit, mesh->rotOrigin), rotation);
                coords.origin = location + actor::PrePivot(self);
                includeBox(boxMin, boxMax, valid, scaledMin, scaledMax, coordsTranspose(coords));
            } else {
                Vector extent = cylinderExtent(self) + Vector { 1, 1, 1 };
                boxMin = location - extent;
                boxMax = location + extent;
                valid = true;
            }
            minimum = boxMin;
            maximum = boxMax;
            return valid;
        }

        void nativeGetCollisionRadius(UObject* self, Frame& stack, void* result)
        {
            vm::finish(stack);
            Vector minimum;
            Vector maximum;
            renderBounds(self, false, actor::Location(self), actor::Rotation(self), minimum, maximum);
            vm::ret<float>(result, (maximum - minimum).size() * 0.5f);
        }

        void nativeGetTexture(UObject* self, Frame& stack, void* result)
        {
            vm::finish(stack);
            UModel* brush = cast<UModel>(actor::Brush(self));
            UModel* level = World::current && World::current->level ? World::current->level->model : nullptr;
            std::vector<UObject*> candidates;
            if (brush && level) {
                objects::ensureLoaded(brush);
                for (const BspNode& node : brush->nodes) {
                    if (node.surf < 0 || static_cast<size_t>(node.surf) >= level->surfs.size()) {
                        continue;
                    }
                    const BspSurf& surf = level->surfs[static_cast<size_t>(node.surf)];
                    if (surf.polyFlags & bsp::PF_Invisible) {
                        continue;
                    }
                    UObject* surfTexture = surf.texture;
                    if (surfTexture) {
                        objects::ensureLoaded(surfTexture);
                        if (UObject* current = texture::AnimCurrent(surfTexture)) {
                            surfTexture = current;
                        }
                    }
                    candidates.push_back(surfTexture);
                }
            }
            UObject* best = nullptr;
            std::ptrdiff_t bestCount = 0;
            for (UObject* candidate : candidates) {
                std::ptrdiff_t count = std::count(candidates.begin(), candidates.end(), candidate);
                if (count > bestCount) {
                    bestCount = count;
                    best = candidate;
                }
            }
            vm::ret<UObject*>(result, best);
        }

        void nativeConsoleCommand(UObject*, Frame& stack, void* result)
        {
            std::string command = vm::get<std::string>(stack);
            vm::finish(stack);
            vm::ret<std::string>(result, console::executeEngine(command));
        }
    }

    namespace engine {
        void registerEngineNatives()
        {
            vm::registerNative(552, nativeMultiplyColorFloat);
            vm::registerNative(550, nativeMultiplyFloatColor);
            vm::registerNative(551, nativeAddColorColor);
            vm::registerNative(549, nativeSubtractColorColor);
            vm::registerNative(278, nativeSpawn);
            vm::registerNative(279, nativeDestroy);
            vm::registerNative(280, nativeSetTimer);
            vm::registerNative(277, nativeTrace);
            vm::registerNative(548, nativeFastTrace);
            vm::registerNative(267, nativeSetLocation);
            vm::registerNative(299, nativeSetRotation);
            vm::registerNative(266, nativeMove);
            vm::registerNative(3969, nativeMoveSmooth);
            vm::registerNative(262, nativeSetCollision);
            vm::registerNative(283, nativeSetCollisionSize);
            vm::registerNative(298, nativeSetBase);
            vm::registerNative(272, nativeSetOwner);
            vm::registerNative(3970, nativeSetPhysics);
            vm::registerNative(3971, nativeAutonomousPhysics);
            vm::registerNative(233, nativeError);
            vm::registerNative(304, nativeAllActors);
            vm::registerNative(305, nativeChildActors);
            vm::registerNative(306, nativeBasedActors);
            vm::registerNative(307, nativeTouchingActors);
            vm::registerNative(309, nativeTraceActors);
            vm::registerNative(310, nativeRadiusActors);
            vm::registerNative(311, nativeVisibleActors);
            vm::registerNative(312, nativeVisibleCollidingActors);
            vm::registerNative(666, nativeTraceTexture);
            vm::registerNative(663, nativeCalcArcVelocity);
            vm::registerNative(668, nativeGetCollisionRadius);
            vm::registerNative(669, nativeGetTexture);
            vm::registerNative(667, nativeLipSyncString);
            vm::registerNative(264, nativePlaySound);
            vm::registerNative("Actor", "PlayOwnedSound", nativePlayOwnedSound);
            vm::registerNative("Actor", "DemoPlaySound", nativePlayOwnedSound);
            vm::registerNative(265, nativeStopSound);
            vm::registerNative("Actor", "GetSoundDuration", nativeGetSoundDuration);
            vm::registerNative(268, nativeStopAllSound);
            vm::registerNative(512, nativeMakeNoise);
            vm::registerNative(532, nativePlayerCanSeeMe);
            vm::registerNative(547, nativeGetURLMap);
            vm::registerNative(539, nativeGetMapName);
            vm::registerNative(545, nativeGetNextSkin);
            vm::registerNative("Actor", "GetNextInt", nativeGetNextInt);
            vm::registerNative("Actor", "GetNextIntDesc", nativeGetNextIntDesc);
            vm::registerNative("Actor", "ConsoleCommand", nativeConsoleCommand);
            registerAnimNatives();
            registerPawnNatives();
            registerMiscNatives();
            registerSkeletalNatives();
            registerRuneINatives();
            registerIpDrvNatives();
            registerUWebNatives();
            registerScriptedTextureNatives();
        }
    }
}
