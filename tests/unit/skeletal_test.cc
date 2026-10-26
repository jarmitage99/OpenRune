#include "core/objects.h"
#include "core/url.h"
#include "core/vm.h"
#include "engine/actor.h"
#include "engine/anim.h"
#include "engine/skeletal.h"
#include "engine/world.h"

#include <catch_amalgamated.hpp>

#include <algorithm>
#include <cmath>
#include <memory>

namespace {
    rune::USkelModel* loadModel(const char* path)
    {
        rune::engine::initialize(RUNE_GAME_DATA);
        rune::UObject* object = rune::objects::loadObject(path, rune::objects::intrinsicClass(rune::Name("SkelModel")));
        rune::objects::ensureLoaded(object);
        rune::objects::flushLoads();
        rune::USkelModel* model = rune::cast<rune::USkelModel>(object);
        REQUIRE(model != nullptr);
        return model;
    }

    rune::UObject* makeActor(const char* className)
    {
        rune::engine::initialize(RUNE_GAME_DATA);
        rune::UClass* cls = rune::objects::findClass(className);
        REQUIRE(cls != nullptr);
        rune::UObject* actor = rune::objects::newObject(cls, rune::objects::packageObject(rune::Name("Transient")), rune::Name(), 0);
        REQUIRE(actor != nullptr);
        return actor;
    }

    int32_t jointIndex(const rune::USkelModel& model, const char* name)
    {
        for (size_t i = 0; i < model.joints.size(); i++) {
            if (model.joints[i].name == rune::Name(name)) {
                return static_cast<int32_t>(i);
            }
        }
        FAIL("missing joint " << name);
        return -1;
    }
}

TEST_CASE("every skeletal model in the mesh packages loads to its export end", "[data]")
{
    rune::engine::initialize(RUNE_GAME_DATA);
    size_t models = 0;
    for (const char* package : { "creatures", "objects", "plants", "players", "weapons" }) {
        rune::Linker* linker = rune::objects::loadPackage(rune::Name(package));
        REQUIRE(linker != nullptr);
        for (size_t i = 0; i < linker->file().exports().size(); i++) {
            rune::UObject* object = linker->object(static_cast<int32_t>(i + 1));
            rune::objects::ensureLoaded(object);
            rune::objects::flushLoads();
            if (rune::USkelModel* model = rune::cast<rune::USkelModel>(object)) {
                models++;
                for (const rune::SkelSequence& sequence : model->sequences) {
                    CHECK(sequence.frameOffsets.size() == static_cast<size_t>(sequence.info.numFrames));
                }
            }
        }
    }
    CHECK(models == 144);
    CHECK(rune::objects::loadMismatches().empty());
}

TEST_CASE("Ragnar's skeletal model has the counts and offsets stored in players.ums", "[data]")
{
    rune::USkelModel* model = loadModel("Players.Ragnar");
    CHECK(model->meshes.size() == 25);
    CHECK(model->joints.size() == 38);
    CHECK(model->sequences.size() == 612);
    CHECK(model->frames.size() == 15594);
    CHECK(model->meshes[0].verts.size() == 659);
    CHECK(model->meshes[0].tris.size() == 1319);
    CHECK(model->posOffset.z == -4.0f);
    CHECK(model->rotOffset.yaw == -16384);
    CHECK(model->rotOffset.roll == -16384);
    CHECK(model->joints[17].name == rune::Name("Head"));
    CHECK(model->joints[17].parent == 16);
    CHECK(model->meshes[0].groupSkins[13] == rune::Name("RagnarRagd_head"));
    const rune::SkelSequence* stow = model->findSequence(rune::Name("IDL_ALL_drop1_AA0S"));
    REQUIRE(stow != nullptr);
    CHECK(stow->info.numFrames == 6);
    CHECK(stow->info.rate == 15.0f);
    REQUIRE(stow->info.notifys.size() == 1);
    CHECK(stow->info.notifys[0].time == 0.5f);
    CHECK(stow->info.notifys[0].function == rune::Name("DoStow"));
}

TEST_CASE("decoding a frame reads the masked joint channels as fixed point", "[data]")
{
    rune::USkelModel* model = loadModel("Players.Ragnar");
    std::vector<rune::JointState> states;
    model->decodeFrame(model->sequences[0], 0, states);
    REQUIRE(states.size() == 38);
    CHECK(states[0].pos.x == 0.0f);
    CHECK(states[0].pos.y == -8.0f / 256.0f);
    CHECK(states[0].pos.z == -25.0f / 256.0f);
    CHECK(states[0].rot.pitch == 0);
    CHECK(states[2].pos.x == 1280.0f / 256.0f);
    CHECK(states[2].pos.y == -1417.0f / 256.0f);
    CHECK(states[2].rot.pitch == -73);
    CHECK(states[2].rot.yaw == -16386);
    CHECK(states[2].rot.roll == -16386);
    CHECK(states[2].scale.x == 1.0f);
}

TEST_CASE("a scale channel carries separate X and Z values", "[data]")
{
    rune::USkelModel* model = loadModel("creatures.manowar");
    const rune::SkelSequence* swim = model->findSequence(rune::Name("swim"));
    REQUIRE(swim != nullptr);
    REQUIRE(swim->info.numFrames == 30);
    std::vector<rune::JointState> states;
    model->decodeFrame(*swim, 0, states);
    REQUIRE(states.size() == 6);
    CHECK(states[5].scale.x == 258.0f / 256.0f);
    CHECK(states[5].scale.z == 258.0f / 256.0f);
    CHECK(states[5].scale.y == 252.0f / 256.0f);
    model->decodeFrame(*swim, 1, states);
    CHECK(states[5].scale.x == 260.0f / 256.0f);
    CHECK(states[5].scale.y == 248.0f / 256.0f);
}

TEST_CASE("the base frame stands upright facing the actor's forward axis", "[data]")
{
    rune::UObject* ragnar = makeActor("RuneI.Ragnar");
    rune::actor::Location(ragnar) = { 100.0f, 200.0f, 300.0f };
    rune::actor::Rotation(ragnar) = {};
    rune::SkelPose pose;
    REQUIRE(rune::skeletal::framePose(ragnar, rune::Name("baseframe"), 0, pose));
    rune::USkelModel* model = rune::skeletal::animationModel(ragnar);
    REQUIRE(model != nullptr);
    const rune::Vector& head = pose.joints[static_cast<size_t>(jointIndex(*model, "Head"))].origin;
    const rune::Vector& toe = pose.joints[static_cast<size_t>(jointIndex(*model, "ltoe"))].origin;
    const rune::Vector& ankle = pose.joints[static_cast<size_t>(jointIndex(*model, "lankle"))].origin;
    const rune::Vector& leftHip = pose.joints[static_cast<size_t>(jointIndex(*model, "lhip"))].origin;
    CHECK(std::abs(head.z - (300.0f + 34.2f - 4.0f)) < 0.2f);
    CHECK(std::abs(toe.z - (300.0f - 40.1f - 4.0f)) < 0.2f);
    CHECK(toe.x > ankle.x + 7.0f);
    CHECK(std::abs(leftHip.y - (200.0f - 4.8f)) < 0.2f);
    rune::actor::Rotation(ragnar) = { 0, 16384, 0 };
    REQUIRE(rune::skeletal::framePose(ragnar, rune::Name("baseframe"), 0, pose));
    const rune::Vector& turnedToe = pose.joints[static_cast<size_t>(jointIndex(*model, "ltoe"))].origin;
    const rune::Vector& turnedAnkle = pose.joints[static_cast<size_t>(jointIndex(*model, "lankle"))].origin;
    CHECK(turnedToe.y > turnedAnkle.y + 7.0f);
}

TEST_CASE("animation lookups use the skeletal sequences of the actor or of an animation proxy's owner", "[data]")
{
    rune::UObject* ragnar = makeActor("RuneI.Ragnar");
    rune::AnimInfo info;
    REQUIRE(rune::findAnim(ragnar, rune::Name("IDL_ALL_drop1_AA0S"), info));
    CHECK(info.numFrames == 6);
    CHECK(info.rate == 15.0f);
    REQUIRE(info.notifys.size() == 1);
    CHECK(info.notifys[0].second == rune::Name("DoStow"));
    CHECK_FALSE(rune::findAnim(ragnar, rune::Name("NoSuchSequence"), info));
    rune::UObject* proxy = makeActor("Engine.AnimationProxy");
    CHECK_FALSE(rune::findAnim(proxy, rune::Name("IDL_ALL_drop1_AA0S"), info));
    rune::actor::Owner(proxy) = ragnar;
    REQUIRE(rune::findAnim(proxy, rune::Name("IDL_ALL_drop1_AA0S"), info));
    CHECK(info.numFrames == 6);
}

TEST_CASE("a joint trace reports the bit of the collision joint it crosses", "[data]")
{
    rune::engine::initialize(RUNE_GAME_DATA);
    rune::World world;
    REQUIRE(world.loadMap("DM-Hel"));
    rune::UObject* player = world.player();
    REQUIRE(player != nullptr);
    rune::skeletal::setDefaultJointFlags(player);
    rune::USkelModel* model = rune::skeletal::animationModel(player);
    REQUIRE(model != nullptr);
    int32_t head = jointIndex(*model, "Head");
    const rune::SkelPose* pose = rune::skeletal::pose(player);
    REQUIRE(pose != nullptr);
    rune::Vector target = pose->joints[static_cast<size_t>(head)].origin;
    rune::Vector across = rune::rotationAxes(rune::actor::Rotation(player)).yAxis;
    std::vector<rune::CheckResult> hits = world.multiLineCheck(target - across * 60.0f, target + across * 60.0f, {}, nullptr, rune::TRACE_Pawns | rune::TRACE_HitJoint);
    const rune::CheckResult* jointHit = nullptr;
    for (const rune::CheckResult& hit : hits) {
        if (hit.actor == player) {
            jointHit = &hit;
        }
    }
    REQUIRE(jointHit != nullptr);
    CHECK(jointHit->item == (1 << head));
    CHECK(jointHit->item2 == 0);
    rune::Vector above = target + rune::Vector { 0.0f, 0.0f, 40.0f };
    hits = world.multiLineCheck(above - across * 60.0f, above + across * 60.0f, {}, nullptr, rune::TRACE_Pawns | rune::TRACE_HitJoint);
    for (const rune::CheckResult& hit : hits) {
        CHECK(hit.actor != player);
    }
}

TEST_CASE("map URLs accept the Map/Portal form and relative portals keep the current map", "[data]")
{
    CHECK(rune::travelUrl("RagnarVillage?Difficulty=1", "ragnarvillage2/start", rune::TRAVEL_Relative) == "ragnarvillage2/start?Difficulty=1");
    CHECK(rune::travelUrl("RagnarVillage?Difficulty=1", "#start?Class=RuneI.Ragnar", rune::TRAVEL_Relative) == "RagnarVillage#start?Difficulty=1?Class=RuneI.Ragnar");
    rune::engine::initialize(RUNE_GAME_DATA);
    rune::World world;
    REQUIRE(world.loadMap("ragnarvillage2/start?Difficulty=1"));
    REQUIRE(world.level->outermost()->name == rune::Name("ragnarvillage2"));
    rune::UObject* player = world.player();
    REQUIRE(player != nullptr);
    rune::UObject* portal = nullptr;
    for (rune::UObject* a : world.actors) {
        if (a && a->isA(rune::Name("NavigationPoint")) && rune::actor::Tag(a) == rune::Name("start")) {
            portal = a;
        }
    }
    REQUIRE(portal != nullptr);
    rune::Vector offset = rune::actor::Location(player) - rune::actor::Location(portal);
    CHECK(offset.x * offset.x + offset.y * offset.y < 1.0f);
}

namespace {
    struct DynamicsWorld {
        std::unique_ptr<rune::World> world;
        rune::UObject* player = nullptr;
        int32_t joint = -1;

        DynamicsWorld()
        {
            rune::engine::initialize(RUNE_GAME_DATA);
            world = std::make_unique<rune::World>();
            REQUIRE(world->loadMap("DM-Hel"));
            player = world->player();
            REQUIRE(player != nullptr);
            rune::actor::bApplyLagToAccelerators(player) = 0;
            const rune::SkelPose* pose = rune::skeletal::pose(player);
            REQUIRE(pose != nullptr);
            for (size_t j = 1; j < pose->joints.size() && joint < 0; j++) {
                const rune::Coords& c = pose->joints[j];
                bool wide = true;
                for (const rune::Vector& axis : { c.xAxis, c.yAxis, c.zAxis }) {
                    wide = wide && std::abs(axis.normal().dot({ 1, 1, 1 })) * 100.0f / axis.size() > 30.0f;
                }
                if (wide) {
                    joint = static_cast<int32_t>(j);
                }
            }
            REQUIRE(joint > 0);
            for (int j = 0; j < 50; j++) {
                rune::actor::JointFlags(player, j) = 0;
            }
        }

        rune::DynamicJoint& dynamic(uint8_t flags)
        {
            rune::actor::JointFlags(player, joint) = flags;
            rune::DynamicJoint* state = rune::skeletal::dynamicJoint(player, joint);
            REQUIRE(state != nullptr);
            *state = rune::DynamicJoint {};
            return *state;
        }
    };
}

TEST_CASE("a spring joint integrates its velocity against the script default spring and damping", "[data]")
{
    DynamicsWorld fixture;
    rune::DynamicJoint& spring = fixture.dynamic(0x04);
    spring.relaccel = { 10, 0, 0 };
    spring.relpos = { 10, 0, 0 };
    spring.relvel = { 100, 0, 0 };
    rune::skeletal::tick(fixture.player, 0.1f);
    CHECK(spring.relpos.x == Catch::Approx(19.0));
    CHECK(spring.relvel.x == Catch::Approx(90.0));
    rune::skeletal::tick(fixture.player, 0.1f);
    CHECK(spring.relpos.x == Catch::Approx(25.48));
    CHECK(spring.relvel.x == Catch::Approx(64.8));
    CHECK(spring.relpos.y == 0.0f);

    spring.relaccel = { 10, 0, 0 };
    spring.relpos = { 9, 0, 0 };
    spring.relvel = { 30, 0, 0 };
    rune::skeletal::tick(fixture.player, 0.1f);
    CHECK(spring.relpos.x == Catch::Approx(11.88));
    CHECK(spring.relvel.x == Catch::Approx(28.8));

    spring.relaccel = { 10, 2, 0 };
    spring.relpos = { 10, 2, 0 };
    spring.relvel = {};
    rune::skeletal::tick(fixture.player, 0.1f);
    CHECK(spring.relpos.x == 10.0f);
    CHECK(spring.relpos.y == 2.0f);
    CHECK(spring.relvel.zero());
}

TEST_CASE("an accelerative joint keeps its bone length and stops outside the rotation threshold cone", "[data]")
{
    DynamicsWorld fixture;
    rune::DynamicJoint& accel = fixture.dynamic(0x02);
    accel.relaccel = { 5000, 0, 0 };
    accel.relpos = { 0, 10, 0 };
    accel.accelBoneLength = 10;
    rune::skeletal::tick(fixture.player, 0.1f);
    CHECK(accel.relpos.x == Catch::Approx(9.805807));
    CHECK(accel.relpos.y == Catch::Approx(1.961161));
    CHECK(accel.relvel.zero());
}

TEST_CASE("a gravity joint normalises to its bone length without the cone test", "[data]")
{
    DynamicsWorld fixture;
    rune::DynamicJoint& gravity = fixture.dynamic(0x80);
    gravity.relpos = { 0, 10, 0 };
    gravity.relvel = { 50, 0, 0 };
    gravity.accelBoneLength = 10;
    rune::skeletal::tick(fixture.player, 0.1f);
    CHECK(gravity.relpos.x == Catch::Approx(4.472136));
    CHECK(gravity.relpos.y == Catch::Approx(8.944272));
    CHECK(gravity.relvel.zero());
}

TEST_CASE("ApplyJointForce adds the force in joint space and only to dynamic joints", "[data]")
{
    DynamicsWorld fixture;
    rune::DynamicJoint& spring = fixture.dynamic(0x04);
    const rune::Coords coords = rune::skeletal::pose(fixture.player)->joints[static_cast<size_t>(fixture.joint)];
    rune::DynamicJoint* state = rune::skeletal::dynamicJoint(fixture.player, fixture.joint);
    REQUIRE(state == &spring);
    rune::vm::Call(fixture.player, rune::Name("ApplyJointForce")).i(fixture.joint).v({ 30, -20, 10 }).run();
    rune::Vector world = coords.xAxis * spring.relvel.x + coords.yAxis * spring.relvel.y + coords.zAxis * spring.relvel.z;
    CHECK(world.x == Catch::Approx(30.0).margin(1e-3));
    CHECK(world.y == Catch::Approx(-20.0).margin(1e-3));
    CHECK(world.z == Catch::Approx(10.0).margin(1e-3));
    rune::DynamicJoint& still = fixture.dynamic(0x10);
    rune::vm::Call(fixture.player, rune::Name("ApplyJointForce")).i(fixture.joint).v({ 30, -20, 10 }).run();
    CHECK(still.relvel.zero());
}

TEST_CASE("a spring joint replaces the animated joint position with its dynamic position", "[data]")
{
    DynamicsWorld fixture;
    rune::DynamicJoint& rest = fixture.dynamic(0x04);
    rune::skeletal::pose(fixture.player);
    REQUIRE_FALSE(rest.relaccel.zero());
    rune::skeletal::dynamicJoint(fixture.player, fixture.joint)->relpos = rest.relaccel;
    rune::Vector before = rune::skeletal::pose(fixture.player)->joints[static_cast<size_t>(fixture.joint)].origin;
    int32_t parent = rune::skeletal::animationModel(fixture.player)->joints[static_cast<size_t>(fixture.joint)].parent;
    REQUIRE(parent >= 0);
    rune::Vector parentX = rune::skeletal::pose(fixture.player)->joints[static_cast<size_t>(parent)].xAxis;
    rune::DynamicJoint* spring = rune::skeletal::dynamicJoint(fixture.player, fixture.joint);
    spring->relpos = spring->relaccel + rune::Vector { 5, 0, 0 };
    rune::Vector after = rune::skeletal::pose(fixture.player)->joints[static_cast<size_t>(fixture.joint)].origin;
    rune::Vector moved = after - before;
    CHECK(moved.x == Catch::Approx(parentX.x * 5).margin(1e-3));
    CHECK(moved.y == Catch::Approx(parentX.y * 5).margin(1e-3));
    CHECK(moved.z == Catch::Approx(parentX.z * 5).margin(1e-3));
}

namespace {
    rune::Coords relativeRotation(const rune::SkelPose& pose, int32_t joint, int32_t parent)
    {
        const auto& base = pose.joints[static_cast<size_t>(parent)];
        const auto& child = pose.joints[static_cast<size_t>(joint)];
        auto local = [&](const rune::Vector& axis) {
            return rune::Vector { base.xAxis.normal().dot(axis.normal()), base.yAxis.normal().dot(axis.normal()), base.zAxis.normal().dot(axis.normal()) };
        };
        return { {}, local(child.xAxis), local(child.yAxis), local(child.zAxis) };
    }

    float rotationDistance(const rune::Coords& a, const rune::Coords& b)
    {
        float trace = a.xAxis.dot(b.xAxis) + a.yAxis.dot(b.yAxis) + a.zAxis.dot(b.zAxis);
        return std::acos(std::clamp((trace - 1.0f) * 0.5f, -1.0f, 1.0f));
    }
}

TEST_CASE("Ragnar's forward walk interpolates the hip through the shortest rotation", "[data]")
{
    auto* ragnar = makeActor("RuneI.Ragnar");
    auto* model = rune::skeletal::animationModel(ragnar);
    REQUIRE(model != nullptr);
    rune::Name walk("walkforward7");
    const auto* sequence = model->findSequence(walk);
    REQUIRE(sequence != nullptr);
    int32_t hip = jointIndex(*model, "lhip");
    int32_t parent = model->joints[static_cast<size_t>(hip)].parent;
    rune::actor::AnimSequence(ragnar) = walk;
    for (int32_t f = 0; f < sequence->info.numFrames; f++) {
        CAPTURE(f);
        rune::SkelPose start;
        rune::SkelPose end;
        REQUIRE(rune::skeletal::framePose(ragnar, walk, f, start));
        REQUIRE(rune::skeletal::framePose(ragnar, walk, (f + 1) % sequence->info.numFrames, end));
        rune::actor::AnimFrame(ragnar) = (static_cast<float>(f) + 0.5f) / sequence->info.numFrames;
        const auto* midpoint = rune::skeletal::pose(ragnar);
        REQUIRE(midpoint != nullptr);
        auto a = relativeRotation(start, hip, parent);
        auto b = relativeRotation(end, hip, parent);
        auto mid = relativeRotation(*midpoint, hip, parent);
        float halfAngle = rotationDistance(a, b) * 0.5f;
        CHECK(rotationDistance(a, mid) == Catch::Approx(halfAngle).margin(0.002f));
        CHECK(rotationDistance(b, mid) == Catch::Approx(halfAngle).margin(0.002f));
    }
}

TEST_CASE("Ragnar's hip uses the shortest rotation for animation transitions and blends", "[data]")
{
    auto* ragnar = makeActor("RuneI.Ragnar");
    auto* model = rune::skeletal::animationModel(ragnar);
    REQUIRE(model != nullptr);
    rune::Name walk("walkforward7");
    const auto* sequence = model->findSequence(walk);
    REQUIRE(sequence != nullptr);
    int32_t hip = jointIndex(*model, "lhip");
    int32_t parent = model->joints[static_cast<size_t>(hip)].parent;
    rune::actor::AnimSequence(ragnar) = walk;
    rune::actor::AnimFrame(ragnar) = 7.0f / sequence->info.numFrames;
    auto start = *rune::skeletal::pose(ragnar);
    rune::SkelPose end;
    SECTION("tween to the first frame")
    {
        REQUIRE(rune::skeletal::framePose(ragnar, walk, 0, end));
        rune::actor::AnimFrame(ragnar) = -0.5f / sequence->info.numFrames;
    }
    SECTION("blend across the hip singularity")
    {
        REQUIRE(rune::skeletal::framePose(ragnar, walk, 8, end));
        rune::actor::JointFlags(ragnar, hip) |= rune::JointFlagBlend;
        rune::actor::BlendAnimSequence(ragnar) = walk;
        rune::actor::BlendAnimFrame(ragnar) = 8.0f / sequence->info.numFrames;
        rune::actor::BlendAnimAlpha(ragnar) = 0.5f;
    }
    const auto* midpoint = rune::skeletal::pose(ragnar);
    REQUIRE(midpoint != nullptr);
    auto a = relativeRotation(start, hip, parent);
    auto b = relativeRotation(end, hip, parent);
    auto mid = relativeRotation(*midpoint, hip, parent);
    float halfAngle = rotationDistance(a, b) * 0.5f;
    CHECK(rotationDistance(a, mid) == Catch::Approx(halfAngle).margin(0.002f));
    CHECK(rotationDistance(b, mid) == Catch::Approx(halfAngle).margin(0.002f));
}

namespace {
    float townRagnarMouthGap(rune::UObject* ragnar, const rune::SkelPose& pose)
    {
        auto* model = rune::skeletal::geometryModel(ragnar);
        REQUIRE(model != nullptr);
        REQUIRE(rune::actor::SkelMesh(ragnar) == 13);
        const auto& mesh = model->meshes[13];
        std::vector<rune::Vector> vertices;
        rune::skeletal::skin(*model, mesh, pose, false, true, vertices);
        REQUIRE(vertices.size() > 344);
        const auto& head = pose.joints[static_cast<size_t>(jointIndex(*model, "Head"))];
        return head.yAxis.normal().dot(vertices[344] - vertices[319]);
    }
}

TEST_CASE("Town Ragnar keeps his lips at rest through idle walking and combat poses", "[data]")
{
    auto* ragnar = makeActor("RuneI.TownRagnar");
    auto* model = rune::skeletal::animationModel(ragnar);
    REQUIRE(model != nullptr);
    for (const char* name : { "neutral_idle", "walkforward7", "S1_StandingAttackA", "S1_StandingAttackAReturn" }) {
        rune::Name sequenceName(name);
        const auto* sequence = model->findSequence(sequenceName);
        REQUIRE(sequence != nullptr);
        rune::actor::AnimSequence(ragnar) = sequenceName;
        for (int32_t frame = 0; frame < sequence->info.numFrames; frame++) {
            CAPTURE(name, frame);
            rune::actor::AnimFrame(ragnar) = static_cast<float>(frame) / sequence->info.numFrames;
            const auto* pose = rune::skeletal::pose(ragnar);
            REQUIRE(pose != nullptr);
            float gap = townRagnarMouthGap(ragnar, *pose);
            CHECK(gap > 0);
            CHECK(gap < 1.2f);
            rune::SkelPose sampled;
            REQUIRE(rune::skeletal::framePose(ragnar, sequenceName, frame, sampled));
            CHECK(townRagnarMouthGap(ragnar, sampled) == Catch::Approx(gap).margin(0.001f));
        }
    }
}

TEST_CASE("enabling scripted mouth motion replaces Ragnar's resting jaw correction immediately", "[data]")
{
    auto* ragnar = makeActor("RuneI.TownRagnar");
    auto* model = rune::skeletal::animationModel(ragnar);
    REQUIRE(model != nullptr);
    rune::actor::AnimSequence(ragnar) = rune::Name("neutral_idle");
    rune::actor::AnimFrame(ragnar) = 0;
    auto resting = *rune::skeletal::pose(ragnar);
    CHECK(townRagnarMouthGap(ragnar, resting) < 1.2f);
    rune::pawn::MaxMouthRot(ragnar) = 7000;
    rune::pawn::MaxMouthRotRate(ragnar) = 65535;
    auto scripted = *rune::skeletal::pose(ragnar);
    int32_t jaw = jointIndex(*model, "Jaw");
    int32_t parent = model->joints[static_cast<size_t>(jaw)].parent;
    auto neutral = relativeRotation(scripted, jaw, parent);
    CHECK(neutral.yAxis.y == Catch::Approx(1.0f).margin(0.001f));
    rune::vm::Call(ragnar, rune::Name("OpenMouth")).f(0.5f).f(1.0f).run();
    rune::vm::Call(ragnar, rune::Name("Jaw")).f(0.2f).run();
    CHECK(rune::pawn::MouthRot(ragnar) == 3500);
    auto opened = relativeRotation(*rune::skeletal::pose(ragnar), jaw, parent);
    CHECK(rotationDistance(neutral, opened) == Catch::Approx(0.335558f).margin(0.0001f));
    rune::vm::Call(ragnar, rune::Name("OpenMouth")).f(0.0f).f(1.0f).run();
    rune::vm::Call(ragnar, rune::Name("Jaw")).f(0.2f).run();
    CHECK(rune::pawn::MouthRot(ragnar) == 0);
    auto closed = relativeRotation(*rune::skeletal::pose(ragnar), jaw, parent);
    CHECK(rotationDistance(neutral, closed) < 0.001f);
}

TEST_CASE("Ragnar's resting jaw stays closed during the longhouse training dialogue", "[data]")
{
    rune::engine::initialize(RUNE_GAME_DATA);
    rune::World world;
    REQUIRE(world.loadMap("RagnarVillage2"));
    auto* player = world.player();
    REQUIRE(player != nullptr);
    for (int i = 0; i < 3; i++) {
        world.tick(1.0f / 30.0f);
    }
    REQUIRE(world.farMoveActor(player, { 2687, 620, -76.7f }, false, false));
    rune::UObject* trigger = nullptr;
    for (auto* actor : world.actors) {
        if (actor && actor->name == rune::Name("Trigger9")) {
            trigger = actor;
        }
    }
    REQUIRE(trigger != nullptr);
    rune::vm::Call(trigger, rune::Name("Touch")).obj(player).run();
    for (int i = 0; i < 450; i++) {
        world.tick(1.0f / 30.0f);
    }
    REQUIRE(rune::vm::stateName(player) == rune::Name("Scripting"));
    const auto* pose = rune::skeletal::pose(player);
    REQUIRE(pose != nullptr);
    CHECK(townRagnarMouthGap(player, *pose) < 1.2f);
}
