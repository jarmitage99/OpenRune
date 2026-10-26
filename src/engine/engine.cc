#include "audio/audio.h"
#include "core/config.h"
#include "core/natives.h"
#include "core/objects.h"
#include "core/vm.h"
#include "engine/props_engine.h"
#include "engine/props_fire.h"
#include "engine/mesh.h"
#include "engine/resources.h"
#include "engine/skeletal.h"
#include "engine/actor.h"
#include "engine/console.h"
#include "engine/world.h"

#include <filesystem>
#include <vector>

namespace rune::engine {
    namespace {
        bool actorEventAllowed(UObject* object)
        {
            if (!object->isA(engineClasses().actor)) {
                return true;
            }
            UObject* level = actor::Level(object);
            return !level || levelinfo::bBegunPlay(level);
        }

        std::vector<World*>& savedWorlds()
        {
            static std::vector<World*> saved;
            return saved;
        }

        bool enterActorWorld(UObject* object)
        {
            World* entry = World::entryLevel;
            if (!entry || entry == World::current || !object || !object->data || !object->isA(engineClasses().actor) || actor::XLevel(object) != entry->level) {
                return false;
            }
            savedWorlds().push_back(World::current);
            World::current = entry;
            return true;
        }

        void leaveActorWorld()
        {
            World::current = savedWorlds().back();
            savedWorlds().pop_back();
        }
    }

    void initializeCore(const std::string& gameRoot)
    {
        static bool initialized = false;
        if (initialized) {
            return;
        }
        initialized = true;
        std::filesystem::path root(gameRoot);
        config::initialize(root / "System");
        registerEngineResources();
        registerMeshResources();
        skeletal::registerResources();
        registerObjectNatives();
        registerEngineNatives();
        objects::initialize(root);
    }

    void initialize(const std::string& gameRoot)
    {
        static bool initialized = false;
        if (initialized) {
            return;
        }
        initialized = true;
        initializeCore(gameRoot);
        const char* packages[] = { "Engine", "Fire", "RuneI" };
        for (const char* package : packages) {
            objects::loadPackage(Name(package));
        }
        bindProps_engine();
        bindProps_fire();
        objects::flushLoads();
        bindEngineClasses();
        vm::setEventGate(actorEventAllowed);
        vm::setNativeContext(enterActorWorld, leaveActorWorld);
        console::loadBindings();
    }
}
