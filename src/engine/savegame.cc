#include "engine/savegame.h"

#include "core/config.h"
#include "core/objects.h"
#include "core/package_writer.h"
#include "core/vm.h"
#include "engine/actor.h"
#include "engine/console.h"
#include "engine/map_writer.h"
#include "engine/mesh.h"
#include "engine/props_engine.h"
#include "engine/resources.h"
#include "engine/world.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <functional>
#include <unordered_set>

namespace rune {
    namespace savegame {
        namespace {
            struct State {
                std::filesystem::path root;
                std::filesystem::path pending;
            };

            State& state()
            {
                static State instance;
                return instance;
            }

            std::string lower(std::string text)
            {
                for (char& c : text) {
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                }
                return text;
            }

            std::filesystem::path defaultRoot()
            {
                return config::userDirectory() / "Save";
            }

            bool classNamed(const UClass* cls, std::initializer_list<const char*> names)
            {
                for (const UClass* current = cls; current; current = current->superClass()) {
                    for (const char* candidate : names) {
                        if (current->name == Name(candidate)) {
                            return true;
                        }
                    }
                }
                return false;
            }

            bool transientObject(UObject* object)
            {
                for (const UClass* current = object->cls; current; current = current->superClass()) {
                    if (current->classFlags & CLASS_Transient) {
                        return true;
                    }
                }
                return classNamed(object->cls, { "Window", "GC", "Player", "Canvas", "Console", "Client", "GameDirectory" });
            }

            bool nativeResource(UObject* object)
            {
                return cast<UField>(object) || cast<UTexture>(object) || cast<UPalette>(object) || cast<USound>(object) || cast<UMusic>(object) || cast<UPolys>(object) || cast<UPrimitive>(object) || cast<UFont>(object) || cast<UMesh>(object) || cast<ULevel>(object);
            }

            bool isActor(UObject* object)
            {
                return object->cls && engineClasses().actor && object->isA(engineClasses().actor);
            }

            void forEachReference(UStruct* structType, uint8_t* data, const std::function<void(UObject*)>& visit)
            {
                if (!structType || !data) {
                    return;
                }
                for (UProperty* property : structType->properties) {
                    if (property->propertyFlags & (CPF_Transient | CPF_Native)) {
                        continue;
                    }
                    for (int32_t index = 0; index < property->arrayDim; index++) {
                        uint8_t* memory = data + property->offset + static_cast<size_t>(index) * property->elementSize();
                        if (property->type == PropType::Object || property->type == PropType::Class) {
                            visit(*reinterpret_cast<UObject**>(memory));
                        } else if (property->type == PropType::Struct) {
                            forEachReference(property->structType, memory, visit);
                        }
                    }
                }
            }
        }

        void setSaveRoot(const std::filesystem::path& root)
        {
            state().root = root;
        }

        std::filesystem::path saveRoot()
        {
            if (state().root.empty()) {
                state().root = defaultRoot();
            }
            std::error_code error;
            std::filesystem::create_directories(state().root, error);
            return state().root;
        }

        std::filesystem::path slotFile(int32_t index)
        {
            return saveRoot() / ("Save" + std::to_string(index) + ".rsa");
        }

        std::string mapBaseName(const std::string& url)
        {
            std::string map = url.substr(0, url.find('?'));
            size_t hash = map.find('#');
            if (hash != std::string::npos) {
                map = map.substr(0, hash);
            }
            map = std::filesystem::path(map).filename().string();
            size_t dot = map.rfind('.');
            if (dot != std::string::npos) {
                map = map.substr(0, dot);
            }
            return map;
        }

        bool saveLevel(World& world, const std::filesystem::path& file, bool includePlayer, std::string* error)
        {
            ULevel* level = world.level;
            if (!level) {
                if (error) {
                    *error = "no level loaded";
                }
                return false;
            }
            UObject* player = world.player();
            std::unordered_set<UObject*> excluded;
            if (!includePlayer && player) {
                for (UObject* a : world.actors) {
                    if (!a || a->destroyed) {
                        continue;
                    }
                    UObject* owner = a;
                    int guard = 0;
                    while (owner && owner != player && guard++ < 32) {
                        owner = actor::Owner(owner);
                    }
                    if (owner == player) {
                        excluded.insert(a);
                    }
                }
            }

            PackageWriter writer(Name("SaveGame"));
            UObject* mapPackage = level->outermost();
            writer.addExport(level, level->outer);
            std::deque<UObject*> queue;
            for (UObject* a : world.actors) {
                if (a && !a->destroyed && !excluded.count(a) && !writer.isExport(a) && !transientObject(a)) {
                    writer.addExport(a, a->outer);
                    queue.push_back(a);
                }
            }

            auto exportable = [&](UObject* object) {
                if (!object || object->destroyed || writer.isExport(object) || !object->cls || !object->outer) {
                    return false;
                }
                if (isActor(object) || transientObject(object) || nativeResource(object)) {
                    return false;
                }
                return !object->linker || object->linker->savedGame || object->outermost() == mapPackage;
            };
            auto visit = [&](UObject* object) {
                if (exportable(object)) {
                    writer.addExport(object, object->outer);
                    queue.push_back(object);
                }
            };
            auto addModel = [&](UModel* model) {
                if (!model || writer.isExport(model) || model->outermost() != mapPackage) {
                    return;
                }
                objects::ensureLoaded(model);
                writer.addExport(model, model->outer);
                if (model->polys && !writer.isExport(model->polys) && model->polys->outermost() == mapPackage) {
                    objects::ensureLoaded(model->polys);
                    writer.addExport(model->polys, model->polys->outer);
                }
            };
            addModel(level->model);
            for (UObject* a : world.actors) {
                if (a && !a->destroyed && writer.isExport(a)) {
                    addModel(cast<UModel>(actor::Brush(a)));
                }
            }
            while (!queue.empty()) {
                UObject* object = queue.front();
                queue.pop_front();
                if (object->outer && exportable(object->outer)) {
                    visit(object->outer);
                }
                forEachReference(object->cls, object->data, visit);
            }
            objects::flushLoads();

            writer.setFlagSelector([&](UObject* object) {
                constexpr uint32_t loadFlags = 0x00070000;
                if (isActor(object)) {
                    return loadFlags | RF_Transactional;
                }
                if (object->outermost() == mapPackage) {
                    return loadFlags | (object->flags & (RF_Transactional | RF_Public | RF_Standalone));
                }
                return loadFlags;
            });

            writer.setFilter([&](UObject* object) {
                if (!object->outer) {
                    return true;
                }
                if (cast<UClass>(object)) {
                    return true;
                }
                if (isActor(object) || transientObject(object)) {
                    return false;
                }
                return object->linker && !object->linker->savedGame;
            });

            std::string map = mapBaseName(world.url);
            writer.setNativeSerializer([&](PackageWriter& out, UObject* object, Writer& data) {
                if (object == level) {
                    int32_t count = static_cast<int32_t>(world.actors.size());
                    data.i32(count);
                    data.i32(count);
                    for (UObject* a : world.actors) {
                        out.writeObject(data, a && !a->destroyed && out.isExport(a) ? a : nullptr);
                    }
                    std::vector<std::string> options;
                    size_t mark = world.url.find('?');
                    while (mark != std::string::npos) {
                        size_t next = world.url.find('?', mark + 1);
                        std::string option = world.url.substr(mark + 1, next == std::string::npos ? std::string::npos : next - mark - 1);
                        if (!option.empty()) {
                            options.push_back(option);
                        }
                        mark = next;
                    }
                    if (options.empty() && player) {
                        options.push_back("Name=Player");
                        options.push_back("Class=" + std::string(player->cls->outermost()->name.c_str()) + "." + player->cls->name.c_str());
                    }
                    data.string("Rune");
                    data.string("");
                    data.string(map);
                    data.string("");
                    data.index(static_cast<int32_t>(options.size()));
                    for (const std::string& option : options) {
                        data.string(option);
                    }
                    data.i32(7777);
                    data.u32(1);
                    out.writeObject(data, level->model);
                    data.index(static_cast<int32_t>(level->reachSpecs.size()));
                    for (const ReachSpec& spec : level->reachSpecs) {
                        data.i32(spec.distance);
                        out.writeObject(data, spec.start);
                        out.writeObject(data, spec.end);
                        data.i32(spec.collisionRadius);
                        data.i32(spec.collisionHeight);
                        data.i32(spec.reachFlags);
                        data.u8(spec.pruned);
                    }
                    data.f32(world.timeSeconds);
                    out.writeObject(data, nullptr);
                    for (UObject* block : level->textBlocks) {
                        out.writeObject(data, block);
                    }
                    data.index(static_cast<int32_t>(level->travelInfo.size()));
                    for (const auto& [key, value] : level->travelInfo) {
                        data.string(key);
                        data.string(value);
                    }
                    return;
                }
                if (UModel* model = cast<UModel>(object)) {
                    mapwriter::writeModel(out, *model, data);
                    return;
                }
                if (UPolys* polys = cast<UPolys>(object)) {
                    mapwriter::writePolys(out, *polys, data);
                    return;
                }
                if (classNamed(object->cls, { "ExtString" })) {
                    uint8_t* text = object->propertyAddress(Name("Text"));
                    data.string(text ? *reinterpret_cast<std::string*>(text) : std::string());
                }
            });

            return writer.save(file, error);
        }

        bool saveGame(World& world, int32_t index)
        {
            if (!world.level) {
                return false;
            }
            if (index != 10 && world.levelInfo) {
                levelinfo::LevelAction(world.levelInfo) = 2;
                if (console::paintProgressHook) {
                    console::paintProgressHook();
                }
            }
            world.cleanupDestroyed(true);
            std::string error;
            bool saved = saveLevel(world, slotFile(index), true, &error);
            if (world.levelInfo) {
                levelinfo::LevelAction(world.levelInfo) = 0;
            }
            if (!saved) {
                std::fprintf(stderr, "save failed: %s\n", error.c_str());
                return false;
            }
            return true;
        }

        bool deleteGame(int32_t index)
        {
            std::error_code error;
            return std::filesystem::remove(slotFile(index), error);
        }

        bool loadRequested(const std::string& url, int32_t& index)
        {
            std::string options = lower(url.substr(std::min(url.find('?'), url.size())));
            size_t at = options.find("?load=");
            if (at == std::string::npos) {
                return false;
            }
            index = std::atoi(options.c_str() + at + 6);
            return true;
        }

        bool prepareLoad(int32_t index)
        {
            std::filesystem::path file = slotFile(index);
            if (!std::filesystem::is_regular_file(file)) {
                return false;
            }
            state().pending = file;
            return true;
        }

        Linker* takePendingLevel()
        {
            if (state().pending.empty()) {
                return nullptr;
            }
            std::filesystem::path file = state().pending;
            state().pending.clear();
            try {
                return objects::loadPackageFile(Name("SaveGame_" + file.stem().string()), file, true);
            } catch (const std::exception& failure) {
                std::fprintf(stderr, "cannot open saved level %s: %s\n", file.string().c_str(), failure.what());
                return nullptr;
            }
        }

        void levelRestored(World& world)
        {
            if (world.level) {
                world.timeSeconds = world.level->timeSeconds;
            }
        }
    }
}
