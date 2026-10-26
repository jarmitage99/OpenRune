#include "engine/skeletal.h"

#include "core/objects.h"
#include "core/vm.h"
#include "engine/actor.h"
#include "engine/reader.h"
#include "engine/world.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace rune {
    namespace {
        size_t readCount(Reader& reader)
        {
            int32_t count = reader.index();
            if (count < 0) {
                throw std::runtime_error("negative array count in skeletal mesh");
            }
            return static_cast<size_t>(count);
        }

        Vector readVector(Reader& reader)
        {
            Vector value;
            value.x = reader.f32();
            value.y = reader.f32();
            value.z = reader.f32();
            return value;
        }

        Rotator readRotator(Reader& reader)
        {
            Rotator value;
            value.pitch = reader.i32();
            value.yaw = reader.i32();
            value.roll = reader.i32();
            return value;
        }

        BoundingBox readBox(Reader& reader)
        {
            BoundingBox box;
            box.min = readVector(reader);
            box.max = readVector(reader);
            box.valid = reader.u8() != 0;
            return box;
        }

        int32_t valueCount(uint8_t mask)
        {
            int32_t count = 0;
            for (int bit = 0; bit < 8; bit++) {
                if (mask & (1 << bit)) {
                    count++;
                }
            }
            return (mask & AnimMaskScaleXZ) ? count + 1 : count;
        }

        Coords compose(const Coords& parent, const Coords& local)
        {
            Coords result;
            result.origin = skeletal::transformPoint(parent, local.origin);
            result.xAxis = parent.xAxis * local.xAxis.x + parent.yAxis * local.xAxis.y + parent.zAxis * local.xAxis.z;
            result.yAxis = parent.xAxis * local.yAxis.x + parent.yAxis * local.yAxis.y + parent.zAxis * local.yAxis.z;
            result.zAxis = parent.xAxis * local.zAxis.x + parent.yAxis * local.zAxis.y + parent.zAxis * local.zAxis.z;
            return result;
        }

        Coords localCoords(const Vector& origin, const Rotator& rotation, const Vector& scale)
        {
            Coords axes = rotationAxes(rotation);
            axes.origin = origin;
            axes.xAxis = axes.xAxis * scale.x;
            axes.yAxis = axes.yAxis * scale.y;
            axes.zAxis = axes.zAxis * scale.z;
            return axes;
        }

        Plane rotationQuaternion(const Rotator& rotation)
        {
            float pitch = unrToRad(rotation.pitch) * 0.5f;
            float yaw = unrToRad(rotation.yaw) * 0.5f;
            float roll = unrToRad(rotation.roll) * 0.5f;
            float sp = std::sin(pitch);
            float cp = std::cos(pitch);
            float sy = std::sin(yaw);
            float cy = std::cos(yaw);
            float sr = std::sin(roll);
            float cr = std::cos(roll);
            return { -sr * cp * cy + cr * sp * sy, -cr * sp * cy - sr * cp * sy,
                cr * cp * sy - sr * sp * cy, cr * cp * cy + sr * sp * sy };
        }

        Rotator slerpRotation(const Rotator& a, const Rotator& b, float alpha)
        {
            if (alpha <= 0 || a == b) {
                return a;
            }
            if (alpha >= 1) {
                return b;
            }
            Plane from = rotationQuaternion(a);
            Plane to = rotationQuaternion(b);
            float dot = from.x * to.x + from.y * to.y + from.z * to.z + from.w * to.w;
            if (dot < 0) {
                to = { -to.x, -to.y, -to.z, -to.w };
                dot = -dot;
            }
            float fromWeight = 1.0f - alpha;
            float toWeight = alpha;
            if (dot < 0.9995f) {
                float angle = std::acos(std::clamp(dot, 0.0f, 1.0f));
                float sine = std::sin(angle);
                fromWeight = std::sin((1.0f - alpha) * angle) / sine;
                toWeight = std::sin(alpha * angle) / sine;
            }
            Plane q { from.x * fromWeight + to.x * toWeight, from.y * fromWeight + to.y * toWeight,
                from.z * fromWeight + to.z * toWeight, from.w * fromWeight + to.w * toWeight };
            float length = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
            q = { q.x / length, q.y / length, q.z / length, q.w / length };
            Coords axes;
            axes.xAxis = { 1 - 2 * (q.y * q.y + q.z * q.z), 2 * (q.x * q.y + q.z * q.w), 2 * (q.x * q.z - q.y * q.w) };
            axes.yAxis = { 2 * (q.x * q.y - q.z * q.w), 1 - 2 * (q.x * q.x + q.z * q.z), 2 * (q.y * q.z + q.x * q.w) };
            axes.zAxis = { 2 * (q.x * q.z + q.y * q.w), 2 * (q.y * q.z - q.x * q.w), 1 - 2 * (q.x * q.x + q.y * q.y) };
            return skeletal::coordsRotation(axes);
        }

        JointState lerpState(const JointState& a, const JointState& b, float alpha)
        {
            JointState result;
            result.pos = a.pos + (b.pos - a.pos) * alpha;
            result.scale = a.scale + (b.scale - a.scale) * alpha;
            result.rot = slerpRotation(a.rot, b.rot, alpha);
            return result;
        }

        struct Channel {
            Name sequence;
            float frame = 0;
            std::vector<JointState> last;
            std::vector<JointState> from;
        };

        struct Instance {
            const USkelModel* model = nullptr;
            std::vector<DynamicJoint> dynamics;
            Channel body;
            Channel torso;
            Vector location;
            Rotator rotation;
            Vector prePivot;
            float drawScale = 0;
            Name blendSequence;
            float blendFrame = 0;
            float blendAlpha = 0;
            bool torsoActive = false;
            int32_t restingJaw = -1;
            bool mirrored = false;
            bool valid = false;
            float lodCurve = 0;
            uint64_t parentRevision = 0;
            std::vector<Rotator> jointDelta;
            SkelPose pose;
        };

        std::unordered_map<uint32_t, Instance>& instances()
        {
            static std::unordered_map<uint32_t, Instance> table;
            return table;
        }

        Instance& instanceFor(UObject* actor, const USkelModel& model)
        {
            Instance& instance = instances()[actor->handle];
            if (instance.model != &model) {
                instance = Instance {};
                instance.model = &model;
            }
            return instance;
        }

        USkelModel* skelOf(UObject* object)
        {
            if (!object) {
                return nullptr;
            }
            objects::ensureLoaded(object);
            return cast<USkelModel>(object);
        }

        void sequenceFrame(const USkelModel& model, const SkelSequence* sequence, float animFrame, bool interpolate, std::vector<JointState>& states, std::vector<JointState>& scratch)
        {
            if (!sequence || sequence->info.numFrames <= 0) {
                sequence = model.sequences.empty() ? nullptr : &model.sequences.front();
            }
            if (!sequence) {
                states.assign(model.joints.size(), JointState {});
                return;
            }
            int32_t count = sequence->info.numFrames;
            float position = std::max(animFrame, 0.0f) * static_cast<float>(count);
            int32_t index = std::clamp(static_cast<int32_t>(std::floor(position)), 0, count - 1);
            float alpha = std::clamp(position - static_cast<float>(index), 0.0f, 1.0f);
            model.decodeFrame(*sequence, index, states);
            if (interpolate && alpha > 0 && count > 1) {
                model.decodeFrame(*sequence, (index + 1) % count, scratch);
                for (size_t j = 0; j < states.size(); j++) {
                    states[j] = lerpState(states[j], scratch[j], alpha);
                }
            }
        }

        int32_t restingPlayerJaw(UObject* actor, USkelModel& model)
        {
            static const Name nameRunePlayer("RunePlayer");
            static const Name nameRagnar("Ragnar");
            static const Name namePlayers("Players");
            static const Name nameJaw("Jaw");
            if (model.name != nameRagnar || model.outermost()->name != namePlayers || !actor->isA(nameRunePlayer) || pawn::MaxMouthRotRate(actor) > 0) {
                return -1;
            }
            int32_t joint = model.jointNamed(nameJaw);
            return joint > 0 ? joint : -1;
        }

        void closeRestingJaw(int32_t joint, std::vector<JointState>& states)
        {
            constexpr int32_t closedJawOffset = 2048;
            if (joint >= 0) {
                states[static_cast<size_t>(joint)].rot.roll -= closedJawOffset;
            }
        }

        void applyJointDeltas(const std::vector<Rotator>& deltas, std::vector<JointState>& states)
        {
            for (size_t j = 0; j < states.size() && j < deltas.size(); j++) {
                states[j].rot.pitch -= deltas[j].pitch;
                states[j].rot.yaw -= deltas[j].yaw;
                states[j].rot.roll -= deltas[j].roll;
            }
        }

        void evaluate(const USkelModel& model, Channel& channel, Name sequenceName, float animFrame, bool interpolate)
        {
            const SkelSequence* sequence = model.findSequence(sequenceName);
            bool restarted = channel.sequence != sequenceName || (animFrame < 0 && channel.frame >= 0);
            if (restarted || channel.from.size() != model.joints.size()) {
                channel.from = channel.last.size() == model.joints.size() ? channel.last : std::vector<JointState>();
            }
            channel.sequence = sequenceName;
            channel.frame = animFrame;
            std::vector<JointState> target;
            std::vector<JointState> scratch;
            sequenceFrame(model, sequence, animFrame, interpolate, target, scratch);
            if (animFrame < 0 && sequence && channel.from.size() == target.size()) {
                float alpha = std::clamp(1.0f + animFrame * static_cast<float>(sequence->info.numFrames), 0.0f, 1.0f);
                for (size_t j = 0; j < target.size(); j++) {
                    target[j] = interpolate ? lerpState(channel.from[j], target[j], alpha) : channel.from[j];
                }
            }
            channel.last = std::move(target);
        }
    }

    namespace {
        constexpr uint8_t JointFlagAccel = 0x02;
        constexpr uint8_t JointFlagSpring = 0x04;
        constexpr uint8_t JointFlagAbsPosition = 0x20;
        constexpr uint8_t JointFlagAbsRotation = 0x40;
        constexpr uint8_t JointFlagGravity = 0x80;

        uint8_t jointFlags(UObject* actor, size_t joint)
        {
            return joint < static_cast<size_t>(SkelMaxJoints) ? actor::JointFlags(actor, static_cast<int>(joint)) : 0;
        }

        Vector safeNormal(const Vector& v)
        {
            float squared = v.y * v.y + v.x * v.x + v.z * v.z;
            if (squared < 1e-8f) {
                return {};
            }
            float scale = 1.0f / std::sqrt(squared);
            return { v.x * scale, v.y * scale, v.z * scale };
        }

        Vector localToWorld(const Coords& coords, const Vector& v)
        {
            return coords.xAxis * v.x + coords.yAxis * v.y + coords.zAxis * v.z;
        }

        Vector worldToLocal(const Coords& coords, const Vector& v)
        {
            Vector yz = coords.yAxis.cross(coords.zAxis);
            float determinant = coords.xAxis.dot(yz);
            if (determinant == 0) {
                return {};
            }
            return Vector { yz.dot(v), coords.zAxis.cross(coords.xAxis).dot(v), coords.xAxis.cross(coords.yAxis).dot(v) } * (1.0f / determinant);
        }

        Coords jointCoords(const Instance& instance, size_t joint)
        {
            if (joint < instance.pose.joints.size()) {
                return instance.pose.joints[joint];
            }
            Coords unit;
            unit.xAxis = { 1, 0, 0 };
            unit.yAxis = { 0, 1, 0 };
            unit.zAxis = { 0, 0, 1 };
            return unit;
        }

        void initializeDynamics(UObject* actor, const USkelModel& model, Instance& instance)
        {
            std::vector<JointState> states(model.joints.size());
            const SkelSequence* sequence = model.findSequence(actor::AnimSequence(actor));
            if (sequence && sequence->info.numFrames > 0) {
                model.decodeFrame(*sequence, 0, states);
            }
            instance.dynamics.assign(model.joints.size(), DynamicJoint {});
            for (size_t j = 0; j < model.joints.size(); j++) {
                DynamicJoint& joint = instance.dynamics[j];
                uint8_t flags = jointFlags(actor, j);
                if (flags & JointFlagAccel) {
                    joint.relpos = states[j].pos;
                    joint.relrot = states[j].rot;
                    joint.accelBoneLength = 1.0f;
                } else if (flags & JointFlagSpring) {
                    joint.relaccel = states[j].pos;
                    joint.relpos = joint.relaccel;
                    joint.relrot = states[j].rot;
                }
            }
        }

        float accelMagnitude(UObject* actor, size_t joint)
        {
            if (objects::editorMode()) {
                return 5000.0f;
            }
            static const Name nameGetAccelJointMagnitude("GetAccelJointMagnitude");
            vm::Call call(actor, nameGetAccelJointMagnitude);
            if (!call.valid()) {
                return 5000.0f;
            }
            call.i(static_cast<int32_t>(joint)).run();
            return *reinterpret_cast<float*>(call.returnValue());
        }

        void mangleTranslation(UObject* actor, DynamicJoint& joint, size_t index, Vector& pos)
        {
            uint8_t flags = jointFlags(actor, index);
            if (flags & JointFlagAbsPosition) {
                pos = joint.relpos;
            } else if (flags & JointFlagGravity) {
                joint.relaccel = {};
                joint.accelBoneLength = pos.size();
            } else if (flags & JointFlagAccel) {
                joint.relaccel = pos;
                float squared = pos.y * pos.y + pos.x * pos.x + pos.z * pos.z;
                if (squared >= 1e-8f) {
                    joint.relaccel = pos * (1.0f / std::sqrt(squared));
                }
                joint.relaccel = joint.relaccel * accelMagnitude(actor, index);
                joint.accelBoneLength = pos.size();
            } else if (flags & JointFlagSpring) {
                joint.relaccel = pos;
                pos = joint.relpos;
            }
        }

        void mangleRotation(UObject* actor, DynamicJoint& joint, size_t index, Rotator& rot)
        {
            uint8_t flags = jointFlags(actor, index);
            if (flags & JointFlagAbsRotation) {
                rot = joint.relrot;
            } else if (!(flags & (JointFlagAccel | JointFlagGravity)) && (flags & JointFlagSpring)) {
                joint.radialAccel = rot;
                rot = joint.relrot;
            }
        }

        void applyDynamics(UObject* actor, const USkelModel& model, Instance& instance, std::vector<JointState>& states)
        {
            if (instance.dynamics.size() != model.joints.size()) {
                initializeDynamics(actor, model, instance);
            }
            for (size_t j = 0; j < model.joints.size() && j < states.size(); j++) {
                if (jointFlags(actor, j) & 0xEE) {
                    mangleTranslation(actor, instance.dynamics[j], j, states[j].pos);
                    mangleRotation(actor, instance.dynamics[j], j, states[j].rot);
                }
                int children = 0;
                for (size_t c = j + 1; c < model.joints.size() && children < 4; c++) {
                    if (model.joints[c].parent != static_cast<int32_t>(j)) {
                        continue;
                    }
                    children++;
                    if (!(jointFlags(actor, c) & (JointFlagAccel | JointFlagGravity))) {
                        continue;
                    }
                    Rotator rest = unrealRotation(instance.dynamics[c].relaccel);
                    Rotator current = unrealRotation(instance.dynamics[c].relpos);
                    int32_t pitch = current.pitch - rest.pitch;
                    int32_t roll = rest.yaw > 0 ? -pitch : pitch;
                    states[j].rot.pitch += current.roll - rest.roll;
                    states[j].rot.yaw += current.yaw - rest.yaw;
                    states[j].rot.roll += roll;
                }
            }
        }

        void tickAccelerated(UObject* actor, const Instance& instance, DynamicJoint& joint, size_t index, float deltaSeconds, bool gravity)
        {
            float damp = 1.0f;
            float rotThreshold = 2000.0f;
            static const Name nameGetAccelJointParms("GetAccelJointParms");
            vm::Call call(actor, nameGetAccelJointParms);
            if (!objects::editorMode() && call.valid()) {
                call.i(static_cast<int32_t>(index)).f(damp).f(rotThreshold).run();
                damp = *reinterpret_cast<float*>(call.parameter(1));
                rotThreshold = *reinterpret_cast<float*>(call.parameter(2));
            }
            Vector old = joint.relpos;
            joint.relvel += joint.relaccel * deltaSeconds;
            joint.relpos += joint.relvel * deltaSeconds;
            if (!gravity && actor::bApplyLagToAccelerators(actor)) {
                joint.relpos += worldToLocal(jointCoords(instance, index), -actor::Velocity(actor)) * deltaSeconds;
            }
            if (!(joint.relpos == Vector {})) {
                joint.relpos = safeNormal(joint.relpos) * joint.accelBoneLength;
            }
            Vector delta = joint.relpos - old;
            delta = delta * (1.0f - damp);
            joint.relvel = delta * (1.0f / deltaSeconds);
            if (gravity) {
                return;
            }
            int32_t angle = static_cast<int32_t>(rotThreshold);
            float cosine = static_cast<float>(std::sin(static_cast<double>(((angle + 16384) >> 2) & 16383) * 6.283185307179586 / 16384.0));
            const Vector& rest = joint.relaccel;
            const Vector& pos = joint.relpos;
            float dot = (pos.y * rest.y + rest.x * pos.x) + pos.z * rest.z;
            double ratio = static_cast<double>(dot) / (static_cast<double>(pos.size()) * static_cast<double>(rest.size()));
            if (!(static_cast<double>(cosine) > ratio)) {
                return;
            }
            joint.relvel = {};
        }

        bool oppositeSigns(float before, float after)
        {
            return (before < 0 && after > 0) || (before > 0 && after < 0);
        }

        void tickSpring(UObject* actor, const Instance& instance, DynamicJoint& joint, size_t index, float deltaSeconds)
        {
            if (joint.relvel == Vector {} && joint.relpos == joint.relaccel) {
                return;
            }
            float damp = 1.0f;
            float springConstant = 2000.0f;
            Vector threshold;
            static const Name nameGetSpringJointParms("GetSpringJointParms");
            vm::Call call(actor, nameGetSpringJointParms);
            if (!objects::editorMode() && call.valid()) {
                call.i(static_cast<int32_t>(index)).f(damp).f(springConstant).v(threshold).run();
                damp = *reinterpret_cast<float*>(call.parameter(1));
                springConstant = *reinterpret_cast<float*>(call.parameter(2));
                threshold = *reinterpret_cast<Vector*>(call.parameter(3));
            }
            Vector oldOffset = joint.relpos - joint.relaccel;
            Vector force = oldOffset * -springConstant;
            force = force * deltaSeconds;
            joint.relvel += force;
            Vector step = joint.relvel * (damp * deltaSeconds);
            float stepSquared = (step.y * step.y + step.x * step.x) + step.z * step.z;
            float velocitySquared = (joint.relvel.y * joint.relvel.y + joint.relvel.x * joint.relvel.x) + joint.relvel.z * joint.relvel.z;
            if (velocitySquared > stepSquared) {
                joint.relvel -= step;
            } else {
                joint.relvel = {};
            }
            float radialScale = deltaSeconds * springConstant;
            joint.radialVel.pitch += static_cast<int32_t>(static_cast<float>(joint.radialAccel.pitch - joint.relrot.pitch) * radialScale);
            joint.radialVel.yaw += static_cast<int32_t>(static_cast<float>(joint.radialAccel.yaw - joint.relrot.yaw) * radialScale);
            joint.radialVel.roll += static_cast<int32_t>(static_cast<float>(joint.radialAccel.roll - joint.relrot.roll) * radialScale);
            joint.relpos += joint.relvel * deltaSeconds;
            joint.relrot.pitch += static_cast<int32_t>(static_cast<float>(joint.radialVel.pitch) * deltaSeconds);
            joint.relrot.yaw += static_cast<int32_t>(static_cast<float>(joint.radialVel.yaw) * deltaSeconds);
            joint.relrot.roll += static_cast<int32_t>(static_cast<float>(joint.radialVel.roll) * deltaSeconds);
            Coords coords = jointCoords(instance, index);
            Vector limit = worldToLocal(coords, threshold);
            limit = { std::abs(limit.x), std::abs(limit.y), std::abs(limit.z) };
            Vector offset = joint.relpos - joint.relaccel;
            offset = { std::clamp(offset.x, -limit.x, limit.x), std::clamp(offset.y, -limit.y, limit.y), std::clamp(offset.z, -limit.z, limit.z) };
            joint.relpos = joint.relaccel + offset;
            float speedSquared = (joint.relvel.y * joint.relvel.y + joint.relvel.x * joint.relvel.x) + joint.relvel.z * joint.relvel.z;
            if ((joint.relpos - joint.relaccel).sizeSquared() < 0.1f && speedSquared < 0.1f) {
                joint.relpos = joint.relaccel;
                joint.relvel = {};
            }
            Vector newOffset = joint.relpos - joint.relaccel;
            Vector direction;
            if (oppositeSigns(oldOffset.x, newOffset.x)) {
                direction.x = newOffset.x;
            }
            if (oppositeSigns(oldOffset.y, newOffset.y)) {
                direction.y = newOffset.y;
            }
            if (oppositeSigns(oldOffset.z, newOffset.z)) {
                direction.z = newOffset.z;
            }
            if (direction == Vector {}) {
                return;
            }
            static const Name nameJointChangedDirection("JointChangedDirection");
            vm::Call changed(actor, nameJointChangedDirection);
            if (changed.valid()) {
                changed.i(static_cast<int32_t>(index)).v(localToWorld(coords, joint.relvel)).v(localToWorld(coords, direction)).run();
            }
        }

        bool mirrored(UObject* actor)
        {
            UObject* parent = actor::AttachParent(actor);
            if (actor::bCarriedItem(actor) && parent) {
                actor::bMirrored(actor) = actor::bMirrored(parent);
            }
            return actor::bMirrored(actor) != 0;
        }

        void mirrorStates(std::vector<JointState>& states)
        {
            for (JointState& state : states) {
                state.pos.z = -state.pos.z;
                state.rot.pitch = 65536 - state.rot.pitch;
                state.rot.roll = 65536 - state.rot.roll;
            }
        }

        void buildJoints(UObject* actor, const USkelModel& model, const std::vector<JointState>& states, const Coords* attached, SkelPose& pose)
        {
            Rotator rotation = actor::Rotation(actor);
            if (actor::bMirrored(actor) && !actor::bCarriedItem(actor)) {
                rotation.yaw += 32768;
            }
            Coords base = localCoords(actor::Location(actor) + actor::PrePivot(actor), rotation, Vector { 1, 1, 1 } * actor::DrawScale(actor));
            if (attached) {
                base.origin = attached->origin;
                base.xAxis = attached->xAxis.normal() * actor::DrawScale(actor);
                base.yAxis = attached->yAxis.normal() * actor::DrawScale(actor);
                base.zAxis = attached->zAxis.normal() * actor::DrawScale(actor);
            }
            base = compose(base, localCoords(model.posOffset, model.rotOffset, Vector { 1, 1, 1 }));
            pose.joints.resize(model.joints.size());
            for (size_t j = 0; j < model.joints.size(); j++) {
                int32_t parent = model.joints[j].parent;
                const Coords& parentCoords = parent >= 0 && static_cast<size_t>(parent) < j ? pose.joints[static_cast<size_t>(parent)] : base;
                pose.joints[j] = compose(parentCoords, localCoords(states[j].pos, states[j].rot, states[j].scale));
            }
        }
    }

    void USkelModel::serialize(Reader& reader, Linker& source)
    {
        UPrimitive::serialize(reader, source);
        reader.i32();
        reader.i32();
        reader.i32();
        reader.i32();
        numSkins = reader.i32();
        rootJoint = reader.i32();
        meshes.resize(readCount(reader));
        for (SkelMesh& mesh : meshes) {
            reader.i32();
            reader.i32();
            mesh.tris.resize(readCount(reader));
            for (SkelTriangle& tri : mesh.tris) {
                for (uint16_t& vertex : tri.vertex) {
                    vertex = static_cast<uint16_t>(reader.i16());
                }
                for (auto& uv : tri.uv) {
                    uv[0] = reader.u8();
                    uv[1] = reader.u8();
                }
                tri.group = reader.u8();
            }
            mesh.verts.resize(readCount(reader));
            for (SkelVertex& vertex : mesh.verts) {
                vertex.point = readVector(reader);
                vertex.point2 = readVector(reader);
                vertex.joint1 = reader.i32();
                vertex.joint2 = reader.i32();
                vertex.weight1 = reader.f32();
            }
            mesh.decCount = reader.i32();
            mesh.decData.resize(readCount(reader));
            reader.bytes(mesh.decData.data(), mesh.decData.size());
            for (int32_t group = 0; group < SkelPolyGroups; group++) {
                mesh.groupFlags[static_cast<size_t>(group)] = reader.i32();
                mesh.groupSkins[static_cast<size_t>(group)] = source.readName(reader);
            }
        }
        joints.resize(readCount(reader));
        for (SkelJoint& joint : joints) {
            joint.parent = reader.i32();
            for (int i = 0; i < 4; i++) {
                reader.i32();
            }
            joint.name = source.readName(reader);
            joint.group = reader.i32();
            joint.flags = reader.i32();
            joint.baseRot = readRotator(reader);
            for (Plane& plane : joint.planes) {
                plane.x = reader.f32();
                plane.y = reader.f32();
                plane.z = reader.f32();
                plane.w = reader.f32();
            }
        }
        sequences.resize(readCount(reader));
        for (size_t index = 0; index < sequences.size(); index++) {
            SkelSequence& sequence = sequences[index];
            sequence.info.name = source.readName(reader);
            sequence.info.group = source.readName(reader);
            sequence.info.startFrame = reader.i32();
            sequence.info.numFrames = reader.i32();
            sequence.info.notifys.resize(readCount(reader));
            for (AnimNotify& notify : sequence.info.notifys) {
                notify.time = reader.f32();
                notify.function = source.readName(reader);
            }
            sequence.info.rate = reader.f32();
            std::stable_sort(sequence.info.notifys.begin(), sequence.info.notifys.end(), [](const AnimNotify& a, const AnimNotify& b) {
                return a.time < b.time;
            });
            sequence.data.resize(readCount(reader));
            reader.bytes(sequence.data.data(), sequence.data.size());
            size_t offset = 0;
            for (int32_t frame = 0; frame < sequence.info.numFrames; frame++) {
                sequence.frameOffsets.push_back(static_cast<uint32_t>(offset));
                for (size_t joint = 0; joint < joints.size(); joint++) {
                    if (offset >= sequence.data.size()) {
                        throw std::runtime_error("truncated skeletal animation");
                    }
                    offset += 1 + 2 * static_cast<size_t>(valueCount(sequence.data[offset]));
                }
            }
            if (offset != sequence.data.size()) {
                throw std::runtime_error("unexpected skeletal animation size");
            }
            sequenceIndex.emplace(sequence.info.name, index);
        }
        frames.resize(readCount(reader));
        for (SkelFrame& frame : frames) {
            frame.sequence = reader.i16();
            frame.event = source.readName(reader);
            frame.bounds = readBox(reader);
            if (readCount(reader) != 0) {
                throw std::runtime_error("unexpected joint states in skeletal frame");
            }
        }
        posOffset = readVector(reader);
        rotOffset = readRotator(reader);
    }

    const SkelSequence* USkelModel::findSequence(Name sequence) const
    {
        auto found = sequenceIndex.find(sequence);
        return found == sequenceIndex.end() ? nullptr : &sequences[found->second];
    }

    void USkelModel::decodeFrame(const SkelSequence& sequence, int32_t frame, std::vector<JointState>& states) const
    {
        states.assign(joints.size(), JointState {});
        const uint8_t* cursor = sequence.data.data() + sequence.frameOffsets[static_cast<size_t>(frame)];
        auto next = [&]() {
            int16_t value = static_cast<int16_t>(cursor[0] | (cursor[1] << 8));
            cursor += 2;
            return value;
        };
        for (JointState& state : states) {
            uint8_t mask = *cursor++;
            if (mask & AnimMaskPosX) {
                state.pos.x = static_cast<float>(next()) / 256.0f;
            }
            if (mask & AnimMaskPosY) {
                state.pos.y = static_cast<float>(next()) / 256.0f;
            }
            if (mask & AnimMaskPosZ) {
                state.pos.z = static_cast<float>(next()) / 256.0f;
            }
            if (mask & AnimMaskScaleXZ) {
                state.scale.x = static_cast<float>(next()) / 256.0f;
                state.scale.z = static_cast<float>(next()) / 256.0f;
            }
            if (mask & AnimMaskScaleY) {
                state.scale.y = static_cast<float>(next()) / 256.0f;
            }
            if (mask & AnimMaskPitch) {
                state.rot.pitch = next();
            }
            if (mask & AnimMaskYaw) {
                state.rot.yaw = next();
            }
            if (mask & AnimMaskRoll) {
                state.rot.roll = next();
            }
        }
    }

    int32_t USkelModel::jointNamed(Name jointName) const
    {
        for (size_t i = 0; i < joints.size(); i++) {
            if (joints[i].name == jointName) {
                return static_cast<int32_t>(i);
            }
        }
        return 0;
    }

    namespace skeletal {
        USkelModel* animationModel(UObject* actor)
        {
            if (!actor || !actor->data) {
                return nullptr;
            }
            if (USkelModel* model = skelOf(actor::Skeletal(actor))) {
                return model;
            }
            static const Name nameAnimationProxy("AnimationProxy");
            UObject* owner = actor::Owner(actor);
            if (owner && owner->data && actor->isA(nameAnimationProxy)) {
                return skelOf(actor::Skeletal(owner));
            }
            return nullptr;
        }

        USkelModel* geometryModel(UObject* actor)
        {
            if (offsets_actor.SubstituteMesh != MissingProperty) {
                if (USkelModel* substitute = skelOf(actor::SubstituteMesh(actor))) {
                    return substitute;
                }
            }
            return skelOf(actor::Skeletal(actor));
        }

        Vector transformPoint(const Coords& coords, const Vector& point)
        {
            return coords.origin + coords.xAxis * point.x + coords.yAxis * point.y + coords.zAxis * point.z;
        }

        Rotator coordsRotation(const Coords& coords)
        {
            Vector x = coords.xAxis.normal();
            Rotator result;
            result.pitch = static_cast<int32_t>(std::lround(std::atan2(x.z, std::sqrt(x.x * x.x + x.y * x.y)) * 32768.0f / Pi));
            result.yaw = static_cast<int32_t>(std::lround(std::atan2(x.y, x.x) * 32768.0f / Pi));
            Vector unrolledY = rotationAxes(result).yAxis;
            result.roll = static_cast<int32_t>(std::lround(std::atan2(coords.zAxis.normal().dot(unrolledY), coords.yAxis.normal().dot(unrolledY)) * 32768.0f / Pi));
            return result;
        }

        bool attachmentCoords(UObject* actor, Coords& coords)
        {
            UObject* parent = actor::bCarriedItem(actor) ? actor::AttachParent(actor) : nullptr;
            if (!parent || !parent->data || parent->destroyed || parent == actor) {
                return false;
            }
            const SkelPose* parentPose = pose(parent);
            size_t joint = actor::AttachParentJoint(actor);
            if (!parentPose || joint >= parentPose->joints.size()) {
                return false;
            }
            coords = parentPose->joints[joint];
            return true;
        }

        const SkelPose* pose(UObject* actor)
        {
            USkelModel* model = animationModel(actor);
            if (!model || model->joints.empty()) {
                return nullptr;
            }
            Coords attached;
            bool carried = attachmentCoords(actor, attached);
            uint64_t parentRevision = carried ? pose(actor::AttachParent(actor))->revision : 0;
            Instance& instance = instances()[actor->handle];
            UObject* proxy = actor::AnimProxy(actor);
            bool torsoActive = proxy && proxy->data && !proxy->destroyed && !actor::AnimSequence(proxy).none();
            Name torsoSequence = torsoActive ? actor::AnimSequence(proxy) : Name();
            float torsoFrame = torsoActive ? actor::AnimFrame(proxy) : 0.0f;
            bool mirror = mirrored(actor);
            int32_t restingJaw = restingPlayerJaw(actor, *model);
            bool interpolate = instance.lodCurve < 0.9f;
            bool blendAnimation = instance.lodCurve < 0.8f;
            bool same = instance.valid && instance.restingJaw == restingJaw && instance.mirrored == mirror && instance.model == model && instance.body.sequence == actor::AnimSequence(actor) && instance.body.frame == actor::AnimFrame(actor) && instance.torsoActive == torsoActive && (!torsoActive || (instance.torso.sequence == torsoSequence && instance.torso.frame == torsoFrame)) && instance.location.x == actor::Location(actor).x && instance.location.y == actor::Location(actor).y && instance.location.z == actor::Location(actor).z && instance.rotation == actor::Rotation(actor) && instance.drawScale == actor::DrawScale(actor) && instance.prePivot.x == actor::PrePivot(actor).x && instance.prePivot.y == actor::PrePivot(actor).y && instance.prePivot.z == actor::PrePivot(actor).z && instance.blendSequence == actor::BlendAnimSequence(actor) && instance.blendFrame == actor::BlendAnimFrame(actor) && instance.blendAlpha == actor::BlendAnimAlpha(actor) && instance.parentRevision == parentRevision;
            if (same) {
                return &instance.pose;
            }
            if (instance.model != model) {
                instance = Instance {};
                instance.model = model;
            }
            evaluate(*model, instance.body, actor::AnimSequence(actor), actor::AnimFrame(actor), interpolate);
            std::vector<JointState> states = instance.body.last;
            if (torsoActive) {
                evaluate(*model, instance.torso, torsoSequence, torsoFrame, interpolate);
                for (size_t j = 0; j < states.size(); j++) {
                    if (model->joints[j].group == JointGroupTorso) {
                        states[j] = instance.torso.last[j];
                    }
                }
            } else {
                instance.torso.last = instance.body.last;
                instance.torso.sequence = Name();
            }
            float blendAlpha = actor::BlendAnimAlpha(actor);
            if (blendAnimation && blendAlpha > 0 && !actor::BlendAnimSequence(actor).none()) {
                std::vector<JointState> blend;
                std::vector<JointState> scratch;
                sequenceFrame(*model, model->findSequence(actor::BlendAnimSequence(actor)), actor::BlendAnimFrame(actor), interpolate, blend, scratch);
                for (size_t j = 0; j < states.size() && j < static_cast<size_t>(SkelMaxJoints); j++) {
                    if (interpolate && (actor::JointFlags(actor, static_cast<int>(j)) & JointFlagBlend)) {
                        states[j] = lerpState(states[j], blend[j], std::min(blendAlpha, 1.0f));
                    }
                }
            }
            instance.torsoActive = torsoActive;
            instance.restingJaw = restingJaw;
            instance.location = actor::Location(actor);
            instance.rotation = actor::Rotation(actor);
            instance.drawScale = actor::DrawScale(actor);
            instance.prePivot = actor::PrePivot(actor);
            instance.blendSequence = actor::BlendAnimSequence(actor);
            instance.blendFrame = actor::BlendAnimFrame(actor);
            instance.blendAlpha = blendAlpha;
            instance.parentRevision = parentRevision;
            instance.mirrored = mirror;
            instance.valid = true;

            applyDynamics(actor, *model, instance, states);
            closeRestingJaw(restingJaw, states);
            if (mirror) {
                mirrorStates(states);
            }
            applyJointDeltas(instance.jointDelta, states);
            buildJoints(actor, *model, states, carried ? &attached : nullptr, instance.pose);
            static uint64_t revisions = 0;
            instance.pose.revision = ++revisions;
            return &instance.pose;
        }

        bool framePose(UObject* actor, Name sequence, int32_t frame, SkelPose& out)
        {
            USkelModel* model = animationModel(actor);
            if (!model || model->joints.empty()) {
                return false;
            }
            const SkelSequence* found = model->findSequence(sequence);
            if (!found) {
                found = model->findSequence(Name("baseframe"));
            }
            std::vector<JointState> states(model->joints.size());
            if (found && found->info.numFrames > 0) {
                model->decodeFrame(*found, std::clamp(frame, 0, found->info.numFrames - 1), states);
            }
            closeRestingJaw(restingPlayerJaw(actor, *model), states);
            if (mirrored(actor)) {
                mirrorStates(states);
            }
            auto existing = instances().find(actor->handle);
            if (existing != instances().end() && existing->second.model == model) {
                applyJointDeltas(existing->second.jointDelta, states);
            }
            Coords attached;
            bool carried = attachmentCoords(actor, attached);
            buildJoints(actor, *model, states, carried ? &attached : nullptr, out);
            return true;
        }

        void setJointDelta(UObject* actor, int32_t joint, const Rotator& delta)
        {
            USkelModel* model = animationModel(actor);
            if (!model || joint < 0 || static_cast<size_t>(joint) >= model->joints.size()) {
                return;
            }
            Instance& instance = instances()[actor->handle];
            if (instance.model != model) {
                instance = Instance {};
                instance.model = model;
            }
            instance.jointDelta.resize(model->joints.size());
            Rotator& slot = instance.jointDelta[static_cast<size_t>(joint)];
            if (!(slot == delta)) {
                slot = delta;
                instance.valid = false;
            }
        }

        void skin(const USkelModel& model, const SkelMesh& mesh, const SkelPose& pose, bool mirrored, bool blend, std::vector<Vector>& out)
        {
            Vector flip { 1, 1, mirrored ? -1.0f : 1.0f };
            out.resize(mesh.verts.size());
            size_t count = std::min(pose.joints.size(), model.joints.size());
            for (size_t i = 0; i < mesh.verts.size(); i++) {
                const SkelVertex& vertex = mesh.verts[i];
                size_t first = static_cast<size_t>(std::clamp(vertex.joint1, 0, static_cast<int32_t>(count) - 1));
                size_t second = static_cast<size_t>(std::clamp(vertex.joint2, 0, static_cast<int32_t>(count) - 1));
                Vector a = transformPoint(pose.joints[first], vertex.point * flip);
                if (!blend || vertex.weight1 == 1.0f) {
                    out[i] = a;
                    continue;
                }
                Vector b = transformPoint(pose.joints[second], vertex.point2 * flip);
                out[i] = a * vertex.weight1 + b * (1.0f - vertex.weight1);
            }
        }

        float lodCurve(UObject* actor, const Vector& viewLocation)
        {
            float distance = (actor::Location(actor) - viewLocation).size();
            float maxDistance = actor::LODDistMax(actor) * actor::DrawScale(actor);
            double t = maxDistance > 0 ? static_cast<double>(std::min(std::max(distance, 0.0f), maxDistance) / maxDistance) : 1.0;
            double curve = 0;
            switch (actor::LODCurve(actor)) {
            case 1:
                curve = t * t;
                break;
            case 2:
                curve = t + (t * t - t) * 0.5;
                break;
            case 3:
                curve = t;
                break;
            case 4:
                curve = std::sqrt(t) + (t - std::sqrt(t)) * 0.5;
                break;
            case 5:
                curve = std::sqrt(t);
                break;
            case 6:
                curve = 1.0 - std::cos(t * 3.141592653589793 * 0.5);
                break;
            default:
                break;
            }
            float result = static_cast<float>(curve);
            float low = 1.0f - actor::LODPercentMax(actor);
            float high = 1.0f - actor::LODPercentMin(actor);
            if (low > result) {
                return low;
            }
            return high <= result ? high : result;
        }

        int32_t lodApproximation(const SkelMesh& mesh, float curve)
        {
            return mesh.decCount - static_cast<int32_t>(static_cast<double>(mesh.decCount - 1) * static_cast<double>(curve));
        }

        int32_t decimationLevel(UObject* actor, const SkelMesh& mesh, float curve, int32_t skelApproximation, float lodPercentage, bool wire)
        {
            int32_t level = std::max(static_cast<int32_t>(static_cast<double>(mesh.decCount - 1) * static_cast<double>(curve)), 0);
            double budget = 2500.0 * static_cast<double>(lodPercentage);
            if (budget < static_cast<double>(skelApproximation) && actor::LODCurve(actor) != 0) {
                float ratio = static_cast<float>(mesh.decCount - level) / static_cast<float>(skelApproximation);
                if (isPlayerPawn(actor)) {
                    ratio = static_cast<float>(static_cast<double>(ratio) * 1.25);
                }
                level = mesh.decCount - static_cast<int32_t>(budget * static_cast<double>(ratio));
                level = level <= 0 ? 0 : std::min(level, mesh.decCount - 1);
            }
            return wire ? 0 : level;
        }

        const SkelDecimation& decimate(const SkelMesh& mesh, int32_t level)
        {
            SkelDecimation& state = mesh.decimation;
            if (state.tris.size() != mesh.tris.size()) {
                state = SkelDecimation {};
                state.tris = mesh.tris;
                state.removed.assign(mesh.tris.size(), 0);
            }
            const std::vector<uint8_t>& data = mesh.decData;
            auto u16 = [&](size_t at) {
                return static_cast<size_t>(data[at] | (data[at + 1] << 8));
            };
            auto back = [&](size_t at) {
                return static_cast<size_t>(static_cast<uint32_t>(data[at] | (data[at + 1] << 8) | (data[at + 2] << 16)) | (static_cast<uint32_t>(data[at + 3]) << 24));
            };
            auto apply = [&](size_t at, bool forward) {
                size_t corners = u16(at + 14);
                size_t removedTris = u16(at + 12);
                if (at + 16 + corners * 8 + removedTris * 2 > data.size()) {
                    return false;
                }
                uint16_t vertex = static_cast<uint16_t>(u16(forward ? at + 6 : at + 4));
                for (size_t c = 0; c < corners; c++) {
                    size_t entry = at + 16 + c * 8;
                    size_t tri = u16(entry);
                    size_t corner = u16(entry + 2);
                    if (tri >= state.tris.size() || corner > 2) {
                        continue;
                    }
                    SkelTriangle& target = state.tris[tri];
                    target.vertex[2 - corner] = vertex;
                    target.uv[2 - corner][0] = data[entry + (forward ? 6 : 4)];
                    target.uv[2 - corner][1] = data[entry + (forward ? 7 : 5)];
                }
                for (size_t r = 0; r < removedTris; r++) {
                    size_t tri = u16(at + 16 + corners * 8 + r * 2);
                    if (tri < state.removed.size()) {
                        state.removed[tri] = forward ? 1 : 0;
                    }
                }
                return true;
            };
            while (state.level < level && state.offset + 16 <= data.size()) {
                size_t at = state.offset;
                if (!apply(at, true)) {
                    break;
                }
                state.offset = at + 16 + u16(at + 14) * 8 + u16(at + 12) * 2;
                state.level++;
            }
            while (state.level > level && state.offset + 4 <= data.size() && back(state.offset) <= state.offset) {
                size_t at = state.offset - back(state.offset);
                if (!apply(at, false)) {
                    break;
                }
                state.offset = at;
                state.level--;
            }
            return state;
        }

        void setLodCurve(UObject* actor, float curve)
        {
            USkelModel* model = animationModel(actor);
            if (!model) {
                return;
            }
            Instance& instance = instanceFor(actor, *model);
            if ((instance.lodCurve < 0.9f) != (curve < 0.9f) || (instance.lodCurve < 0.8f) != (curve < 0.8f)) {
                instance.valid = false;
            }
            instance.lodCurve = curve;
        }

        void setDefaultJointFlags(UObject* actor)
        {
            USkelModel* model = animationModel(actor);
            if (!model) {
                return;
            }
            for (int32_t j = 0; j < SkelMaxJoints; j++) {
                actor::JointFlags(actor, j) = static_cast<size_t>(j) < model->joints.size() ? static_cast<uint8_t>(model->joints[static_cast<size_t>(j)].flags) : 0;
            }
            Instance& instance = instanceFor(actor, *model);
            instance.jointDelta.clear();
            initializeDynamics(actor, *model, instance);
            instance.valid = false;
        }

        void tick(UObject* actor, float deltaSeconds)
        {
            USkelModel* model = skelOf(actor::Skeletal(actor));
            if (!model || model->joints.empty()) {
                return;
            }
            Instance& instance = instanceFor(actor, *model);
            if (instance.dynamics.size() != model->joints.size()) {
                initializeDynamics(actor, *model, instance);
            }
            bool jointsBlock = actor::bJointsBlock(actor) != 0;
            std::vector<Vector> saved;
            if (jointsBlock) {
                for (const DynamicJoint& joint : instance.dynamics) {
                    saved.push_back(joint.relpos);
                }
            }
            for (size_t j = 0; j < model->joints.size(); j++) {
                uint8_t flags = jointFlags(actor, j);
                DynamicJoint& joint = instance.dynamics[j];
                if (flags & JointFlagGravity) {
                    tickAccelerated(actor, instance, joint, j, deltaSeconds, true);
                } else if (flags & JointFlagAccel) {
                    tickAccelerated(actor, instance, joint, j, deltaSeconds, false);
                } else if (flags & JointFlagSpring) {
                    tickSpring(actor, instance, joint, j, deltaSeconds);
                } else {
                    continue;
                }
                instance.valid = false;
            }
            if (!jointsBlock || actor::StandingCount(actor) == 0 || !World::current) {
                return;
            }
            for (UObject* other : World::current->actors) {
                if (!other || other->destroyed || actor::Base(other) != actor || actor::BaseJoint(other) >= model->joints.size()) {
                    continue;
                }
                size_t b = actor::BaseJoint(other);
                Vector delta = localToWorld(jointCoords(instance, b), instance.dynamics[b].relpos - saved[b]);
                CheckResult hit;
                World::current->moveActor(other, delta, actor::Rotation(other), hit, false, false, true, false);
            }
        }

        DynamicJoint* dynamicJoint(UObject* actor, int32_t joint)
        {
            USkelModel* model = skelOf(actor::Skeletal(actor));
            if (!model || joint < 0 || static_cast<size_t>(joint) >= model->joints.size()) {
                return nullptr;
            }
            Instance& instance = instanceFor(actor, *model);
            if (instance.dynamics.size() != model->joints.size()) {
                initializeDynamics(actor, *model, instance);
            }
            instance.valid = false;
            return &instance.dynamics[static_cast<size_t>(joint)];
        }

        void applyJointForce(UObject* actor, int32_t joint, const Vector& force)
        {
            USkelModel* model = skelOf(actor::Skeletal(actor));
            if (!model || joint < 0 || static_cast<size_t>(joint) >= model->joints.size() || !(jointFlags(actor, static_cast<size_t>(joint)) & (JointFlagAccel | JointFlagSpring | JointFlagGravity))) {
                return;
            }
            Instance& instance = instanceFor(actor, *model);
            if (instance.dynamics.size() != model->joints.size()) {
                initializeDynamics(actor, *model, instance);
            }
            instance.dynamics[static_cast<size_t>(joint)].relvel += worldToLocal(jointCoords(instance, static_cast<size_t>(joint)), force);
            instance.valid = false;
        }

        void actorDestroyed(UObject* actor)
        {
            instances().erase(actor->handle);
        }

        void reset()
        {
            instances().clear();
        }

        void registerResources()
        {
            objects::registerFactory(Name("SkelModel"), [] { return new USkelModel(); });
        }
    }
}
