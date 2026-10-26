#include "core/objects.h"
#include "engine/props_engine.h"
#include "engine/resources.h"
#include "engine/world.h"
#include "render/texture_cache.h"

#include <catch_amalgamated.hpp>

namespace {
    rune::UTexture* makeTexture(rune::UClass* cls)
    {
        rune::UObject* object = rune::objects::newObject(cls, rune::objects::packageObject(rune::Name("Transient")), rune::Name(), 0);
        return rune::cast<rune::UTexture>(object);
    }
}

TEST_CASE("animated textures advance like UTexture::Update and Tick", "[data]")
{
    rune::engine::initialize(RUNE_GAME_DATA);
    rune::UClass* cls = rune::objects::findClass("Engine.Texture");
    REQUIRE(cls != nullptr);
    rune::UTexture* a = makeTexture(cls);
    rune::UTexture* b = makeTexture(cls);
    rune::UTexture* c = makeTexture(cls);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    REQUIRE(c != nullptr);
    rune::texture::AnimNext(a) = b;
    rune::texture::AnimNext(b) = c;
    rune::texture::AnimNext(c) = a;

    rune::texture::MaxFrameRate(a) = 0.0f;
    rune::TextureAnimation perFrame;
    CHECK(rune::TextureCache::advance(a, perFrame, 0.01) == b);
    CHECK(rune::TextureCache::advance(a, perFrame, 0.01) == b);
    CHECK(rune::TextureCache::advance(a, perFrame, 0.02) == c);
    CHECK(rune::TextureCache::advance(a, perFrame, 0.03) == a);

    rune::texture::MaxFrameRate(a) = 20.0f;
    rune::texture::MinFrameRate(a) = 10.0f;
    rune::TextureAnimation limited;
    limited.lastUpdate = 1.0;
    CHECK(rune::TextureCache::advance(a, limited, 1.03) == a);
    CHECK(rune::TextureCache::advance(a, limited, 1.06) == b);
    CHECK(limited.accumulator == 0.0f);
    CHECK(rune::TextureCache::advance(a, limited, 1.31) == c);
    CHECK(limited.accumulator == Catch::Approx(0.1f).margin(1e-5));

    rune::texture::MaxFrameRate(a) = 0.0f;
    rune::texture::PrimeCount(a) = 2;
    rune::TextureAnimation primed;
    CHECK(rune::TextureCache::advance(a, primed, 0.5) == a);
}
