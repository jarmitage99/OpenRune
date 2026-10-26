#include "core/objects.h"
#include "core/vm.h"
#include "engine/actor.h"
#include "engine/skeletal.h"
#include "engine/world.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace rune {
    namespace {
        USkelModel* modelOf(UObject* self)
        {
            return skeletal::animationModel(self);
        }

        bool validJoint(UObject* self, int32_t joint)
        {
            USkelModel* model = modelOf(self);
            return model && joint >= 0 && static_cast<size_t>(joint) < model->joints.size() && joint < SkelMaxJoints;
        }

        void nativeGetJointPos(UObject* self, Frame& stack, void* result)
        {
            int32_t joint = vm::get<int32_t>(stack);
            vm::finish(stack);
            const SkelPose* pose = skeletal::pose(self);
            bool valid = pose && joint >= 0 && static_cast<size_t>(joint) < pose->joints.size();
            vm::ret<Vector>(result, valid ? pose->joints[static_cast<size_t>(joint)].origin : actor::Location(self));
        }

        void nativeGetJointRot(UObject* self, Frame& stack, void* result)
        {
            int32_t joint = vm::get<int32_t>(stack);
            vm::finish(stack);
            const SkelPose* pose = skeletal::pose(self);
            bool valid = pose && joint >= 0 && static_cast<size_t>(joint) < pose->joints.size();
            vm::ret<Rotator>(result, valid ? skeletal::coordsRotation(pose->joints[static_cast<size_t>(joint)]) : actor::Rotation(self));
        }

        void nativeGetJointName(UObject* self, Frame& stack, void* result)
        {
            int32_t joint = vm::get<int32_t>(stack);
            vm::finish(stack);
            USkelModel* model = modelOf(self);
            bool valid = model && joint >= 0 && static_cast<size_t>(joint) < model->joints.size();
            vm::ret<std::string>(result, valid ? model->joints[static_cast<size_t>(joint)].name.str() : std::string());
        }

        void nativeApplyJointForce(UObject* self, Frame& stack, void*)
        {
            int32_t joint = vm::get<int32_t>(stack);
            Vector force = vm::get<Vector>(stack);
            vm::finish(stack);
            skeletal::applyJointForce(self, joint, force);
        }

        void nativeNumJoints(UObject* self, Frame& stack, void* result)
        {
            vm::finish(stack);
            USkelModel* model = modelOf(self);
            vm::ret<int32_t>(result, model ? static_cast<int32_t>(model->joints.size()) : 0);
        }

        void nativeSetDefaultPolygroups(UObject* self, Frame& stack, void*)
        {
            vm::finish(stack);
            USkelModel* model = skeletal::geometryModel(self);
            if (!model || model->meshes.empty()) {
                return;
            }
            const SkelMesh& mesh = model->meshes[std::min<size_t>(actor::SkelMesh(self), model->meshes.size() - 1)];
            for (int group = 0; group < SkelPolyGroups; group++) {
                if (!actor::SkelGroupSkins(self, group)) {
                    Name skin = mesh.groupSkins[static_cast<size_t>(group)];
                    actor::SkelGroupSkins(self, group) = skin.none() ? nullptr : objects::loadObject(model->outermost()->name.str() + "." + skin.str(), objects::findClass("Engine.Texture"));
                }
                if (actor::SkelGroupFlags(self, group) == 0) {
                    actor::SkelGroupFlags(self, group) = mesh.groupFlags[static_cast<size_t>(group)];
                }
            }
        }

        void nativeSetDefaultJointFlags(UObject* self, Frame& stack, void*)
        {
            vm::finish(stack);
            skeletal::setDefaultJointFlags(self);
        }

        void nativeAttachActorToJoint(UObject* self, Frame& stack, void*)
        {
            UObject* other = vm::get<UObject*>(stack);
            int32_t joint = vm::get<int32_t>(stack);
            vm::finish(stack);
            if (!other || !validJoint(self, joint)) {
                return;
            }
            actor::JointChild(self, joint) = other;
            actor::bCarriedItem(other) = 1;
            actor::AttachParent(other) = self;
            actor::AttachParentJoint(other) = static_cast<uint8_t>(joint);
        }

        void nativeDetachActorFromJoint(UObject* self, Frame& stack, void* result)
        {
            int32_t joint = vm::get<int32_t>(stack);
            vm::finish(stack);
            UObject* other = validJoint(self, joint) ? actor::JointChild(self, joint) : nullptr;
            if (other) {
                actor::JointChild(self, joint) = nullptr;
                actor::bCarriedItem(other) = 0;
                actor::AttachParent(other) = nullptr;
                actor::AttachParentJoint(other) = 0;
                const SkelPose* pose = skeletal::pose(self);
                if (pose && World::current && !other->destroyed) {
                    const Coords& coords = pose->joints[static_cast<size_t>(joint)];
                    Rotator current = skeletal::coordsRotation(coords);
                    Rotator rotation = actor::Rotation(other);
                    CheckResult hit;
                    World::current->moveActor(other, coords.origin - actor::Location(other), { current.pitch - rotation.pitch, current.yaw - rotation.yaw, current.roll - rotation.roll }, hit, false, false, false, false);
                }
            }
            vm::ret<UObject*>(result, other);
        }

        void nativeTurnJointTo(UObject* self, Frame& stack, void*)
        {
            int32_t joint = vm::get<int32_t>(stack);
            Rotator rotation = vm::get<Rotator>(stack);
            vm::finish(stack);
            const Rotator& base = actor::Rotation(self);
            skeletal::setJointDelta(self, joint, Rotator { rotation.yaw - base.yaw, rotation.roll - base.roll, rotation.pitch - base.pitch });
        }

        void nativeActorAttachedTo(UObject* self, Frame& stack, void* result)
        {
            int32_t joint = vm::get<int32_t>(stack);
            vm::finish(stack);
            vm::ret<UObject*>(result, validJoint(self, joint) ? actor::JointChild(self, joint) : nullptr);
        }

        void nativeClosestJointTo(UObject* self, Frame& stack, void* result)
        {
            Vector point = vm::get<Vector>(stack);
            vm::finish(stack);
            const SkelPose* pose = skeletal::pose(self);
            int32_t best = 0;
            if (pose && !pose->joints.empty()) {
                float bestDistance = (pose->joints[0].origin - point).sizeSquared();
                for (size_t j = 0; j < pose->joints.size() && j < static_cast<size_t>(SkelMaxJoints); j++) {
                    if (!(actor::JointFlags(self, static_cast<int>(j)) & JointFlagCollision)) {
                        continue;
                    }
                    float distance = (pose->joints[j].origin - point).sizeSquared();
                    if (distance < bestDistance) {
                        bestDistance = distance;
                        best = static_cast<int32_t>(j);
                    }
                }
            }
            vm::ret<int32_t>(result, best);
        }

        void nativeJointNamed(UObject* self, Frame& stack, void* result)
        {
            Name name = vm::get<Name>(stack);
            vm::finish(stack);
            USkelModel* model = modelOf(self);
            vm::ret<int32_t>(result, model ? model->jointNamed(name) : 0);
        }

        void nativeResetAnimationCache(UObject*, Frame& stack, void*)
        {
            vm::get<Name>(stack);
            vm::finish(stack);
        }

        void nativeSetJointRot(UObject* self, Frame& stack, void*)
        {
            int32_t joint = vm::get<int32_t>(stack);
            Rotator rotation = vm::get<Rotator>(stack);
            vm::finish(stack);
            skeletal::setJointDelta(self, joint, Rotator { -rotation.yaw, -rotation.roll, -rotation.pitch });
        }
    }

    namespace {
        void nativeFrameSweep(UObject* self, Frame& stack, void*)
        {
            int32_t frame = vm::get<int32_t>(stack);
            Vector weaponVector = vm::get<Vector>(stack);
            Vector* lastStart = vm::getRef<Vector>(stack);
            Vector* lastEnd = vm::getRef<Vector>(stack);
            vm::finish(stack);
            UObject* parent = actor::AttachParent(self);
            if (!parent || !parent->data || parent->destroyed || !actor::Skeletal(parent)) {
                return;
            }
            UObject* proxy = actor::AnimProxy(parent);
            Name sequence = proxy && proxy->data && !proxy->destroyed ? actor::AnimSequence(proxy) : actor::AnimSequence(parent);
            SkelPose pose;
            size_t joint = actor::AttachParentJoint(self);
            if (!skeletal::framePose(parent, sequence, frame, pose) || joint >= pose.joints.size()) {
                return;
            }
            Vector oldStart = *lastStart;
            Vector oldEnd = *lastEnd;
            Vector newStart = pose.joints[joint].origin;
            Vector newEnd = skeletal::transformPoint(pose.joints[joint], weaponVector);
            vm::Call(self, Name("FrameSwept")).v(oldStart).v(oldEnd).v(newStart).v(newEnd).run();
            *lastStart = newStart;
            *lastEnd = newEnd;
        }

        void nativeSweepActors(UObject*, Frame& stack, void*)
        {
            vm::get<UObject*>(stack);
            UObject** out = vm::getRef<UObject*>(stack);
            Vector start1 = vm::get<Vector>(stack);
            Vector stop1 = vm::get<Vector>(stack);
            Vector start2 = vm::get<Vector>(stack);
            Vector stop2 = vm::get<Vector>(stack);
            float radius = vm::get<float>(stack);
            Vector* hitLocation = vm::getRef<Vector>(stack);
            Vector* hitNormal = vm::getRef<Vector>(stack);
            int32_t* lowMask = vm::getRef<int32_t>(stack);
            int32_t* highMask = vm::getRef<int32_t>(stack);
            vm::finish(stack);
            Vector startDelta = start2 - start1;
            Vector stopDelta = stop2 - stop1;
            int32_t steps = 0;
            if (radius > 0) {
                float longest = std::max(startDelta.size(), stopDelta.size());
                steps = std::min(static_cast<int32_t>(longest / radius + 1.0f), 100);
            }
            Vector extent { radius, radius, radius };
            uint32_t flags = TRACE_AllColliding | TRACE_HitJoint;
            std::vector<CheckResult> hits;
            if (World::current && steps > 0) {
                hits = World::current->multiLineCheck(start1, stop1, extent, nullptr, flags);
            }
            size_t index = 0;
            int32_t step = 0;
            vm::iterate(stack, [&]() {
                while (World::current && steps > 0 && step <= steps) {
                    if (index < hits.size()) {
                        const CheckResult& hit = hits[index++];
                        if (!hit.actor || hit.actor->destroyed) {
                            continue;
                        }
                        *out = hit.actor;
                        *hitLocation = hit.location;
                        *hitNormal = hit.normal;
                        bool jointHit = cast<USkelModel>(hit.material) != nullptr;
                        bool levelHit = hit.actor == World::current->levelInfo;
                        *lowMask = jointHit || levelHit ? hit.item : 0;
                        *highMask = jointHit ? hit.item2 : (levelHit ? -1 : 0);
                        return true;
                    }
                    step++;
                    if (step > steps) {
                        break;
                    }
                    float t = static_cast<float>(step) / static_cast<float>(steps);
                    hits = World::current->multiLineCheck(start1 + startDelta * t, stop1 + stopDelta * t, extent, nullptr, flags);
                    index = 0;
                }
                *out = nullptr;
                return false;
            });
        }
    }

    void registerSkeletalNatives()
    {
        vm::registerNative(622, nativeFrameSweep);
        vm::registerNative(313, nativeSweepActors);
        vm::registerNative(602, nativeGetJointPos);
        vm::registerNative(603, nativeGetJointRot);
        vm::registerNative(605, nativeGetJointName);
        vm::registerNative(608, nativeApplyJointForce);
        vm::registerNative(609, nativeNumJoints);
        vm::registerNative(610, nativeSetDefaultPolygroups);
        vm::registerNative(611, nativeSetDefaultJointFlags);
        vm::registerNative(613, nativeAttachActorToJoint);
        vm::registerNative(614, nativeDetachActorFromJoint);
        vm::registerNative(616, nativeTurnJointTo);
        vm::registerNative(617, nativeActorAttachedTo);
        vm::registerNative(618, nativeClosestJointTo);
        vm::registerNative(619, nativeJointNamed);
        vm::registerNative(620, nativeResetAnimationCache);
        vm::registerNative(621, nativeSetJointRot);
    }
}
