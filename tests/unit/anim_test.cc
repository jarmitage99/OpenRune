#include "engine/mesh.h"

#include <catch_amalgamated.hpp>

namespace {
    void setupMesh(rune::UMesh& mesh)
    {
        mesh.frameVerts = 1;
        mesh.animFrames = 4;
        mesh.scale = { 1, 1, 1 };
        mesh.verts = { { 0, 0, 0 }, { 10, 0, 0 }, { 100, 0, 0 }, { 200, 0, 0 } };
        rune::AnimSequence first;
        first.name = rune::Name("TweenTestA");
        first.startFrame = 0;
        first.numFrames = 2;
        first.rate = 10;
        rune::AnimSequence second;
        second.name = rune::Name("TweenTestB");
        second.startFrame = 2;
        second.numFrames = 2;
        second.rate = 10;
        mesh.sequences = { first, second };
    }
}

TEST_CASE("tweening starts from the last drawn pose and moves toward the new sequence")
{
    rune::UMesh mesh;
    setupMesh(mesh);
    rune::MeshPoseCache cache;
    mesh.tweenFrame(rune::Name("TweenTestA"), 0.25f, cache, 1);
    REQUIRE(cache.verts.size() == 1);
    CHECK(cache.verts[0].x == 5.0f);
    mesh.tweenFrame(rune::Name("TweenTestB"), -0.5f, cache, 1);
    CHECK(cache.verts[0].x == 5.0f);
    mesh.tweenFrame(rune::Name("TweenTestB"), -0.25f, cache, 1);
    CHECK(cache.verts[0].x == 52.5f);
    mesh.tweenFrame(rune::Name("TweenTestB"), -0.125f, cache, 1);
    CHECK(cache.verts[0].x == 76.25f);
    mesh.tweenFrame(rune::Name("TweenTestB"), 0.0f, cache, 1);
    CHECK(cache.verts[0].x == 100.0f);
}

TEST_CASE("LOD mesh tweening stops once the tween indicator passes 0.97")
{
    rune::ULodMesh mesh;
    setupMesh(mesh);
    rune::MeshPoseCache cache;
    mesh.tweenFrame(rune::Name("TweenTestA"), 0.25f, cache, 1);
    mesh.tweenFrame(rune::Name("TweenTestB"), -0.5f, cache, 1);
    const float frames[] = { -0.25f, -0.125f, -0.0625f, -0.03125f, -0.015625f };
    for (float frame : frames) {
        mesh.tweenFrame(rune::Name("TweenTestB"), frame, cache, 1);
    }
    CHECK(cache.verts[0].x == 97.03125f);
    mesh.tweenFrame(rune::Name("TweenTestB"), -0.0078125f, cache, 1);
    CHECK(cache.verts[0].x == 97.03125f);
    CHECK(cache.tweenIndicator == 0.984375f);
}

TEST_CASE("LOD mesh tweening cannot return more vertices than the cached pose had")
{
    rune::ULodMesh mesh;
    setupMesh(mesh);
    rune::MeshPoseCache cache;
    CHECK(mesh.tweenFrame(rune::Name("TweenTestA"), 0.25f, cache, 0) == 0);
    CHECK(mesh.tweenFrame(rune::Name("TweenTestB"), -0.5f, cache, 1) == 0);
    CHECK(mesh.tweenFrame(rune::Name("TweenTestA"), 0.5f, cache, 1) == 1);
}
