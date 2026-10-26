#pragma once

#include "engine/mesh.h"

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace rune {
    constexpr int32_t SkelPolyGroups = 16;
    constexpr int32_t SkelMaxJoints = 50;

    constexpr uint8_t AnimMaskPosX = 0x01;
    constexpr uint8_t AnimMaskPosY = 0x02;
    constexpr uint8_t AnimMaskPosZ = 0x04;
    constexpr uint8_t AnimMaskScaleXZ = 0x08;
    constexpr uint8_t AnimMaskScaleY = 0x10;
    constexpr uint8_t AnimMaskPitch = 0x20;
    constexpr uint8_t AnimMaskYaw = 0x40;
    constexpr uint8_t AnimMaskRoll = 0x80;

    constexpr int32_t JointGroupTorso = 1;
    constexpr int32_t JointFlagBlend = 0x01;
    constexpr int32_t JointFlagCollision = 0x10;

    struct SkelTriangle {
        uint16_t vertex[3] = {};
        uint8_t uv[3][2] = {};
        uint8_t group = 0;
    };

    struct SkelVertex {
        Vector point;
        Vector point2;
        int32_t joint1 = 0;
        int32_t joint2 = 0;
        float weight1 = 1;
    };

    struct SkelDecimation {
        std::vector<SkelTriangle> tris;
        std::vector<uint8_t> removed;
        size_t offset = 0;
        int32_t level = 0;
    };

    struct SkelMesh {
        std::vector<SkelTriangle> tris;
        std::vector<SkelVertex> verts;
        std::array<int32_t, SkelPolyGroups> groupFlags = {};
        std::array<Name, SkelPolyGroups> groupSkins;
        int32_t decCount = 0;
        std::vector<uint8_t> decData;
        mutable SkelDecimation decimation;
    };

    struct SkelJoint {
        int32_t parent = -1;
        Name name;
        int32_t group = 0;
        int32_t flags = 0;
        Rotator baseRot;
        std::array<Plane, 6> planes;
    };

    struct JointState {
        Vector pos;
        Rotator rot;
        Vector scale { 1, 1, 1 };
    };

    struct SkelSequence {
        AnimSequence info;
        std::vector<uint8_t> data;
        std::vector<uint32_t> frameOffsets;
    };

    struct SkelFrame {
        int16_t sequence = 0;
        Name event;
        BoundingBox bounds;
    };

    class USkelModel : public UPrimitive {
    public:
        void serialize(Reader& reader, Linker& source) override;

        const SkelSequence* findSequence(Name sequence) const;
        void decodeFrame(const SkelSequence& sequence, int32_t frame, std::vector<JointState>& states) const;
        int32_t jointNamed(Name jointName) const;

        int32_t numSkins = 0;
        int32_t rootJoint = 0;
        Vector posOffset;
        Rotator rotOffset;
        std::vector<SkelMesh> meshes;
        std::vector<SkelJoint> joints;
        std::vector<SkelSequence> sequences;
        std::vector<SkelFrame> frames;

    private:
        std::unordered_map<Name, size_t> sequenceIndex;
    };

    struct DynamicJoint {
        Vector relpos;
        Vector relvel;
        Vector relaccel;
        float accelBoneLength = 0;
        Rotator relrot;
        Rotator radialVel;
        Rotator radialAccel;
    };

    struct SkelPose {
        std::vector<Coords> joints;
        uint64_t revision = 0;
    };

    namespace skeletal {
        USkelModel* animationModel(UObject* actor);
        USkelModel* geometryModel(UObject* actor);
        const SkelPose* pose(UObject* actor);
        bool attachmentCoords(UObject* actor, Coords& coords);
        bool framePose(UObject* actor, Name sequence, int32_t frame, SkelPose& out);
        void setJointDelta(UObject* actor, int32_t joint, const Rotator& delta);
        Vector transformPoint(const Coords& coords, const Vector& point);
        Rotator coordsRotation(const Coords& coords);
        void skin(const USkelModel& model, const SkelMesh& mesh, const SkelPose& pose, bool mirrored, bool blend, std::vector<Vector>& out);
        float lodCurve(UObject* actor, const Vector& viewLocation);
        int32_t lodApproximation(const SkelMesh& mesh, float curve);
        int32_t decimationLevel(UObject* actor, const SkelMesh& mesh, float curve, int32_t skelApproximation, float lodPercentage, bool wire);
        const SkelDecimation& decimate(const SkelMesh& mesh, int32_t level);
        void setLodCurve(UObject* actor, float curve);
        void setDefaultJointFlags(UObject* actor);
        void tick(UObject* actor, float deltaSeconds);
        void applyJointForce(UObject* actor, int32_t joint, const Vector& force);
        DynamicJoint* dynamicJoint(UObject* actor, int32_t joint);
        void actorDestroyed(UObject* actor);
        void reset();
        void registerResources();
    }
}
