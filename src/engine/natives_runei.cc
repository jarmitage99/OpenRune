#include "core/random.h"
#include "core/vm.h"
#include "engine/actor.h"
#include "engine/world.h"

#include <cmath>

namespace rune {
    namespace {
        float& floatProperty(UObject* object, const Name& name)
        {
            return *reinterpret_cast<float*>(object->propertyAddress(name));
        }

        bool boolProperty(UObject* object, const Name& name)
        {
            return *reinterpret_cast<uint32_t*>(object->propertyAddress(name)) != 0;
        }

        Vector& vectorProperty(UObject* object, const Name& name)
        {
            return *reinterpret_cast<Vector*>(object->propertyAddress(name));
        }

        UObject* objectProperty(UObject* object, const Name& name, int32_t index = 0)
        {
            return *reinterpret_cast<UObject**>(object->propertyAddress(name, index));
        }

        Vector safeNormal(const Vector& v)
        {
            float squared = v.sizeSquared();
            if (squared < 1e-8f) {
                return {};
            }
            return v * (1.0f / std::sqrt(squared));
        }

        void nativeConfinementTick(UObject* self, Frame& stack, void*)
        {
            vm::get<float>(stack);
            vm::finish(stack);
            static const Name nameConfiner("Confiner");
            Vector sum;
            int32_t count = 0;
            for (int32_t i = 0; i < 4; i++) {
                UObject* confiner = objectProperty(self, nameConfiner, i);
                if (confiner) {
                    sum += actor::Location(confiner);
                    count++;
                }
            }
            if (!World::current || count == 0) {
                return;
            }
            CheckResult hit;
            World::current->moveActor(self, sum / static_cast<float>(count) - actor::Location(self), actor::Rotation(self), hit, false, false, false, false);
        }

        void nativeRopePointTick(UObject*, Frame& stack, void*)
        {
            vm::get<float>(stack);
            vm::finish(stack);
        }

        void nativeSpringPointTick(UObject* self, Frame& stack, void*)
        {
            float deltaSeconds = vm::get<float>(stack);
            vm::finish(stack);
            static const Name nameOriginalPos("OriginalPos");
            static const Name nameSpringConstant("SpringConstant");
            static const Name nameDampFactor("DampFactor");
            Vector& velocity = actor::Velocity(self);
            float spring = floatProperty(self, nameSpringConstant) * deltaSeconds / actor::Mass(self);
            velocity += (vectorProperty(self, nameOriginalPos) - actor::Location(self)) * spring;
            float damping = deltaSeconds * floatProperty(self, nameDampFactor);
            if (damping >= 1.0f) {
                velocity = {};
            } else {
                velocity *= 1.0f - damping;
            }
        }

        void nativeTreePointTick(UObject* self, Frame& stack, void*)
        {
            float deltaSeconds = vm::get<float>(stack);
            vm::finish(stack);
            static const Name nameParent("Parent");
            static const Name nameApplyAcceleration("bApplyAcceleration");
            static const Name nameGravDir("GravDir");
            static const Name nameGravAccel("GravAccel");
            static const Name nameApplySway("bApplySway");
            static const Name nameSwayDir("SwayDir");
            static const Name nameSwayMagnitude("SwayMagnitude");
            static const Name nameSpring("bSpring");
            static const Name nameSpringConstant("SpringConstant");
            static const Name nameBranchLength("BranchLength");
            static const Name nameRestrictVelocity("bRestrictVelocity");
            static const Name nameMaxVelocityAllowed("MaxVelocityAllowed");
            static const Name nameDampFactor("DampFactor");
            static const Name nameStiffRotation("bStiffRotation");
            static const Name nameAffectsParent("bAffectsParent");
            if (!World::current) {
                return;
            }
            for (UObject* point = self; point; point = objectProperty(point, nameParent)) {
                UObject* parent = objectProperty(point, nameParent);
                if (!parent) {
                    break;
                }
                Vector& velocity = actor::Velocity(point);
                if (boolProperty(point, nameApplyAcceleration)) {
                    velocity += vectorProperty(point, nameGravDir) * (floatProperty(point, nameGravAccel) * deltaSeconds);
                }
                if (boolProperty(point, nameApplySway) && random::frand() < 0.1f) {
                    float amount = random::frand();
                    velocity += safeNormal(vectorProperty(point, nameSwayDir)) * amount * floatProperty(self, nameSwayMagnitude) * deltaSeconds;
                }
                Vector predicted = actor::Location(point) + velocity * deltaSeconds;
                Vector direction = safeNormal(actor::Location(parent) - predicted);
                Vector branch;
                Vector target;
                if (boolProperty(point, nameSpring)) {
                    branch = actor::Location(parent) - actor::Location(point);
                    float force = floatProperty(point, nameSpringConstant) * (branch.size() - floatProperty(point, nameBranchLength)) / actor::Mass(point);
                    target = predicted + safeNormal(branch) * force * (deltaSeconds * deltaSeconds);
                } else {
                    branch = direction * floatProperty(point, nameBranchLength);
                    target = actor::Location(parent) - branch;
                }
                Vector delta = target - actor::Location(point);
                float maxVelocity = floatProperty(point, nameMaxVelocityAllowed);
                if (boolProperty(point, nameRestrictVelocity) && delta.size() > maxVelocity) {
                    delta = safeNormal(delta) * maxVelocity;
                }
                velocity = delta * ((1.0f - floatProperty(point, nameDampFactor)) / deltaSeconds);
                Rotator rotation = unrealRotation(boolProperty(self, nameStiffRotation) ? -branch : actor::Location(point) - actor::Location(parent));
                rotation.roll = rotation.yaw;
                CheckResult hit;
                World::current->moveActor(point, {}, rotation, hit, false, false, false, false);
                if (boolProperty(point, nameAffectsParent)) {
                    actor::Velocity(parent) = (actor::Velocity(parent) + velocity * 0.5f) * 0.5f;
                }
            }
        }

        void nativeMeshPointTick(UObject* self, Frame& stack, void*)
        {
            float deltaSeconds = vm::get<float>(stack);
            vm::finish(stack);
            static const Name nameAnchored("bAnchored");
            static const Name nameOriginalPos("OriginalPos");
            static const Name nameSpringConstant("SpringConstant");
            static const Name nameDampFactor("DampFactor");
            static const Name nameAdjacent("Adjacent");
            if (boolProperty(self, nameAnchored)) {
                return;
            }
            float spring = floatProperty(self, nameSpringConstant) * deltaSeconds / actor::Mass(self);
            Vector impulse = (vectorProperty(self, nameOriginalPos) - actor::Location(self)) * spring;
            Vector& velocity = actor::Velocity(self);
            velocity += impulse;
            velocity *= 1.0f - floatProperty(self, nameDampFactor);
            for (int32_t i = 0; i < 4; i++) {
                UObject* adjacent = objectProperty(self, nameAdjacent, i);
                if (adjacent && !boolProperty(adjacent, nameAnchored)) {
                    actor::Velocity(adjacent) -= impulse * 0.25f;
                }
            }
        }
    }

    void registerRuneINatives()
    {
        vm::registerNative(651, nativeConfinementTick);
        vm::registerNative(652, nativeRopePointTick);
        vm::registerNative(653, nativeSpringPointTick);
        vm::registerNative(654, nativeTreePointTick);
        vm::registerNative(655, nativeMeshPointTick);
    }
}
