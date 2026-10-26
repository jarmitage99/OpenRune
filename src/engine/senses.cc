#include "core/random.h"
#include "core/vm.h"
#include "engine/actor.h"
#include "engine/world.h"

#include <algorithm>
#include <cmath>

namespace rune {
    namespace {
        World& world()
        {
            return *World::current;
        }

    }

    bool lineOfSightTo(UObject* self, UObject* other, bool showSelf, bool ignoreDistance)
    {
        if (!other || other->destroyed || !World::current) {
            return false;
        }
        UObject* enemy = pawn::Enemy(self);
        if (other == enemy) {
            showSelf = false;
        } else if (showSelf) {
            pawn::bLOSflag(self) = pawn::bLOSflag(self) ? 0 : 1;
        }
        bool otherPawn = isPawn(other);
        bool isPlayer = pawn::bIsPlayer(self) != 0;
        Vector location = actor::Location(self);
        Vector otherLocation = actor::Location(other);
        float distSq = (otherLocation - location).sizeSquared();
        float maxDistSq = 0;
        if (showSelf) {
            if (otherPawn) {
                maxDistSq = pawn::SightRadius(self) * std::min(1.0f, static_cast<float>(pawn::Visibility(other)) * 0.0078125f);
                maxDistSq = std::min(isPlayer ? 16000000.0f : 12000000.0f, maxDistSq * maxDistSq);
            } else {
                maxDistSq = pawn::SightRadius(self) * pawn::SightRadius(self);
            }
            if (distSq > maxDistSq) {
                return false;
            }
            Vector sightDir = (otherLocation - location).normal();
            float stimulus = sightDir.dot(rotationVector(actor::Rotation(self))) - pawn::PeripheralVision(self);
            if (stimulus > 0) {
                stimulus = static_cast<float>(static_cast<double>(stimulus) * 0.8 + 0.2);
            } else {
                stimulus = static_cast<float>(static_cast<double>(stimulus) * 0.17 + 0.2);
            }
            pawn::Stimulus(self) = stimulus;
            if (stimulus <= 0) {
                return false;
            }
            float heightMod = std::abs(otherLocation.z - location.z) / std::max(1.0f, pawn::Skill(self) + 1.0f);
            distSq = (distSq + heightMod * heightMod) / (stimulus * stimulus);
            if (distSq > maxDistSq && !ignoreDistance) {
                return false;
            }
            pawn::Stimulus(self) = 1.0f;
        } else {
            if (isPlayer) {
                if (otherPawn) {
                    maxDistSq = 5000.0f * std::min(1.0f, static_cast<float>(pawn::Visibility(other) + 16) * 0.015f);
                    maxDistSq = std::min(25000000.0f, maxDistSq * maxDistSq);
                } else {
                    maxDistSq = 16000000.0f;
                }
            } else if (otherPawn) {
                maxDistSq = 4000.0f * std::min(1.0f, static_cast<float>(pawn::Visibility(other) + 16) * 0.015f);
                maxDistSq = std::min(16000000.0f, maxDistSq * maxDistSq);
            } else {
                maxDistSq = 9000000.0f;
            }
            if (distSq > maxDistSq && !ignoreDistance) {
                return false;
            }
        }
        World& w = world();
        Vector viewPoint = location + Vector { 0, 0, pawn::BaseEyeHeight(self) };
        if (other == enemy) {
            if (w.fastLineCheck(otherLocation, viewPoint) || w.fastLineCheck(otherLocation, location)) {
                pawn::LastSeeingPos(self) = location;
                pawn::LastSeenPos(self) = actor::Location(enemy);
                return true;
            }
            if (distSq > 1000000.0f && !ignoreDistance) {
                return false;
            }
        } else if (distSq > 1000000.0f && !ignoreDistance) {
            if (otherPawn) {
                if (!pawn::bLOSflag(self) && distSq > 0.5f * maxDistSq) {
                    return false;
                }
                if (!isPlayer && random::frand() < 0.5f) {
                    return false;
                }
            }
            return w.fastLineCheck(otherLocation, viewPoint);
        }
        if (!showSelf || !pawn::bLOSflag(self)) {
            Vector body = otherLocation;
            body.z = static_cast<float>(static_cast<double>(body.z) + static_cast<double>(actor::CollisionHeight(other)) * 0.8);
            if (w.fastLineCheck(body, viewPoint)) {
                return true;
            }
        }
        if ((distSq > 250000.0f && !ignoreDistance) || !otherPawn) {
            return false;
        }
        float r = actor::CollisionRadius(other);
        Vector points[4] = {
            { otherLocation.x - r, otherLocation.y + r, otherLocation.z },
            { otherLocation.x + r, otherLocation.y + r, otherLocation.z },
            { otherLocation.x - r, otherLocation.y - r, otherLocation.z },
            { otherLocation.x + r, otherLocation.y - r, otherLocation.z },
        };
        int imin = 0;
        int imax = 0;
        float currentMin = points[0].sizeSquared();
        float currentMax = currentMin;
        for (int i = 1; i < 4; i++) {
            float size = points[i].sizeSquared();
            if (size > currentMax) {
                currentMax = size;
                imax = i;
            } else if (size < currentMin) {
                currentMin = size;
                imin = i;
            }
        }
        bool skip = pawn::bLOSflag(self) != 0;
        for (int i = 0; i < 4; i++) {
            if (i == imin || i == imax) {
                continue;
            }
            if (skip && showSelf) {
                skip = false;
            } else {
                skip = true;
                if (w.fastLineCheck(points[i], viewPoint)) {
                    return true;
                }
            }
        }
        return false;
    }

    void World::tickPawnSenses(UObject* a, float deltaSeconds)
    {
        float& counter = pawn::SightCounter(a);
        if (counter < 0) {
            counter += 0.2f;
        }
        counter -= deltaSeconds;
        if (pawn::bIsPlayer(a) && !actor::bHidden(a) && levelInfo) {
            for (UObject* other = levelinfo::PawnList(levelInfo); other; other = pawn::nextPawn(other)) {
                float otherCounter = pawn::SightCounter(other);
                if (other == a || other->destroyed || (otherCounter >= 0 && otherCounter != 0.1f) || !vm::isProbing(other, Name("SeePlayer"))) {
                    continue;
                }
                if (lineOfSightTo(other, a, true, false)) {
                    vm::Call(other, Name("SeePlayer")).obj(a).run();
                }
            }
        }
        if (counter < 0 && vm::findEvent(a, Name("EnemyNotVisible"))) {
            UObject* enemy = pawn::Enemy(a);
            if (enemy) {
                if (lineOfSightTo(a, enemy, false, false)) {
                    pawn::LastSeenTime(a) = timeSeconds;
                } else {
                    vm::event(a, Name("EnemyNotVisible"));
                }
            }
            pawn::SightCounter(a) = 0.1f;
        }
    }
}
