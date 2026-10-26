#include "core/objects.h"
#include "core/random.h"
#include "core/vm.h"
#include "engine/actor.h"
#include "engine/net.h"
#include "engine/world.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace rune {
    namespace {
        World& world()
        {
            return *World::current;
        }

        bool move(UObject* a, const Vector& delta, CheckResult& hit)
        {
            return world().moveActor(a, delta, actor::Rotation(a), hit, false, false, false, false);
        }

        Vector zoneGravity(UObject* a)
        {
            UObject* zone = zoneOf(a);
            return zone ? zoneinfo::ZoneGravity(zone) : Vector { 0, 0, -950 };
        }

        Vector zoneVelocity(UObject* a)
        {
            UObject* zone = zoneOf(a);
            return zone ? zoneinfo::ZoneVelocity(zone) : Vector {};
        }

        float zoneGroundFriction(UObject* a)
        {
            UObject* zone = zoneOf(a);
            return zone ? zoneinfo::ZoneGroundFriction(zone) : 8.0f;
        }

        float zoneFluidFriction(UObject* a)
        {
            UObject* zone = zoneOf(a);
            return zone ? zoneinfo::ZoneFluidFriction(zone) : 1.2f;
        }

        float zoneTerminalVelocity(UObject* a)
        {
            UObject* zone = zoneOf(a);
            return zone ? zoneinfo::ZoneTerminalVelocity(zone) : 2500.0f;
        }

        bool waterZone(UObject* zone)
        {
            return zone && zoneinfo::bWaterZone(zone);
        }

        Vector safeNormal(const Vector& v)
        {
            float squared = v.sizeSquared();
            if (squared < 1e-8f) {
                return {};
            }
            return v * (1.0f / std::sqrt(squared));
        }

        bool nearlyZero(const Vector& v)
        {
            return std::abs(v.x) < 1e-4f && std::abs(v.y) < 1e-4f && std::abs(v.z) < 1e-4f;
        }

        void twoWallAdjust(const Vector& desired, Vector& delta, const Vector& hitNormal, const Vector& oldNormal, float hitTime)
        {
            if (oldNormal.dot(hitNormal) <= 0) {
                Vector direction = hitNormal.cross(oldNormal).normal();
                delta = direction * (delta.dot(direction) * (1.0f - hitTime));
                if (desired.dot(delta) < 0) {
                    delta = delta * -1.0f;
                }
            } else {
                delta = (delta - hitNormal * delta.dot(hitNormal)) * (1.0f - hitTime);
                if (delta.dot(desired) <= 0) {
                    delta = {};
                }
            }
        }

        int32_t fixedTurn(UObject* a, int32_t current, int32_t desired, int32_t deltaRate)
        {
            if (deltaRate == 0) {
                return current & 65535;
            }
            int32_t result = current & 65535;
            current = result;
            desired = desired & 65535;
            if (actor::bFixedRotationDir(a)) {
                if (actor::bRotateToDesired(a)) {
                    if (deltaRate > 0) {
                        if (current > desired) {
                            desired += 65536;
                        }
                        result += std::min(deltaRate, desired - current);
                    } else {
                        if (current < desired) {
                            current += 65536;
                        }
                        result += std::max(deltaRate, desired - current);
                    }
                } else {
                    result += deltaRate;
                }
            } else if (actor::bRotateToDesired(a)) {
                int32_t rate = std::abs(deltaRate);
                if (current > desired) {
                    if (current - desired < 32768) {
                        result -= std::min(current - desired, rate);
                    } else {
                        result += std::min(desired + 65536 - current, rate);
                    }
                } else {
                    if (desired - current < 32768) {
                        result += std::min(desired - current, rate);
                    } else {
                        result -= std::min(current + 65536 - desired, rate);
                    }
                }
            }
            return result & 65535;
        }

        void actorRotation(UObject* a, float deltaTime)
        {
            bool toDesired = actor::bRotateToDesired(a) != 0;
            if ((!toDesired && !actor::bFixedRotationDir(a)) || (toDesired && actor::Rotation(a) == actor::DesiredRotation(a))) {
                return;
            }
            Rotator rotation = actor::Rotation(a);
            Rotator rate = actor::RotationRate(a);
            Rotator desired = actor::DesiredRotation(a);
            int32_t yaw = static_cast<int32_t>(static_cast<float>(rate.yaw) * deltaTime);
            int32_t pitch = static_cast<int32_t>(static_cast<float>(rate.pitch) * deltaTime);
            int32_t roll = static_cast<int32_t>(static_cast<float>(rate.roll) * deltaTime);
            if (yaw != 0 && (!toDesired || desired.yaw != rotation.yaw)) {
                rotation.yaw = fixedTurn(a, rotation.yaw, desired.yaw, yaw);
            }
            if (pitch != 0 && (!toDesired || desired.pitch != rotation.pitch)) {
                rotation.pitch = fixedTurn(a, rotation.pitch, desired.pitch, pitch);
            }
            if (roll != 0 && (!toDesired || desired.roll != rotation.roll)) {
                rotation.roll = fixedTurn(a, rotation.roll, desired.roll, roll);
            }
            if (!(rotation == actor::Rotation(a))) {
                CheckResult hit;
                world().moveActor(a, {}, rotation, hit, false, false, false, false);
            }
            if (toDesired && actor::Rotation(a) == actor::DesiredRotation(a)) {
                vm::event(a, Name("EndedRotation"));
            }
        }

        void pawnRotation(UObject* a, float deltaTime, const Vector& oldVelocity)
        {
            Rotator rotation = actor::Rotation(a);
            Rotator rate = actor::RotationRate(a);
            Rotator desired = actor::DesiredRotation(a);
            bool player = isPlayerPawn(a);
            if (!player || !(desired == rotation)) {
                int32_t deltaYaw = static_cast<int32_t>(static_cast<float>(rate.yaw) * deltaTime);
                actor::bRotateToDesired(a) = 1;
                actor::bFixedRotationDir(a) = 0;
                if (desired.yaw != rotation.yaw) {
                    rotation.yaw = fixedTurn(a, actor::Rotation(a).yaw, desired.yaw, deltaYaw);
                }
                if (desired.pitch != rotation.pitch) {
                    rotation.pitch = fixedTurn(a, rotation.pitch, desired.pitch, static_cast<int32_t>(static_cast<float>(rate.pitch) * deltaTime));
                }
            }
            int32_t roll = 0;
            if (rate.roll > 0) {
                Vector velocity = actor::Velocity(a);
                int32_t currentRoll = actor::Rotation(a).roll;
                auto settle = [&]() {
                    float smooth = std::min(1.0f, static_cast<float>(8.0 * static_cast<double>(deltaTime)));
                    if (currentRoll < 32768) {
                        return static_cast<int32_t>((1.0f - smooth) * static_cast<float>(currentRoll));
                    }
                    return static_cast<int32_t>(static_cast<float>(65536 - currentRoll) * smooth + static_cast<float>(currentRoll));
                };
                if (actor::Physics(a) == PHYS_Walking && velocity.sizeSquared() < 40000.0f) {
                    roll = settle();
                } else {
                    Vector realAcceleration = (velocity - oldVelocity) / deltaTime;
                    if (realAcceleration.sizeSquared() > 10000.0f) {
                        float maxRoll = actor::Physics(a) == PHYS_Walking ? 4096.0f : 28000.0f;
                        Rotator level { rotation.pitch, rotation.yaw, 0 };
                        float sideways = realAcceleration.dot(rotationAxes(level).yAxis);
                        if (sideways > 0) {
                            roll = std::min(rate.roll, static_cast<int32_t>(sideways * maxRoll / pawn::AccelRate(a)));
                        } else {
                            roll = std::max(65536 - rate.roll, static_cast<int32_t>(static_cast<double>(sideways * maxRoll / pawn::AccelRate(a)) + 65536.0));
                        }
                        int32_t& actorRoll = actor::Rotation(a).roll;
                        actorRoll &= 0xffff;
                        if (roll > 32768) {
                            if (actorRoll < 32768) {
                                actorRoll += 65536;
                            }
                        } else if (actorRoll > 32768) {
                            actorRoll -= 65536;
                        }
                        float smooth = std::min(1.0f, static_cast<float>(5.0 * static_cast<double>(deltaTime)));
                        roll = static_cast<int32_t>(static_cast<float>(roll) * smooth + static_cast<float>(actorRoll) * (1.0f - smooth));
                    } else {
                        roll = settle();
                    }
                }
            }
            rotation.roll = roll;
            if (!(rotation == actor::Rotation(a))) {
                CheckResult hit;
                world().moveActor(a, {}, rotation, hit, false, false, false, false);
            }
            if (player) {
                pawn::ViewRotation(a).yaw = rotation.yaw;
            }
        }

        void calcVelocity(UObject* a, const Vector& accelDir, float deltaTime, float maxSpeed, float friction, bool fluid, bool brake, bool buoyant)
        {
            float effectiveFriction = std::max(fluid ? 1.0f : 0.0f, friction);
            bool walkingPlayer = isPlayerPawn(a) && pawn::bIsWalking(a);
            Vector& velocity = actor::Velocity(a);
            Vector& acceleration = actor::Acceleration(a);
            float accelRate = pawn::AccelRate(a);
            if (brake && acceleration.zero()) {
                Vector old = velocity;
                Vector sum;
                float remainingTime = deltaTime;
                float timeStep = 0.03f;
                while (static_cast<double>(remainingTime) > 0.03) {
                    velocity = velocity - velocity * 2.0f * timeStep * effectiveFriction;
                    if (velocity.dot(old) > 0) {
                        sum = sum + velocity * timeStep / deltaTime;
                    }
                    remainingTime = static_cast<float>(static_cast<double>(remainingTime) - 0.03);
                }
                velocity = velocity - velocity * 2.0f * remainingTime * effectiveFriction;
                if (velocity.dot(old) > 0) {
                    sum = sum + velocity * remainingTime / deltaTime;
                }
                velocity = sum;
                if (velocity.dot(old) < 0 || velocity.sizeSquared() < 100.0f) {
                    velocity = {};
                }
            } else {
                float speed = velocity.size();
                if (walkingPlayer) {
                    if (acceleration.sizeSquared() > 0.09f * accelRate * accelRate) {
                        acceleration = accelDir * (accelRate * 0.3f);
                    }
                } else if (acceleration.sizeSquared() > accelRate * accelRate) {
                    acceleration = accelDir * accelRate;
                }
                velocity = velocity - (velocity - accelDir * speed) * (deltaTime * effectiveFriction);
            }
            velocity = velocity * (1 - (fluid ? friction * deltaTime : 0.0f)) + acceleration * deltaTime;
            if (!isPlayerPawn(a)) {
                maxSpeed *= pawn::DesiredSpeed(a);
            }
            if (buoyant) {
                velocity = velocity + zoneGravity(a) * deltaTime * (1.0f - actor::Buoyancy(a) / actor::Mass(a));
            }
            if (walkingPlayer && velocity.sizeSquared() > 0.09f * maxSpeed * maxSpeed) {
                float speed = velocity.size();
                velocity = velocity / speed;
                velocity = velocity * std::max(0.3f * maxSpeed, speed * (1 - deltaTime * 2 * effectiveFriction));
            } else if (velocity.sizeSquared() > maxSpeed * maxSpeed) {
                velocity = velocity.normal() * maxSpeed;
            }
        }

        void physFalling(UObject* a, float deltaTime, int iterations);
        void physFlying(UObject* a, float deltaTime, int iterations);
        void physSwimming(UObject* a, float deltaTime, int iterations);
        void startSwimming(UObject* a, const Vector& oldVelocity, float timeTick, float remainingTime, int iterations);

        void stepUp(UObject* a, const Vector& gravDir, const Vector& desiredDir, Vector delta, CheckResult& hit)
        {
            Vector down = gravDir * pawn::MaxStepHeight(a);
            Vector up = down * -1.0f;
            move(a, up, hit);
            move(a, delta, hit);
            if (hit.time < 1.0f) {
                UObject* hitActor = hit.actor;
                bool pushable = hitActor && isPlayerPawn(a) && hitActor->isA(engineClasses().decoration) && decoration::bPushable(hitActor) && hit.normal.dot(desiredDir) < -0.9f;
                if (pushable) {
                    actor::bJustTeleported(a) = 1;
                    actor::Velocity(a) = actor::Velocity(a) * (actor::Mass(a) / (actor::Mass(a) + actor::Mass(hitActor)));
                    processHitWall(a, hit.normal, hitActor);
                    if (actor::Physics(a) == PHYS_Falling) {
                        return;
                    }
                } else if (std::abs(hit.normal.z) < 0.2f && hit.time * delta.sizeSquared() > 144.0f) {
                    stepUp(a, gravDir, desiredDir, delta * (1 - hit.time), hit);
                    if (actor::Physics(a) == PHYS_Falling) {
                        return;
                    }
                } else {
                    processHitWall(a, hit.normal, hitActor);
                    Vector original = delta;
                    Vector oldNormal = hit.normal;
                    delta = (delta - hit.normal * delta.dot(hit.normal)) * (1.0f - hit.time);
                    if (delta.dot(original) >= 0) {
                        move(a, delta, hit);
                        if (hit.time < 1.0f) {
                            processHitWall(a, hit.normal, hit.actor);
                            if (actor::Physics(a) == PHYS_Falling) {
                                return;
                            }
                            twoWallAdjust(desiredDir, delta, hit.normal, oldNormal, hit.time);
                            move(a, delta, hit);
                        }
                    }
                }
            }
            move(a, down, hit);
            if (hit.time < 1.0f && hit.normal.z < 0.5f) {
                Vector slide = (down - hit.normal * down.dot(hit.normal)) * (1.0f - hit.time);
                if (slide.dot(down) >= 0) {
                    move(a, slide, hit);
                }
            }
        }

        void physWalking(UObject* a, float deltaTime, int iterations)
        {
            float entryZ = actor::Location(a).z;
            if (region(a).zoneNumber == 0) {
                if (actor::Role(a) == net::ROLE_Authority) {
                    std::fprintf(stderr, "%s fell out of the world!\n", a->name.c_str());
                }
                vm::event(a, Name("FellOutOfWorld"));
                return;
            }
            Vector& velocity = actor::Velocity(a);
            Vector& acceleration = actor::Acceleration(a);
            velocity.z = 0;
            acceleration.z = 0;
            Vector accelDir = acceleration.zero() ? Vector {} : acceleration.normal();
            float speedAdjust = 0;
            if (pawn::SpeedScale(a) == 1) {
                Vector forward = rotationAxes(actor::Rotation(a)).xAxis;
                float dot = safeNormal(velocity).dot(forward);
                speedAdjust = static_cast<float>(static_cast<double>(1.0f - dot) * static_cast<double>(pawn::GroundSpeed(a)) * -0.25);
            }
            calcVelocity(a, accelDir, deltaTime, pawn::GroundSpeed(a) + speedAdjust, zoneGroundFriction(a), false, true, false);
            Vector desiredMove = actor::Velocity(a);
            Vector zoneVel = zoneVelocity(a);
            if (isPlayerPawn(a) || zoneVel.sizeSquared() > 90000.0f) {
                desiredMove = desiredMove + zoneVel * 25.0f * deltaTime;
            }
            desiredMove.z = 0;
            Vector gravDir { 0, 0, zoneGravity(a).z > 0 ? 1.0f : -1.0f };
            float maxStep = pawn::MaxStepHeight(a);
            Vector down = gravDir * (maxStep + 2.0f);
            CheckResult hit;
            actor::OldLocation(a) = actor::Location(a);
            actor::bJustTeleported(a) = 0;
            bool checkedFall = false;
            bool mustJump = false;
            float remaining = deltaTime;
            float radius = actor::CollisionRadius(a);
            float height = actor::CollisionHeight(a);
            Vector extent = cylinderExtent(a);
            while (remaining > 0 && iterations < 8) {
                iterations++;
                float tick;
                if (remaining > 0.05f && (isPlayerPawn(a) || desiredMove.sizeSquared() * remaining * remaining > 400.0f)) {
                    tick = std::min(0.05f, remaining * 0.5f);
                } else {
                    tick = remaining;
                }
                remaining -= tick;
                Vector delta = desiredMove * tick;
                Vector subLocation = actor::Location(a);
                Vector subMove = delta;
                bool zeroMove = nearlyZero(delta);
                if (zeroMove) {
                    remaining = 0;
                    pawn::bHitSlopedWall(a) = 0;
                } else {
                    Vector forwardCheck = accelDir * radius;
                    if (!pawn::bAvoidLedges(a)) {
                        forwardCheck = forwardCheck * 0.5f;
                    }
                    bool cautious = (!isPlayerPawn(a) || pawn::bIsWalking(a)) && !pawn::bCanFly(a);
                    if (cautious) {
                        Vector location = actor::Location(a);
                        Vector destination = location + delta + forwardCheck;
                        world().lineCheck(hit, location, destination, {}, a, TRACE_Pawns | TRACE_World | TRACE_Others);
                        if (hit.time >= 1.0f) {
                            float desiredDist = delta.size();
                            float testDown = std::max(4.0f + maxStep + height, 4.0f + height + radius + desiredDist);
                            world().lineCheck(hit, destination, destination + gravDir * testDown, {}, a, TRACE_Pawns | TRACE_World | TRACE_Others);
                            float maxRadius = std::min(14.0f, 0.5f * radius);
                            bool deep = hit.time >= 1.0f || (hit.normal.z > 0.7f && hit.time * testDown > height + maxStep + 4.0f && hit.time * testDown > height + 4.0f + std::sqrt(std::max(0.0f, 1 - hit.normal.z * hit.normal.z)) * (radius + desiredDist) / hit.normal.z);
                            if (deep) {
                                world().lineCheck(hit, destination, destination + gravDir * (maxStep + 4.0f), { maxRadius, maxRadius, height }, a, TRACE_Pawns | TRACE_World | TRACE_Others);
                            }
                            if (hit.time >= 1.0f) {
                                Vector desiredDir = delta / desiredDist;
                                Vector ledgeDown = gravDir * (height + 6.0f);
                                destination = location + accelDir * desiredDist + forwardCheck;
                                world().lineCheck(hit, destination + ledgeDown, location + ledgeDown - accelDir * (2 * radius), {}, a, TRACE_Pawns | TRACE_World | TRACE_Others);
                                ledgeDown = gravDir * (maxStep + 6.0f);
                                Vector ledgeDir;
                                bool moveForward = false;
                                bool goodMove = false;
                                float desiredSpeed = pawn::DesiredSpeed(a);
                                float groundSpeed = pawn::GroundSpeed(a);
                                if (hit.time < 1.0f) {
                                    if (pawn::bAvoidLedges(a)) {
                                        ledgeDir = hit.normal;
                                        ledgeDir.z = 0;
                                        delta = ledgeDir * (-desiredSpeed * groundSpeed * tick);
                                        if (pawn::bStopAtLedges(a)) {
                                            pawn::MoveTimer(a) = -1.0f;
                                        } else {
                                            pawn::MoveTimer(a) -= 0.25f;
                                        }
                                    } else {
                                        ledgeDir = Vector { hit.normal.y, -hit.normal.x, 0 }.normal();
                                        if (ledgeDir.dot(accelDir) < 0) {
                                            ledgeDir = ledgeDir * -1.0f;
                                        }
                                        float dp = ledgeDir.dot(accelDir);
                                        moveForward = dp < 0.5f || (pawn::bCanJump(a) && dp < 0.7f);
                                        if (dp < 0.7f) {
                                            delta = ledgeDir * (std::min(0.8f, desiredSpeed) * groundSpeed * tick);
                                        } else {
                                            delta = ledgeDir * (desiredSpeed * groundSpeed * tick);
                                        }
                                    }
                                } else {
                                    ledgeDir = { desiredDir.y, -desiredDir.x, 0 };
                                    moveForward = true;
                                    delta = ledgeDir * (std::min(0.8f, desiredSpeed) * groundSpeed * tick);
                                    destination = location + delta;
                                    world().lineCheck(hit, location, destination, extent, a, TRACE_World);
                                    if (hit.time >= 1.0f) {
                                        world().lineCheck(hit, destination, destination + ledgeDown, extent, a, TRACE_World);
                                        if (hit.time >= 1.0f) {
                                            delta = delta * -1.0f;
                                        } else {
                                            goodMove = true;
                                        }
                                    } else {
                                        goodMove = true;
                                    }
                                }
                                if (isPlayerPawn(a)) {
                                    moveForward = false;
                                    if (!goodMove) {
                                        destination = location + delta + forwardCheck;
                                        world().lineCheck(hit, location, destination, extent, a, TRACE_World);
                                        if (hit.time >= 1.0f) {
                                            world().lineCheck(hit, destination, destination + ledgeDown, { maxRadius, maxRadius, height }, a, TRACE_World);
                                        }
                                        if (hit.time >= 1.0f) {
                                            actor::Acceleration(a) = {};
                                            delta = {};
                                        }
                                    }
                                }
                                if (pawn::bCanJump(a) && moveForward) {
                                    if (!vm::isProbing(a, Name("MayFall"))) {
                                        delta = accelDir * desiredDist;
                                    } else if (!checkedFall) {
                                        checkedFall = true;
                                        moveForward = false;
                                        vm::event(a, Name("MayFall"));
                                        if (pawn::bCanJump(a)) {
                                            mustJump = true;
                                            delta = accelDir * desiredDist;
                                        }
                                    }
                                }
                                if (!pawn::bCanJump(a)) {
                                    if (moveForward) {
                                        destination = location + desiredDir * (desiredDist + radius);
                                        world().lineCheck(hit, location, destination, extent, a, TRACE_World);
                                        if (hit.time >= 1.0f) {
                                            world().lineCheck(hit, destination, destination + ledgeDown, extent, a, TRACE_World);
                                            if (hit.time < 1.0f) {
                                                destination = location + desiredDir * desiredDist;
                                                world().lineCheck(hit, destination, destination + ledgeDown, extent, a, TRACE_World);
                                            }
                                        }
                                        if (hit.time < 1.0f) {
                                            delta = desiredDir * desiredDist;
                                        } else {
                                            moveForward = false;
                                            if (random::frand() < 2 * tick) {
                                                pawn::MoveTimer(a) = -1.0f;
                                            } else {
                                                pawn::MoveTimer(a) -= 0.1f;
                                            }
                                        }
                                    }
                                    if (!moveForward && !goodMove) {
                                        destination = location + delta + forwardCheck;
                                        world().lineCheck(hit, location, destination, extent, a, TRACE_World);
                                        if (hit.time >= 1.0f) {
                                            world().lineCheck(hit, destination, destination + ledgeDown, extent, a, TRACE_World);
                                        } else if (hit.normal.dot(desiredDir) < pawn::MinHitWall(a)) {
                                            pawn::MoveTimer(a) = -1.0f;
                                        }
                                        if (hit.time >= 1.0f) {
                                            world().lineCheck(hit, location, location + ledgeDown, { maxRadius, maxRadius, height }, a, TRACE_World);
                                            remaining = 0;
                                            pawn::MoveTimer(a) = -1.0f;
                                            actor::Acceleration(a) = {};
                                            if (hit.time >= 1.0f) {
                                                delta = desiredDir * (-groundSpeed * tick);
                                            } else {
                                                delta = {};
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        subMove = delta;
                    }
                    if (pawn::bHitSlopedWall(a)) {
                        float desiredDist = delta.size();
                        if (desiredDist > 0) {
                            Vector desiredDir = delta / desiredDist;
                            Vector location = actor::Location(a);
                            world().lineCheck(hit, location, location + desiredDir * std::max(30.0f, desiredDist + 4), extent, a, TRACE_World);
                            bool sloped = hit.time < 1.0f && hit.normal.z > 0.01f && hit.normal.z < 0.7f;
                            pawn::bHitSlopedWall(a) = sloped ? 1 : 0;
                            if (sloped) {
                                Vector normal = safeNormal({ hit.normal.x, hit.normal.y, 0 });
                                delta = delta - normal * delta.dot(normal);
                            } else if (isPlayerPawn(a)) {
                                Vector checkLocation = location;
                                checkLocation.z = checkLocation.z - height + maxStep + 4;
                                auto slopedWall = [&](const Vector& direction) {
                                    world().lineCheck(hit, checkLocation, checkLocation + direction * 100.0f, {}, a, TRACE_World);
                                    return hit.time < 1.0f && hit.normal.z > 0.01f && hit.normal.z < 0.7f;
                                };
                                sloped = slopedWall(desiredDir);
                                if (!sloped) {
                                    sloped = slopedWall(safeNormal(Vector { desiredDir.y, -desiredDir.x, 0 } + desiredDir));
                                }
                                if (!sloped) {
                                    sloped = slopedWall(safeNormal(Vector { -desiredDir.y, desiredDir.x, 0 } + desiredDir));
                                }
                                pawn::bHitSlopedWall(a) = sloped ? 1 : 0;
                            }
                        }
                        move(a, delta, hit);
                    } else {
                        move(a, delta, hit);
                        pawn::bHitSlopedWall(a) = (hit.time < 1.0f && hit.normal.z > 0.01f && hit.normal.z < 0.7f) ? 1 : 0;
                    }
                    if (a->destroyed) {
                        return;
                    }
                    if (hit.time < 1.0f) {
                        Vector desiredDir = delta.normal();
                        stepUp(a, gravDir, desiredDir, delta * (1.0f - hit.time), hit);
                        if (actor::Physics(a) == PHYS_Falling) {
                            float desiredDist = subMove.size();
                            float actualDist = (actor::Location(a) - subLocation).size2D();
                            if (desiredDist > 0) {
                                remaining += tick * (1 - std::min(1.0f, actualDist / desiredDist));
                            }
                            vm::event(a, Name("Falling"));
                            if (actor::Physics(a) == PHYS_Falling) {
                                if (remaining > 0.01f) {
                                    physFalling(a, remaining, iterations);
                                }
                            } else if (actor::Physics(a) == PHYS_Flying) {
                                actor::Velocity(a) = { 0, 0, pawn::AirSpeed(a) };
                                actor::Acceleration(a) = { 0, 0, pawn::AccelRate(a) };
                                if (remaining > 0.01f) {
                                    physFlying(a, remaining, iterations);
                                }
                            }
                            return;
                        }
                    }
                    if (actor::Physics(a) == PHYS_Swimming) {
                        startSwimming(a, actor::Velocity(a), tick, remaining, iterations);
                        return;
                    }
                }
                Vector location = actor::Location(a);
                if (zeroMove) {
                    Vector foot = location - Vector { 0, 0, height };
                    world().lineCheck(hit, foot, foot - Vector { 0, 0, 20 }, {}, a, TRACE_World);
                    float floorDist = hit.time * 20;
                    pawn::Floor(a) = hit.time < 1.0f && hit.normal.z >= 0.7f ? hit.normal : Vector {};
                    if (hit.actor == actor::Base(a) && floorDist <= 4.6f && floorDist >= 4.1f) {
                        continue;
                    }
                }
                {
                    world().lineCheck(hit, location, location + down, extent, a, TRACE_Pawns | TRACE_World | TRACE_Others);
                    float floorDist = hit.time * (maxStep + 2.0f);
                    pawn::Floor(a) = hit.time < 1.0f && hit.normal.z >= 0.7f ? hit.normal : Vector {};
                    if (hit.time < 1.0f && (hit.actor != actor::Base(a) || floorDist > 2.4f)) {
                        move(a, down, hit);
                        setSupportBase(a, hit.actor);
                        if (actor::Physics(a) == PHYS_Swimming) {
                            startSwimming(a, actor::Velocity(a), tick, remaining, iterations);
                            return;
                        }
                    } else if (floorDist < 1.9f) {
                        Vector realNormal = hit.normal;
                        move(a, { 0, 0, 2.1f - floorDist }, hit);
                        hit.time = 0;
                        hit.normal = realNormal;
                    }
                    if (!mustJump && hit.time < 1.0f && hit.normal.z >= 0.7f) {
                        float friction = zoneGroundFriction(a);
                        if (hit.normal.z < 1.0f && hit.normal.z * friction < 3.3f) {
                            Vector slide = zoneGravity(a) * (deltaTime * deltaTime / (2 * std::max(0.5f, friction)));
                            Vector slideDelta = slide - hit.normal * slide.dot(hit.normal);
                            if (slideDelta.dot(slide) >= 0) {
                                move(a, slideDelta, hit);
                            }
                            if (actor::Physics(a) == PHYS_Swimming) {
                                startSwimming(a, actor::Velocity(a), tick, remaining, iterations);
                                return;
                            }
                        }
                    } else {
                        if (!mustJump && pawn::bCanJump(a) && !checkedFall && vm::isProbing(a, Name("MayFall"))) {
                            checkedFall = true;
                            vm::event(a, Name("MayFall"));
                        }
                        if (!mustJump && (!pawn::bCanJump(a) || pawn::bIsWalking(a))) {
                            actor::Velocity(a) = {};
                            actor::Acceleration(a) = {};
                            world().farMoveActor(a, actor::OldLocation(a), false, false);
                            pawn::MoveTimer(a) = -1.0f;
                            return;
                        }
                        if (hit.time < 1.0f) {
                            pawn::bHitSlopedWall(a) = 1;
                        }
                        float desiredDist = subMove.size();
                        float actualDist = (actor::Location(a) - subLocation).size2D();
                        if (desiredDist == 0) {
                            remaining = 0;
                        } else {
                            remaining += tick * (1 - std::min(1.0f, actualDist / desiredDist));
                        }
                        actor::Velocity(a).z = 0;
                        vm::event(a, Name("Falling"));
                        if (actor::Physics(a) == PHYS_Walking) {
                            setPhysics(a, PHYS_Falling, nullptr);
                        }
                        if (!mustJump && actor::Physics(a) == PHYS_Falling) {
                            float velocityZ = actor::Velocity(a).z;
                            if (!actor::bJustTeleported(a) && deltaTime > remaining) {
                                actor::Velocity(a) = (actor::Location(a) - actor::OldLocation(a)) / (deltaTime - remaining);
                            }
                            actor::Velocity(a).z = velocityZ;
                            if (remaining > 0.01f) {
                                physFalling(a, remaining, iterations);
                            }
                            return;
                        }
                        move(a, desiredMove * remaining, hit);
                        remaining = 0;
                    }
                }
            }
            float stepDown = entryZ - actor::Location(a).z;
            if (stepDown != 0) {
                actor::PrePivot(a).z += stepDown;
            }
            if (!actor::bJustTeleported(a)) {
                actor::Velocity(a) = (actor::Location(a) - actor::OldLocation(a)) / deltaTime;
            }
            actor::Velocity(a).z = 0;
        }

        Vector fallingVelocity(UObject* a, const Vector& oldVelocity, float tick)
        {
            UObject* zone = zoneOf(a);
            Vector acceleration = actor::Acceleration(a);
            Vector gravity = zoneGravity(a);
            if (!waterZone(zone)) {
                if (a->isA(engineClasses().decoration) && decoration::bBobbing(a)) {
                    return oldVelocity + (acceleration + gravity * 0.5f) * (0.5f * tick);
                }
                if (isPlayerPawn(a)) {
                    UObject* foot = reinterpret_cast<PointRegion*>(pawn::FootRegion(a))->zone;
                    if (waterZone(foot) && oldVelocity.z < 0) {
                        return oldVelocity * (1 - zoneinfo::ZoneFluidFriction(foot) * tick) + (acceleration + gravity) * (0.5f * tick);
                    }
                }
                return oldVelocity + (acceleration + gravity) * (0.5f * tick);
            }
            float mass = std::max(1.0f, actor::Mass(a));
            return oldVelocity * (1 - 2 * zoneFluidFriction(a) * tick) + (acceleration + gravity * (1.0f - actor::Buoyancy(a) / mass)) * (0.5f * tick);
        }

        bool tryGrabEdge(UObject* a, float reach)
        {
            constexpr uint32_t flags = TRACE_Movers | TRACE_Level;
            Vector location = actor::Location(a);
            float radius = actor::CollisionRadius(a);
            float height = actor::CollisionHeight(a);
            Vector extent { radius, radius, height };
            CheckResult hit;
            world().lineCheck(hit, location, location + rotationVector(actor::Rotation(a)) * 4.0f, extent, a, flags);
            if (hit.time >= 1.0f || std::abs(hit.normal.z) >= 0.2f) {
                return false;
            }
            Vector inward = Vector { -hit.normal.x, -hit.normal.y, 0 }.normal() * (radius * 1.5f);
            Vector up = location + Vector { 0, 0, height * 2.0f };
            world().lineCheck(hit, hit.location, up, extent, a, flags);
            Vector over = up + inward;
            if (world().lineCheck(hit, up, over, extent, a, flags)) {
                return false;
            }
            over = over - Vector { 0, 0, height * reach };
            if (!world().lineCheck(hit, up, over, {}, a, flags)) {
                return false;
            }
            CheckResult ledge;
            world().lineCheck(ledge, up, over, extent, a, flags);
            if (ledge.normal.z < 0.7f) {
                return false;
            }
            if (world().encroaches(a, up)) {
                return false;
            }
            float grabDistance = hit.location.z - location.z;
            Vector normal = inward.normal();
            Vector spot { location.x + normal.x * (radius + 4.0f), location.y + normal.y * (radius + 4.0f), location.z + grabDistance + 8.0f + height };
            CheckResult point;
            if (world().pointCheck(point, spot, extent, a, false)) {
                return false;
            }
            vm::Call call(a, Name("GrabEdge"));
            if (!call.valid()) {
                return false;
            }
            call.f(grabDistance).v(normal).run();
            uint8_t* result = call.returnValue();
            return result && *reinterpret_cast<uint32_t*>(result);
        }

        bool grabEdge(UObject* a, float reach)
        {
            uint32_t warping = pawn::bWarping(a);
            pawn::bWarping(a) = 1;
            bool grabbed = tryGrabEdge(a, reach);
            if (!a->destroyed) {
                pawn::bWarping(a) = warping;
            }
            return grabbed;
        }

        void physFalling(UObject* a, float deltaTime, int iterations)
        {
            if (region(a).zoneNumber == 0) {
                if (actor::Role(a) == net::ROLE_Authority && (a->isA(engineClasses().inventory) || a->isA(engineClasses().decoration) || isPawn(a))) {
                    std::fprintf(stderr, "%s fell out of the world!\n", a->name.str().c_str());
                }
                vm::Call(a, Name("FellOutOfWorld")).run();
                return;
            }
            float boundSpeed = 0;
            Vector realAcceleration = actor::Acceleration(a);
            if (isPawn(a)) {
                float airControl = pawn::AirControl(a);
                if (airControl > 0.15f) {
                    Vector testWalk = (actor::Acceleration(a).normal() * (airControl * pawn::AccelRate(a)) + actor::Velocity(a)) * deltaTime;
                    testWalk.z = 0;
                    CheckResult wall;
                    world().lineCheck(wall, actor::Location(a), actor::Location(a) + testWalk, cylinderExtent(a), a, TRACE_Movers | TRACE_Level);
                    if (wall.actor) {
                        airControl = 0.05f;
                    }
                }
                float maxAccel = pawn::AccelRate(a) * airControl;
                Vector& acceleration = actor::Acceleration(a);
                acceleration.z = 0;
                float speed2d = actor::Velocity(a).size2D();
                if (speed2d < 10.0f) {
                    maxAccel = maxAccel + (10 - speed2d) / deltaTime;
                } else if (speed2d >= pawn::GroundSpeed(a)) {
                    if (airControl <= 0.05f) {
                        maxAccel = 1.0f;
                    } else {
                        boundSpeed = speed2d;
                    }
                }
                if (acceleration.sizeSquared() > maxAccel * maxAccel) {
                    acceleration = acceleration.normal() * maxAccel;
                }
            }
            float remaining = deltaTime;
            int bounces = 0;
            CheckResult hit;
            bool adjustApex = false;
            while (remaining > 0 && iterations < 8) {
                iterations++;
                float tick = remaining > 0.1f ? std::min(0.1f, remaining * 0.5f) : remaining;
                remaining -= tick;
                actor::OldLocation(a) = actor::Location(a);
                actor::bJustTeleported(a) = 0;
                Vector oldVelocity = actor::Velocity(a);
                actor::Velocity(a) = fallingVelocity(a, oldVelocity, tick);
                Vector velocity = actor::Velocity(a);
                if (!adjustApex && ((oldVelocity.z > 0) != (velocity.z > 0)) && std::abs(oldVelocity.z) > 5.0f && std::abs(velocity.z) > 5.0f) {
                    adjustApex = true;
                    float part = std::abs(oldVelocity.z) / (std::abs(oldVelocity.z) + std::abs(velocity.z));
                    if (part * tick > 0.015f && (1 - part) * tick > 0.015f) {
                        remaining = remaining + tick * (1 - part);
                        tick = tick * part;
                        actor::Velocity(a) = fallingVelocity(a, oldVelocity, tick);
                    }
                } else {
                    adjustApex = false;
                }
                if (boundSpeed != 0) {
                    Vector velocity2d = actor::Velocity(a);
                    velocity2d.z = 0;
                    if (velocity2d.sizeSquared() > boundSpeed * boundSpeed) {
                        velocity2d = velocity2d.normal() * boundSpeed;
                        velocity2d.z = actor::Velocity(a).z;
                        actor::Velocity(a) = velocity2d;
                    }
                }
                Vector zoneVel;
                if (!isPawn(a) || isPlayerPawn(a) || zoneVelocity(a).sizeSquared() > 40000.0f) {
                    zoneVel = zoneVelocity(a);
                }
                Vector adjusted = (actor::Velocity(a) + zoneVel) * tick;
                if (isPawn(a) && pawn::bCanGrabEdges(a) && grabEdge(a, 2.0f)) {
                    return;
                }
                move(a, adjusted, hit);
                if (a->destroyed) {
                    return;
                }
                if (isPawn(a) && actor::Physics(a) == PHYS_Swimming) {
                    remaining = remaining + tick * (1.0f - hit.time);
                    startSwimming(a, oldVelocity, tick, remaining, iterations);
                    return;
                }
                if (hit.time < 1.0f) {
                    if (isPlayerPawn(hit.actor) && a->isA(engineClasses().decoration)) {
                        decoration::numLandings(a) = std::max(0, decoration::numLandings(a) - 1);
                    }
                    if (actor::bBounce(a)) {
                        vm::Call(a, Name("HitWall")).v(hit.normal).obj(hit.actor).run();
                        if (actor::Physics(a) == PHYS_None || a->destroyed) {
                            return;
                        }
                        if (bounces < 2) {
                            remaining += tick * (1.0f - hit.time);
                        }
                        bounces++;
                    } else if (hit.normal.z > 0.7f) {
                        remaining += tick * (1.0f - hit.time);
                        if (!actor::bJustTeleported(a) && hit.time > 0.1f && hit.time * tick > 0.003f) {
                            actor::Velocity(a) = (actor::Location(a) - actor::OldLocation(a)) / (tick * hit.time);
                        }
                        processLanded(a, hit.normal, hit.actor, remaining, iterations);
                        return;
                    } else {
                        processHitWall(a, hit.normal, hit.actor);
                        if (a->destroyed || actor::Physics(a) != PHYS_Falling) {
                            return;
                        }
                        Vector oldNormal = hit.normal;
                        Vector delta = (adjusted - hit.normal * adjusted.dot(hit.normal)) * (1.0f - hit.time);
                        if (delta.dot(adjusted) >= 0) {
                            move(a, delta, hit);
                            if (hit.time < 1.0f) {
                                if (hit.normal.z > 0.7f) {
                                    processLanded(a, hit.normal, hit.actor, 0, iterations);
                                    return;
                                }
                                processHitWall(a, hit.normal, hit.actor);
                                Vector desiredDir = adjusted.normal();
                                twoWallAdjust(desiredDir, delta, hit.normal, oldNormal, hit.time);
                                bool ditch = oldNormal.z > 0 && hit.normal.z > 0 && delta.z == 0 && hit.normal.dot(oldNormal) < 0;
                                move(a, delta, hit);
                                if (ditch || hit.normal.z > 0.7f) {
                                    processLanded(a, hit.normal, hit.actor, 0, iterations);
                                    return;
                                }
                            }
                        }
                        float oldZ = oldVelocity.z;
                        oldVelocity = (actor::Location(a) - actor::OldLocation(a)) / tick;
                        oldVelocity.z = oldZ;
                    }
                }
                if (!actor::bBounce(a) && !actor::bJustTeleported(a)) {
                    Vector newVelocity = (actor::Location(a) - actor::OldLocation(a)) / tick - zoneVel;
                    if (newVelocity.z < oldVelocity.z || oldVelocity.z >= 0) {
                        newVelocity = newVelocity * 2.0f - oldVelocity;
                    }
                    float terminal = zoneTerminalVelocity(a);
                    if (newVelocity.sizeSquared() > terminal * terminal) {
                        newVelocity = newVelocity.normal() * terminal;
                    }
                    actor::Velocity(a) = newVelocity;
                }
            }
            actor::Acceleration(a) = realAcceleration;
        }

        void physFlying(UObject* a, float deltaTime, int iterations)
        {
            if (actor::bCollideWorld(a) && region(a).zoneNumber == 0) {
                if (!pawn::bIsPlayer(a)) {
                    std::fprintf(stderr, "%s flew out of the world!\n", a->name.c_str());
                    world().destroy(a);
                }
                return;
            }
            Vector acceleration = actor::Acceleration(a);
            Vector accelDir = acceleration.zero() ? Vector {} : acceleration.normal();
            calcVelocity(a, accelDir, deltaTime, pawn::AirSpeed(a), zoneFluidFriction(a), true, false, false);
            actor::OldLocation(a) = actor::Location(a);
            actor::bJustTeleported(a) = 0;
            Vector zoneVel = (isPlayerPawn(a) || zoneVelocity(a).sizeSquared() > 90000.0f) ? zoneVelocity(a) : Vector {};
            Vector adjusted = (actor::Velocity(a) + zoneVel) * deltaTime;
            CheckResult hit;
            move(a, adjusted, hit);
            if (hit.time < 1.0f && !a->destroyed) {
                Vector gravDir { 0, 0, zoneGravity(a).z > 0 ? 1.0f : -1.0f };
                Vector desiredDir = adjusted.normal();
                float upDown = gravDir.dot(actor::Velocity(a).normal());
                if (std::abs(hit.normal.z) < 0.2f && upDown < 0.5f && upDown > -0.2f) {
                    float stepZ = actor::Location(a).z;
                    stepUp(a, gravDir, desiredDir, adjusted * (1.0f - hit.time), hit);
                    actor::OldLocation(a).z = actor::Location(a).z + (actor::OldLocation(a).z - stepZ);
                } else {
                    processHitWall(a, hit.normal, hit.actor);
                    Vector oldNormal = hit.normal;
                    Vector delta = (adjusted - hit.normal * adjusted.dot(hit.normal)) * (1.0f - hit.time);
                    if (delta.dot(adjusted) >= 0) {
                        move(a, delta, hit);
                        if (hit.time < 1.0f) {
                            processHitWall(a, hit.normal, hit.actor);
                            twoWallAdjust(desiredDir, delta, hit.normal, oldNormal, hit.time);
                            move(a, delta, hit);
                        }
                    }
                }
            }
            if (!actor::bJustTeleported(a)) {
                actor::Velocity(a) = (actor::Location(a) - actor::OldLocation(a)) / deltaTime;
            }
            (void)iterations;
        }

        void findWaterLine(UObject* a, Vector start, Vector& end)
        {
            bool water = waterZone(zoneOf(a));
            while ((end - start).sizeSquared() >= 1.0f) {
                Vector midPoint = (start + end) * 0.5f;
                if (waterZone(world().zoneAt(midPoint, nullptr, nullptr)) != water) {
                    start = midPoint;
                } else {
                    end = midPoint;
                }
            }
        }

        float swim(UObject* a, const Vector& delta, CheckResult& hit)
        {
            Vector start = actor::Location(a);
            float airTime = 0;
            move(a, delta, hit);
            Vector end = actor::Location(a);
            if (!waterZone(zoneOf(a))) {
                findWaterLine(a, start, end);
                if (!(end == actor::Location(a))) {
                    airTime = (end - actor::Location(a)).size() / delta.size();
                    move(a, end - actor::Location(a), hit);
                }
            }
            return airTime;
        }

        void physSwimming(UObject* a, float deltaTime, int iterations)
        {
            UObject* head = reinterpret_cast<PointRegion*>(pawn::HeadRegion(a))->zone;
            if (!waterZone(head) && actor::Velocity(a).z > 100.0f) {
                actor::Velocity(a).z = actor::Velocity(a).z * (1 - deltaTime);
            }
            actor::bJustTeleported(a) = 0;
            iterations++;
            actor::OldLocation(a) = actor::Location(a);
            Vector acceleration = actor::Acceleration(a);
            Vector accelDir = acceleration.zero() ? acceleration : safeNormal(acceleration);
            calcVelocity(a, accelDir, deltaTime, pawn::WaterSpeed(a), zoneFluidFriction(a), true, false, true);
            float velocityZ = actor::Velocity(a).z;
            Vector zoneVel;
            if (isPlayerPawn(a) || zoneVelocity(a).sizeSquared() > 90000.0f) {
                zoneVel = zoneVelocity(a) * 25.0f * deltaTime;
            }
            if (actor::bNoSurfaceBob(a) && !waterZone(head) && actor::Velocity(a).z > 0) {
                actor::Velocity(a).z = 0;
            }
            Vector adjusted = (actor::Velocity(a) + zoneVel) * deltaTime;
            CheckResult hit;
            float remaining = deltaTime * swim(a, adjusted, hit);
            if (a->destroyed) {
                return;
            }
            if (hit.time < 1.0f) {
                Vector gravDir { 0, 0, zoneGravity(a).z > 0 ? 1.0f : -1.0f };
                Vector desiredDir = safeNormal(adjusted);
                float upDown = gravDir.dot(safeNormal(actor::Velocity(a)));
                if (std::abs(hit.normal.z) < 0.2f && upDown < 0.5f && upDown > -0.2f) {
                    if (pawn::bCanGrabEdges(a) && grabEdge(a, 2.5f)) {
                        return;
                    }
                    float stepZ = actor::Location(a).z;
                    stepUp(a, gravDir, desiredDir, adjusted * (1.0f - hit.time), hit);
                    actor::OldLocation(a).z = actor::Location(a).z + (actor::OldLocation(a).z - stepZ);
                } else {
                    processHitWall(a, hit.normal, hit.actor);
                    Vector oldHitNormal = hit.normal;
                    Vector delta = (adjusted - hit.normal * adjusted.dot(hit.normal)) * (1.0f - hit.time);
                    if (delta.dot(adjusted) >= 0) {
                        remaining = remaining * (1.0f - hit.time) * swim(a, delta, hit);
                        if (hit.time < 1.0f) {
                            processHitWall(a, hit.normal, hit.actor);
                            twoWallAdjust(desiredDir, delta, hit.normal, oldHitNormal, hit.time);
                            remaining = remaining * (1.0f - hit.time) * swim(a, delta, hit);
                        }
                    }
                }
            }
            if (a->destroyed) {
                return;
            }
            if (!actor::bJustTeleported(a) && remaining < deltaTime) {
                bool waterJump = velocityZ != actor::Velocity(a).z;
                if (waterJump) {
                    velocityZ = actor::Velocity(a).z;
                }
                actor::Velocity(a) = (actor::Location(a) - actor::OldLocation(a)) / (deltaTime - remaining);
                if (waterJump) {
                    actor::Velocity(a).z = velocityZ;
                }
            }
            if (!waterZone(zoneOf(a))) {
                if (actor::Physics(a) == PHYS_Swimming) {
                    setPhysics(a, PHYS_Falling, nullptr);
                }
                Vector& velocity = actor::Velocity(a);
                if (velocity.z < 160.0f && velocity.z > 0) {
                    velocity.z = static_cast<float>(static_cast<double>(velocity.size2D()) * 0.4 + 40.0);
                }
            }
            if (remaining > 0.01f) {
                if (actor::Physics(a) == PHYS_Falling) {
                    physFalling(a, remaining, iterations);
                } else if (actor::Physics(a) == PHYS_Flying) {
                    physFlying(a, remaining, iterations);
                }
            }
        }

        void startSwimming(UObject* a, const Vector& oldVelocity, float timeTick, float remainingTime, int iterations)
        {
            Vector end = actor::Location(a);
            findWaterLine(a, actor::OldLocation(a), end);
            float waterTime = 0;
            if (!(end == actor::Location(a))) {
                waterTime = timeTick * (end - actor::Location(a)).size() / (actor::Location(a) - actor::OldLocation(a)).size();
                remainingTime += waterTime;
                CheckResult hit;
                move(a, end - actor::Location(a), hit);
            }
            if (!actor::bBounce(a) && !actor::bJustTeleported(a)) {
                Vector velocity = (actor::Location(a) - actor::OldLocation(a)) / (timeTick - waterTime);
                velocity = velocity * 2.0f - oldVelocity;
                if (velocity.sizeSquared() > 16000000.0f) {
                    velocity = safeNormal(velocity) * 4000.0f;
                }
                actor::Velocity(a) = velocity;
            }
            Vector& velocity = actor::Velocity(a);
            if (velocity.z > -160.0f && velocity.z < 0) {
                velocity.z = static_cast<float>(-80.0 - static_cast<double>(velocity.size2D()) * 0.7);
            }
            if (remainingTime > 0.01f) {
                physSwimming(a, remainingTime, iterations);
            }
        }

        void physProjectile(UObject* a, float deltaTime, int iterations)
        {
            if (region(a).zoneNumber == 0) {
                world().destroy(a);
                return;
            }
            float remaining = deltaTime;
            int bounces = 0;
            actor::OldLocation(a) = actor::Location(a);
            actor::bJustTeleported(a) = 0;
            CheckResult hit;
            while (remaining > 0 && iterations < 8) {
                iterations++;
                if (waterZone(zoneOf(a))) {
                    actor::Velocity(a) = actor::Velocity(a) * (1 - 0.2f * zoneFluidFriction(a) * remaining);
                }
                actor::Velocity(a) = actor::Velocity(a) + actor::Acceleration(a) * remaining;
                float tick = remaining;
                remaining = 0;
                if (a->isA(engineClasses().projectile)) {
                    float maxSpeed = projectile::MaxSpeed(a);
                    if (actor::Velocity(a).sizeSquared() > maxSpeed * maxSpeed) {
                        actor::Velocity(a) = actor::Velocity(a).normal() * maxSpeed;
                    }
                }
                Vector adjusted = actor::Velocity(a) * deltaTime;
                move(a, adjusted, hit);
                if (hit.time < 1.0f && !a->destroyed && !actor::bJustTeleported(a)) {
                    vm::Call(a, Name("HitWall")).v(hit.normal).obj(hit.actor).run();
                    if (a->destroyed) {
                        return;
                    }
                    if (actor::bBounce(a)) {
                        if (bounces < 2) {
                            remaining = tick * (1.0f - hit.time);
                        }
                        bounces++;
                        if (actor::Physics(a) == PHYS_Falling) {
                            physFalling(a, remaining, iterations);
                        }
                    }
                }
            }
            if (!actor::bBounce(a) && !actor::bJustTeleported(a) && !a->destroyed) {
                actor::Velocity(a) = (actor::Location(a) - actor::OldLocation(a)) / deltaTime;
            }
        }

        int32_t unwind(int32_t value, int32_t reference)
        {
            int32_t delta = (value - reference) & 65535;
            if (delta > 32767) {
                delta -= 65536;
            }
            return reference + delta;
        }

        UObject* pointLink(UObject* point, const char* name)
        {
            uint8_t* address = point ? point->propertyAddress(Name(name)) : nullptr;
            return address ? *reinterpret_cast<UObject**>(address) : nullptr;
        }

        float pointFloat(UObject* point, const char* name)
        {
            return *reinterpret_cast<float*>(point->propertyAddress(Name(name)));
        }

        bool pointFlag(UObject* point, const char* name)
        {
            return *reinterpret_cast<uint32_t*>(point->propertyAddress(Name(name))) != 0;
        }

        Vector pointVector(UObject* point, const char* name)
        {
            return *reinterpret_cast<Vector*>(point->propertyAddress(Name(name)));
        }

        void splineWeights(float t, bool throughPoints, float* w)
        {
            float t2 = t * t;
            float t3 = t2 * t;
            if (throughPoints) {
                w[0] = 0.5f * (-t3 + 2 * t2 - t);
                w[1] = 0.5f * (3 * t3 - 5 * t2 + 2);
                w[2] = 0.5f * (-3 * t3 + 4 * t2 + t);
                w[3] = 0.5f * (t3 - t2);
                return;
            }
            float u = 1 - t;
            w[0] = u * u * u / 6;
            w[1] = (3 * t3 - 6 * t2 + 4) / 6;
            w[2] = (-3 * t3 + 3 * t2 + 3 * t + 1) / 6;
            w[3] = t3 / 6;
        }

        UObject* pathViewer(UObject* a)
        {
            if (isPlayerPawn(a)) {
                return a;
            }
            UObject* owner = actor::Owner(a);
            return owner && isPlayerPawn(owner) ? owner : nullptr;
        }

        void physPathing(UObject* a, float deltaTime)
        {
            UClass* interpolationClass = engineClasses().interpolationPoint;
            auto isPoint = [&](UObject* o) {
                return o && interpolationClass && o->isA(interpolationClass);
            };
            int guard = 0;
            while (actor::PhysRate(a) != 0 && actor::bInterpolating(a) && deltaTime > 0 && guard++ < 16) {
                UObject* target = actor::Target(a);
                UObject* dest = isPoint(target) ? target : nullptr;
                UObject* next = pointLink(dest, "Next");
                float& alpha = actor::PhysAlpha(a);
                float rateModifier = 1.0f;
                if (dest && next) {
                    rateModifier = pointFloat(dest, "RateModifier") * (1 - alpha) + pointFloat(next, "RateModifier") * alpha;
                    UObject* level = actor::Level(a);
                    if (level) {
                        levelinfo::TimeDilation(level) = pointFloat(dest, "GameSpeedModifier") * (1 - alpha) + pointFloat(next, "GameSpeedModifier") * alpha;
                    }
                    UObject* viewer = pathViewer(a);
                    if (viewer) {
                        float flash = pointFloat(dest, "ScreenFlashScale") * (1 - alpha) + pointFloat(next, "ScreenFlashScale") * alpha;
                        playerpawn::DesiredFlashScale(viewer) = flash;
                        playerpawn::FlashScale(viewer) = { flash, flash, flash };
                        Vector fog = pointVector(dest, "ScreenFlashFog") * (1 - alpha) + pointVector(next, "ScreenFlashFog") * alpha;
                        playerpawn::DesiredFlashFog(viewer) = fog;
                        playerpawn::FlashFog(viewer) = fog;
                        float fov = pointFloat(dest, "FovModifier") * (1 - alpha) + pointFloat(next, "FovModifier") * alpha;
                        pawn::FovAngle(viewer) = fov * pawn::FovAngle(viewer->cls->defaultObject);
                    }
                }
                float oldAlpha = alpha;
                float destAlpha = alpha + actor::PhysRate(a) * rateModifier * deltaTime;
                alpha = std::clamp(destAlpha, 0.0f, 1.0f);
                if (dest && next) {
                    if (pointFlag(next, "bInterpInstant")) {
                        destAlpha = 1.01f;
                        alpha = 1.0f;
                    }
                    UObject* prev = pointLink(dest, "Prev");
                    UObject* nextNext = pointLink(next, "Next");
                    Vector newLocation;
                    Rotator newRotation;
                    if (prev && nextNext && (actor::Location(dest) - actor::Location(next)).size() > 16.0f) {
                        float w[4];
                        splineWeights(alpha, pointFlag(dest, "bSplineThruPoints"), w);
                        UObject* points[4] = { prev, dest, next, nextNext };
                        UObject* prevPrev = pointLink(prev, "Prev");
                        Rotator chain = prevPrev ? actor::Rotation(prevPrev) : Rotator {};
                        float pitch = 0;
                        float yaw = 0;
                        float roll = 0;
                        for (int i = 0; i < 4; i++) {
                            Rotator rotation = actor::Rotation(points[i]);
                            chain = { unwind(rotation.pitch, chain.pitch), unwind(rotation.yaw, chain.yaw), unwind(rotation.roll, chain.roll) };
                            newLocation = newLocation + actor::Location(points[i]) * w[i];
                            pitch += static_cast<float>(chain.pitch) * w[i];
                            yaw += static_cast<float>(chain.yaw) * w[i];
                            roll += static_cast<float>(chain.roll) * w[i];
                        }
                        newRotation = { static_cast<int32_t>(pitch), static_cast<int32_t>(yaw), static_cast<int32_t>(roll) };
                    } else {
                        newLocation = actor::Location(dest) * (1 - alpha) + actor::Location(next) * alpha;
                        Rotator from = actor::Rotation(dest);
                        Rotator to = actor::Rotation(next);
                        newRotation = {
                            static_cast<int32_t>(static_cast<float>(from.pitch) * (1 - alpha) + static_cast<float>(to.pitch) * alpha),
                            static_cast<int32_t>(static_cast<float>(from.yaw) * (1 - alpha) + static_cast<float>(to.yaw) * alpha),
                            static_cast<int32_t>(static_cast<float>(from.roll) * (1 - alpha) + static_cast<float>(to.roll) * alpha),
                        };
                    }
                    CheckResult hit;
                    world().moveActor(a, newLocation - actor::Location(a), newRotation, hit, false, false, false, false);
                    if (isPawn(a)) {
                        pawn::ViewRotation(a) = actor::Rotation(a);
                    }
                }
                if (actor::PhysRate(a) > 0 && destAlpha > 1.0f) {
                    alpha = 0;
                    deltaTime *= (destAlpha - 1.0f) / (destAlpha - oldAlpha);
                    if (dest) {
                        UObject* reached = next;
                        while (isPoint(reached) && pointFlag(reached, "bSkipNextPath")) {
                            reached = pointLink(reached, "Next");
                        }
                        actor::Target(a) = reached;
                    }
                    UObject* reached = actor::Target(a);
                    if (reached) {
                        vm::Call(reached, Name("InterpolateEnd")).obj(a).run();
                        vm::Call(a, Name("InterpolateEnd")).obj(reached).run();
                    }
                } else if (actor::PhysRate(a) < 0 && destAlpha < 0) {
                    alpha = 1;
                    deltaTime *= (0 - destAlpha) / (oldAlpha - destAlpha);
                    if (dest) {
                        UObject* reached = pointLink(dest, "Prev");
                        while (isPoint(reached) && pointFlag(reached, "bSkipNextPath")) {
                            reached = pointLink(reached, "Prev");
                        }
                        actor::Target(a) = reached;
                    }
                    UObject* reached = actor::Target(a);
                    if (reached) {
                        vm::Call(reached, Name("InterpolateEnd")).obj(a).run();
                        vm::Call(a, Name("InterpolateEnd")).obj(reached).run();
                    }
                    vm::Call(a, Name("InterpolateEnd")).obj(nullptr).run();
                } else {
                    deltaTime = 0;
                }
            }
        }

        void physMovingBrush(UObject* a, float deltaTime)
        {
            if (!isMovingBrush(a)) {
                return;
            }
            bool polyobj = isPolyobj(a);
            int32_t keyNum = polyobj ? std::clamp<int32_t>(polyobj::KeyNum(a), 0, 15) : std::clamp<int32_t>(mover::KeyNum(a), 0, 15);
            uint8_t glideType = polyobj ? polyobj::PolyObjGlideType(a) : mover::MoverGlideType(a);
            int guard = 0;
            while (actor::bInterpolating(a) && deltaTime > 0 && guard++ < 8) {
                float alpha = actor::PhysAlpha(a);
                float newAlpha = alpha + deltaTime * actor::PhysRate(a);
                if (newAlpha > 1.0f) {
                    deltaTime *= (newAlpha - 1.0f) / (newAlpha - alpha);
                    newAlpha = 1.0f;
                } else {
                    deltaTime = 0;
                }
                float renderAlpha = newAlpha;
                if (glideType == 1) {
                    renderAlpha = 3.0f * newAlpha * newAlpha - 2.0f * newAlpha * newAlpha * newAlpha;
                }
                Vector oldPos = polyobj ? polyobj::OldPos(a) : mover::OldPos(a);
                Rotator oldRot = polyobj ? polyobj::OldRot(a) : mover::OldRot(a);
                Vector targetPos = polyobj ? polyobj::BasePos(a) + polyobj::KeyPos(a, keyNum) : mover::BasePos(a) + mover::KeyPos(a, keyNum);
                Rotator baseRot = polyobj ? polyobj::BaseRot(a) : mover::BaseRot(a);
                Rotator keyRot = polyobj ? polyobj::KeyRot(a, keyNum) : mover::KeyRot(a, keyNum);
                Rotator targetRot { baseRot.pitch + keyRot.pitch, baseRot.yaw + keyRot.yaw, baseRot.roll + keyRot.roll };
                Vector newLocation = oldPos + (targetPos - oldPos) * renderAlpha;
                Rotator newRotation {
                    oldRot.pitch + static_cast<int32_t>(static_cast<float>(targetRot.pitch - oldRot.pitch) * renderAlpha),
                    oldRot.yaw + static_cast<int32_t>(static_cast<float>(targetRot.yaw - oldRot.yaw) * renderAlpha),
                    oldRot.roll + static_cast<int32_t>(static_cast<float>(targetRot.roll - oldRot.roll) * renderAlpha),
                };
                CheckResult hit;
                if (world().moveActor(a, newLocation - actor::Location(a), newRotation, hit, false, false, false, false)) {
                    actor::PhysAlpha(a) = newAlpha;
                    if (newAlpha == 1.0f) {
                        actor::bInterpolating(a) = 0;
                        vm::Call(a, Name("InterpolateEnd")).obj(nullptr).run();
                    }
                }
            }
        }

        bool checkFloor(UObject* a, const Vector& direction, CheckResult& hit)
        {
            Vector location = actor::Location(a);
            world().lineCheck(hit, location, location - direction * pawn::MaxStepHeight(a), cylinderExtent(a), nullptr, TRACE_World);
            if (hit.time < 1.0f) {
                pawn::Floor(a) = hit.normal;
                return true;
            }
            return false;
        }

        bool findNewFloor(UObject* a, const Vector& oldLocation, float deltaTime, float remainingTime, int iterations)
        {
            CheckResult hit;
            for (const Vector& direction : { Vector { 0, 0, 1 }, Vector { 0, 1, 0 }, Vector { 0, -1, 0 }, Vector { 1, 0, 0 }, Vector { -1, 0, 0 } }) {
                if (checkFloor(a, direction, hit)) {
                    return true;
                }
            }
            vm::event(a, Name("Falling"));
            if (actor::Physics(a) == PHYS_Spider) {
                setPhysics(a, PHYS_Falling, nullptr);
            }
            if (actor::Physics(a) == PHYS_Falling) {
                float velocityZ = actor::Velocity(a).z;
                if (!actor::bJustTeleported(a) && deltaTime > remainingTime) {
                    actor::Velocity(a) = (actor::Location(a) - oldLocation) / (deltaTime - remainingTime);
                }
                actor::Velocity(a).z = velocityZ;
                if (remainingTime > 0.005f) {
                    physFalling(a, remainingTime, iterations);
                }
            }
            return false;
        }

        void physSpider(UObject* a, float deltaTime, int iterations)
        {
            Vector& velocity = actor::Velocity(a);
            Vector& acceleration = actor::Acceleration(a);
            Vector accelDir;
            if (acceleration.zero()) {
                Vector oldVelocity = velocity;
                velocity = velocity - velocity * (2 * deltaTime * zoneGroundFriction(a));
                if (oldVelocity.dot(velocity) < 0) {
                    velocity = acceleration;
                }
            } else {
                accelDir = acceleration.normal();
                float velocitySize = velocity.size();
                float accelRate = pawn::AccelRate(a);
                if (acceleration.sizeSquared() > accelRate * accelRate) {
                    acceleration = accelDir * accelRate;
                }
                velocity = velocity - (velocity - accelDir * velocitySize) * (deltaTime * zoneGroundFriction(a));
            }
            velocity = velocity + acceleration * deltaTime;
            float maxSpeed = pawn::GroundSpeed(a) * pawn::DesiredSpeed(a);
            iterations++;
            if (velocity.sizeSquared() > maxSpeed * maxSpeed) {
                velocity = velocity.normal() * maxSpeed;
            }
            Vector zoneVel = zoneVelocity(a).sizeSquared() > 90000.0f ? zoneVelocity(a) : Vector {};
            Vector desiredMove = velocity + zoneVel;
            float moveSize = desiredMove.size();
            Vector desiredDir = desiredMove / moveSize;
            bool needFloor = nearlyZero(pawn::Floor(a));
            CheckResult hit;
            Vector gravDir = pawn::Floor(a) * -1.0f;
            Vector down = gravDir * (pawn::MaxStepHeight(a) + 4.0f);
            actor::DesiredRotation(a) = actor::Rotation(a);
            if (!needFloor) {
                Vector location = actor::Location(a);
                world().lineCheck(hit, location, location + down, cylinderExtent(a), nullptr, TRACE_World);
                needFloor = hit.time == 1.0f;
            }
            if (needFloor) {
                if (!findNewFloor(a, actor::Location(a), deltaTime, deltaTime, iterations)) {
                    return;
                }
                gravDir = pawn::Floor(a) * -1.0f;
                down = gravDir * (pawn::MaxStepHeight(a) + 4.0f);
            }
            Rotator desiredRotation = vectorRotation(pawn::Floor(a));
            desiredRotation.pitch -= 16384;
            desiredRotation.roll = 0;
            actor::DesiredRotation(a) = desiredRotation;
            float dotp = accelDir.dot(pawn::Floor(a));
            Vector realDir = desiredDir;
            if (pawn::Floor(a).z < 0.6f && dotp > 0.9f) {
                pawn::Floor(a) = {};
                vm::event(a, Name("Falling"));
                setPhysics(a, PHYS_Falling, nullptr);
                physFalling(a, deltaTime, iterations);
                return;
            }
            desiredDir = (desiredDir - pawn::Floor(a) * desiredDir.dot(pawn::Floor(a))).normal();
            actor::OldLocation(a) = actor::Location(a);
            actor::bJustTeleported(a) = 0;
            float remaining = deltaTime;
            desiredMove = desiredDir * moveSize;
            while (remaining > 0 && iterations < 8) {
                iterations++;
                float tick = remaining > 0.05f ? std::min(0.05f, remaining * 0.5f) : remaining;
                remaining -= tick;
                Vector delta = desiredMove * tick;
                if (!nearlyZero(delta)) {
                    world().moveActor(a, delta, actor::DesiredRotation(a), hit, false, false, false, false);
                    if (a->destroyed) {
                        return;
                    }
                    if (hit.time < 1.0f) {
                        if (hit.normal.z >= 0) {
                            if (hit.normal.dot(realDir) < 0 && pawn::Floor(a).dot(realDir) < 0) {
                                vm::Call(a, Name("HitWall")).v(hit.normal).obj(hit.actor).run();
                            } else {
                                Vector combo = (hit.normal + pawn::Floor(a)).normal();
                                if (realDir.dot(combo) > 0.9f) {
                                    vm::Call(a, Name("HitWall")).v(hit.normal).obj(hit.actor).run();
                                }
                            }
                            pawn::Floor(a) = hit.normal;
                            gravDir = pawn::Floor(a) * -1.0f;
                            down = gravDir * (pawn::MaxStepHeight(a) + 4.0f);
                        } else if (hit.normal.dot(realDir) < 0) {
                            vm::Call(a, Name("HitWall")).v(hit.normal).obj(hit.actor).run();
                        } else if (pawn::Floor(a).dot(realDir) > 0.7f) {
                            vm::event(a, Name("Falling"));
                            if (actor::Physics(a) == PHYS_Spider) {
                                setPhysics(a, PHYS_Falling, nullptr);
                            }
                            if (actor::Physics(a) == PHYS_Falling) {
                                float velocityZ = actor::Velocity(a).z;
                                if (!actor::bJustTeleported(a) && deltaTime > remaining) {
                                    actor::Velocity(a) = (actor::Location(a) - actor::OldLocation(a)) / (deltaTime - remaining);
                                }
                                actor::Velocity(a).z = velocityZ;
                                if (remaining > 0.005f) {
                                    physFalling(a, remaining, iterations);
                                }
                                return;
                            }
                        }
                        stepUp(a, gravDir, delta.normal(), delta * (1.0f - hit.time), hit);
                        if (actor::Physics(a) == PHYS_Falling) {
                            if (remaining > 0.005f) {
                                physFalling(a, remaining, iterations);
                            }
                            return;
                        }
                    }
                }
                move(a, down, hit);
                if (a->destroyed) {
                    return;
                }
                if (hit.time == 1.0f) {
                    if (!findNewFloor(a, actor::OldLocation(a), deltaTime, remaining, iterations)) {
                        return;
                    }
                    gravDir = pawn::Floor(a) * -1.0f;
                    down = gravDir * (pawn::MaxStepHeight(a) + 4.0f);
                } else {
                    pawn::Floor(a) = hit.normal;
                    if (hit.actor != actor::Base(a) && !isPawn(hit.actor)) {
                        setSupportBase(a, hit.actor);
                    }
                }
            }
            if (!actor::bJustTeleported(a)) {
                actor::Velocity(a) = (actor::Location(a) - actor::OldLocation(a)) / deltaTime;
            }
        }

        void physTrailer(UObject* a)
        {
            UObject* owner = actor::Owner(a);
            if (!owner) {
                return;
            }
            if (actor::DrawType(a) == DT_Sprite) {
                Vector location = actor::Location(owner);
                if (actor::bTrailerPrePivot(a)) {
                    location = location + actor::PrePivot(a);
                } else if (actor::bTrailerSameRotation(a)) {
                    location = location - rotationAxes(actor::Rotation(owner)).xAxis * actor::Mass(a);
                }
                world().farMoveActor(a, location, false, true);
                return;
            }
            world().farMoveActor(a, actor::Location(owner), false, true);
            Rotator rotation { 16384, 0, 0 };
            Vector velocity = actor::Velocity(owner);
            if (actor::bTrailerSameRotation(a)) {
                rotation = actor::Rotation(owner);
            } else if (!nearlyZero(velocity)) {
                rotation = vectorRotation(velocity * -1.0f);
            }
            CheckResult hit;
            world().moveActor(a, {}, rotation, hit, false, false, false, false);
        }

        void physRolling(UObject* a, float deltaTime, int iterations)
        {
            Vector& velocity = actor::Velocity(a);
            Vector velDir = velocity.normal();
            Vector accelDir = actor::Acceleration(a).normal();
            velocity = velocity - (velDir - accelDir) * (velocity.size() * deltaTime * zoneGroundFriction(a));
            velocity = velocity * (1 - zoneFluidFriction(a) * deltaTime) + actor::Acceleration(a) * deltaTime;
            Vector desiredMove = velocity + zoneVelocity(a);
            actor::OldLocation(a) = actor::Location(a);
            actor::bJustTeleported(a) = 0;
            float remaining = deltaTime;
            Vector gravDir { 0, 0, zoneGravity(a).z > 0 ? 1.0f : -1.0f };
            Vector down = gravDir * 16.0f;
            CheckResult hit;
            int bounces = 0;
            while (remaining > 0 && iterations < 8) {
                iterations++;
                float tick = remaining > 0.1f ? std::min(0.1f, remaining * 0.5f) : remaining;
                remaining -= tick;
                Vector delta = desiredMove * tick;
                Vector subMove = delta;
                Vector subLocation = actor::Location(a);
                if (!(std::abs(delta.x) < 1e-4f && std::abs(delta.y) < 1e-4f && std::abs(delta.z) < 1e-4f)) {
                    move(a, delta, hit);
                    if (hit.time < 1.0f) {
                        vm::Call(a, Name("HitWall")).v(hit.normal).obj(hit.actor).run();
                        if (actor::bBounce(a)) {
                            if (bounces < 2) {
                                remaining += tick * (1.0f - hit.time);
                            }
                            bounces++;
                        } else {
                            Vector original = delta;
                            Vector oldNormal = hit.normal;
                            delta = (delta - hit.normal * delta.dot(hit.normal)) * (1.0f - hit.time);
                            if (delta.dot(original) >= 0) {
                                move(a, delta, hit);
                                if (hit.time < 1.0f) {
                                    vm::Call(a, Name("HitWall")).v(hit.normal).obj(hit.actor).run();
                                    twoWallAdjust(desiredMove.normal(), delta, hit.normal, oldNormal, hit.time);
                                    move(a, delta, hit);
                                }
                            }
                        }
                    }
                }
                if (a->destroyed) {
                    return;
                }
                move(a, down, hit);
                float dropTime = hit.time;
                float dropHitZ = hit.normal.z;
                if (dropTime < 1.0f && hit.normal.z < 1.0f && hit.normal.z * zoneGroundFriction(a) < 3.3f) {
                    Vector slide = zoneGravity(a) * (deltaTime / (2 * std::max(0.5f, zoneGroundFriction(a))) * deltaTime);
                    delta = slide - hit.normal * slide.dot(hit.normal);
                    if (delta.dot(slide) >= 0) {
                        move(a, delta, hit);
                        dropHitZ = std::max(dropHitZ, hit.normal.z);
                    }
                }
                if (dropTime == 1.0f || dropHitZ < 0.7f) {
                    move(a, down * (-dropTime), hit);
                    float desiredDist = subMove.size();
                    float actualDist = (actor::Location(a) - subLocation).size2D();
                    remaining += tick * (1 - std::min(1.0f, actualDist / desiredDist));
                    vm::event(a, Name("Falling"));
                    if (actor::Physics(a) == PHYS_Rolling) {
                        setPhysics(a, PHYS_Falling, nullptr);
                    }
                    if (actor::Physics(a) == PHYS_Falling) {
                        if (!actor::bJustTeleported(a) && deltaTime > remaining) {
                            actor::Velocity(a) = (actor::Location(a) - actor::OldLocation(a)) / (deltaTime - remaining);
                        }
                        actor::Velocity(a).z = 0;
                        if (remaining > 0.005f) {
                            physFalling(a, remaining, iterations);
                        }
                        return;
                    }
                    move(a, desiredMove * remaining, hit);
                } else if (hit.actor != actor::Base(a)) {
                    setSupportBase(a, hit.actor);
                }
            }
            if (!actor::bJustTeleported(a)) {
                actor::Velocity(a) = (actor::Location(a) - actor::OldLocation(a)) / deltaTime;
            }
        }
    }

    namespace {
        void physSliding(UObject* a, float deltaTime, int iterations)
        {
            actor::Acceleration(a) = actor::Acceleration(a) * 0.9f;
            if (isPawn(a)) {
                physWalking(a, deltaTime, iterations);
            }
        }

        void postTouch(UObject* a)
        {
            UObject* pending = actor::PendingTouch(a);
            if (!pending) {
                return;
            }
            vm::Call(pending, Name("PostTouch")).obj(a).run();
            pending = actor::PendingTouch(a);
            actor::PendingTouch(a) = actor::PendingTouch(pending);
            actor::PendingTouch(pending) = nullptr;
        }
    }

    void processHitWall(UObject* a, const Vector& hitNormal, UObject* hitActor)
    {
        if (hitActor && isPawn(hitActor)) {
            return;
        }
        Vector normal = hitNormal;
        if (isPawn(a)) {
            if (actor::Acceleration(a).zero()) {
                return;
            }
            Vector direction = safeNormal(pawn::Destination(a) - actor::Location(a));
            if (actor::Physics(a) == PHYS_Walking) {
                normal.z = 0;
                direction.z = 0;
            }
            if (pawn::MinHitWall(a) < direction.dot(normal)) {
                return;
            }
            if (!vm::isProbing(a, Name("HitWall")) && actor::Physics(a) != PHYS_Falling) {
                pawn::MoveTimer(a) = -1.0f;
                pawn::bFromWall(a) = 1;
                return;
            }
        } else if (!vm::isProbing(a, Name("HitWall"))) {
            return;
        }
        vm::Call(a, Name("HitWall")).v(normal).obj(hitActor).run();
    }

    void processLanded(UObject* a, const Vector& hitNormal, UObject* hitActor, float remainingTime, int iterations)
    {
        UObject* zone = zoneOf(a);
        if (!isPawn(a) && zone && zoneinfo::bBounceVelocity(zone) && !zoneinfo::ZoneVelocity(zone).zero()) {
            actor::Velocity(a) = zoneinfo::ZoneVelocity(zone) + Vector { 0, 0, 80 };
            return;
        }
        if (isPawn(a)) {
            CheckResult hit;
            Vector location = actor::Location(a);
            world().lineCheck(hit, location, location - Vector { 0, 0, 0.2f * actor::CollisionRadius(a) + 8 }, cylinderExtent(a) * 0.9f, a, TRACE_Pawns | TRACE_World | TRACE_Others | TRACE_OnlyProjActor);
            if (hit.time >= 1.0f) {
                Vector adjusted = location;
                if (world().findSpot(cylinderExtent(a) * 1.1f, adjusted, false, false) && !(adjusted == location)) {
                    world().farMoveActor(a, adjusted, false, false);
                    actor::Velocity(a).x += random::frand() * 60 - 30;
                    actor::Velocity(a).y += random::frand() * 60 - 30;
                    return;
                }
            }
        } else if (a->isA(engineClasses().decoration)) {
            if (decoration::numLandings(a) < 5) {
                float radius = actor::CollisionRadius(a);
                float height = actor::CollisionHeight(a);
                Vector location = actor::Location(a);
                CheckResult hit;
                world().lineCheck(hit, location - Vector { 0, 0, 0.8f * height }, location - Vector { 0, 0, height + radius + 8 }, {}, a, TRACE_Pawns | TRACE_World | TRACE_Others | TRACE_OnlyProjActor);
                if (!hit.actor) {
                    Vector partExtent = cylinderExtent(a) * 0.5f;
                    partExtent.z *= 2;
                    auto unsupported = [&](float x, float y) {
                        Vector top = location + Vector { x * 0.5f * radius, y * 0.5f * radius, 0 };
                        return world().lineCheck(hit, top, top - Vector { 0, 0, 8 }, partExtent, a, TRACE_Pawns | TRACE_World | TRACE_Others) ? 0 : 1;
                    };
                    int quad1 = unsupported(1, 1);
                    int quad2 = unsupported(-1, 1);
                    int quad3 = unsupported(-1, -1);
                    int quad4 = unsupported(1, -1);
                    if (quad1 + quad2 + quad3 + quad4 > 1 && quad1 + quad3 != 0 && quad2 + quad4 != 0) {
                        decoration::numLandings(a)++;
                        float push = std::clamp(-actor::Velocity(a).z, 30.0f, 30.0f + radius) * 2.0f;
                        actor::Velocity(a) = Vector { static_cast<float>(quad1 + quad4 - quad2 - quad3), static_cast<float>(quad1 + quad2 - quad3 - quad4), 0.5f } * push;
                        return;
                    }
                }
                if (a->isA(Name("Carcass")) && hitNormal.z < 0.9f && carcass::bSlidingCarcass(a)) {
                    if (random::frand() < 0.2f) {
                        decoration::numLandings(a)++;
                    }
                    actor::Velocity(a) = hitNormal * 120.0f;
                    actor::Velocity(a).z = 70;
                    return;
                }
            }
            decoration::numLandings(a) = 0;
        }
        vm::Call(a, Name("Landed")).v(hitNormal).run();
        if (a->destroyed) {
            return;
        }
        if (actor::Physics(a) == PHYS_Falling) {
            if (isPawn(a)) {
                setPhysics(a, PHYS_Walking, hitActor);
            } else {
                setPhysics(a, PHYS_None, hitActor);
                actor::Velocity(a) = {};
            }
        }
        if (actor::Physics(a) == PHYS_Walking && isPawn(a)) {
            actor::Acceleration(a) = actor::Acceleration(a).normal();
            if (remainingTime > 0.01f) {
                physWalking(a, remainingTime, iterations);
            }
        }
    }

    void performPhysics(UObject* a, float deltaSeconds)
    {
        uint8_t physics = actor::Physics(a);
        if (isPawn(a)) {
            Vector oldVelocity = actor::Velocity(a);
            switch (physics) {
            case PHYS_Walking:
                physWalking(a, deltaSeconds, 0);
                break;
            case PHYS_Falling:
                physFalling(a, deltaSeconds, 0);
                break;
            case PHYS_Flying:
                physFlying(a, deltaSeconds, 0);
                break;
            case PHYS_Swimming:
                physSwimming(a, deltaSeconds, 0);
                break;
            case PHYS_Spider:
                physSpider(a, deltaSeconds, 0);
                break;
            case PHYS_Sliding:
                physSliding(a, deltaSeconds, 0);
                break;
            case PHYS_Interpolating:
                actor::OldLocation(a) = actor::Location(a);
                physPathing(a, deltaSeconds);
                actor::Velocity(a) = (actor::Location(a) - actor::OldLocation(a)) / deltaSeconds;
                break;
            default:
                break;
            }
            if (a->destroyed) {
                return;
            }
            Rotator rotation = actor::Rotation(a);
            if (actor::Physics(a) != PHYS_Spider && (isPlayerPawn(a) || !(rotation == actor::DesiredRotation(a)) || actor::RotationRate(a).roll > 0)) {
                pawnRotation(a, deltaSeconds, oldVelocity);
            }
            if (pawn::bAlignToFloor(a)) {
                Vector side = rotationAxes(actor::Rotation(a)).yAxis.cross(pawn::Floor(a));
                Rotator aligned { vectorRotation(side).pitch, actor::Rotation(a).yaw, vectorRotation(side.cross(pawn::Floor(a))).pitch };
                if (!(aligned == actor::Rotation(a))) {
                    CheckResult hit;
                    world().moveActor(a, {}, aligned, hit, false, false, false, false);
                }
            }
            pawn::MoveTimer(a) -= deltaSeconds;
            pawn::AvgPhysicsTime(a) = static_cast<float>(static_cast<double>(pawn::AvgPhysicsTime(a)) * 0.8 + static_cast<double>(deltaSeconds) * 0.2);
            float& prePivot = actor::PrePivot(a).z;
            if (prePivot != 0) {
                float rate = std::max(200.0f, static_cast<float>(static_cast<double>(std::abs(prePivot)) * 10.0));
                if (prePivot > 0) {
                    prePivot -= rate * deltaSeconds;
                    if (prePivot < 0) {
                        prePivot = 0;
                    }
                } else {
                    prePivot += rate * deltaSeconds;
                    if (prePivot > 0) {
                        prePivot = 0;
                    }
                }
            }
            postTouch(a);
            return;
        }
        switch (physics) {
        case PHYS_Projectile:
            physProjectile(a, deltaSeconds, 0);
            break;
        case PHYS_Falling:
            physFalling(a, deltaSeconds, 0);
            break;
        case PHYS_Interpolating:
            actor::OldLocation(a) = actor::Location(a);
            physPathing(a, deltaSeconds);
            actor::Velocity(a) = (actor::Location(a) - actor::OldLocation(a)) / deltaSeconds;
            break;
        case PHYS_MovingBrush:
            actor::OldLocation(a) = actor::Location(a);
            physMovingBrush(a, deltaSeconds);
            actor::Velocity(a) = (actor::Location(a) - actor::OldLocation(a)) / deltaSeconds;
            break;
        case PHYS_Trailer:
            physTrailer(a);
            break;
        case PHYS_Rolling:
            physRolling(a, deltaSeconds, 0);
            break;
        case PHYS_Sliding:
            physSliding(a, deltaSeconds, 0);
            break;
        default:
            break;
        }
        if (a->destroyed) {
            return;
        }
        Rotator rate = actor::RotationRate(a);
        if ((rate.pitch & 65535) != 0 || (rate.yaw & 65535) != 0 || (rate.roll & 65535) != 0) {
            actorRotation(a, deltaSeconds);
        }
        postTouch(a);
    }
}
