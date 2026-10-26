#include "compiler/assets.h"
#include "compiler/exec_parse.h"
#include "compiler/exporter.h"
#include "compiler/script_compiler.h"
#include "compiler/script_package.h"
#include "core/config.h"
#include "core/objects.h"
#include "core/url.h"
#include "engine/net.h"
#include "engine/package.h"
#include "engine/resources.h"
#include "engine/world.h"

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {
    void usage()
    {
        std::printf("Usage:\n");
        std::printf("   ucc <command> <parameters>\n\n");
        std::printf("Commands for \"ucc\":\n");
        std::printf("   ucc batchexport <package.u> <class> <ext> <dir>    Export objects in bulk\n");
        std::printf("   ucc exportsources <package.u> <directory>          Export scripts and #exec source files\n");
        std::printf("   ucc make                                           Rebuild UnrealScript packages\n");
        std::printf("   ucc makepackage <name> <classes> <output.u>        Build one package from a classes directory\n");
        std::printf("   ucc server <map>[?game=<class>][?port=<port>]      Run a dedicated server\n");
    }

    std::string lower(std::string text)
    {
        for (char& c : text) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return text;
    }

    void loadFully(rune::Linker* linker)
    {
        for (size_t i = 0; i < linker->file().exports().size(); i++) {
            rune::objects::ensureLoaded(linker->object(static_cast<int32_t>(i + 1)));
        }
        rune::objects::flushLoads();
    }

    rune::Linker* loadForExport(const std::string& file)
    {
        std::string packageName = std::filesystem::path(file).stem().string();
        rune::Linker* linker = rune::objects::loadPackage(rune::Name(packageName));
        if (!linker) {
            std::fprintf(stderr, "cannot load package %s\n", packageName.c_str());
            return nullptr;
        }
        loadFully(linker);
        return linker;
    }

    int exportClasses(rune::Linker* linker, const std::filesystem::path& directory)
    {
        std::filesystem::create_directories(directory);
        const rune::Package& file = linker->file();
        int exported = 0;
        int skipped = 0;
        for (size_t i = 0; i < file.exports().size(); i++) {
            int32_t ref = static_cast<int32_t>(i + 1);
            if (file.classOf(ref) != "Class") {
                continue;
            }
            rune::UClass* cls = rune::cast<rune::UClass>(linker->object(ref));
            if (!cls) {
                continue;
            }
            if (!cls->scriptText) {
                std::printf("No script text for %s\n", cls->name.str().c_str());
                skipped++;
                continue;
            }
            rune::objects::ensureLoaded(cls->scriptText);
            std::ofstream out(directory / (cls->name.str() + ".uc"), std::ios::binary);
            out << rune::compiler::exportClass(cls);
            exported++;
        }
        std::printf("Exported %d classes, %d without script text\n", exported, skipped);
        return 0;
    }

    int batchExport(const std::vector<std::string>& args)
    {
        if (args.size() < 4) {
            std::fprintf(stderr, "usage: ucc batchexport <package> <classname> <extension> <directory>\n");
            return 1;
        }
        rune::Linker* linker = loadForExport(args[0]);
        if (!linker) {
            return 1;
        }
        std::filesystem::path directory(args[3]);
        if (lower(args[1]) == "class" && lower(args[2]) == "uc") {
            return exportClasses(linker, directory);
        }
        rune::UClass* cls = rune::objects::findClass(args[1]);
        if (!cls) {
            std::fprintf(stderr, "class %s not found\n", args[1].c_str());
            return 1;
        }
        std::filesystem::create_directories(directory);
        int exported = 0;
        for (size_t i = 0; i < linker->file().exports().size(); i++) {
            rune::UObject* object = linker->object(static_cast<int32_t>(i + 1));
            if (!object || !object->cls || !object->isA(cls)) {
                continue;
            }
            std::filesystem::path file = directory / (object->name.str() + "." + args[2]);
            if (rune::compiler::exportAssetObject(object, lower(args[2]), file)) {
                std::printf("Exported %s to %s\n", object->pathName().c_str(), file.string().c_str());
                exported++;
            } else {
                std::printf("Can't export %s to %s\n", object->pathName().c_str(), args[2].c_str());
            }
        }
        std::printf("Exported %d objects\n", exported);
        return 0;
    }

    int exportSources(const std::vector<std::string>& args)
    {
        if (args.size() < 2) {
            std::fprintf(stderr, "usage: ucc exportsources <package> <directory>\n");
            return 1;
        }
        rune::Linker* linker = loadForExport(args[0]);
        if (!linker) {
            return 1;
        }
        std::filesystem::path root = std::filesystem::path(args[1]) / linker->packageName.str();
        exportClasses(linker, root / "Classes");
        rune::compiler::ExecContext context;
        context.package = linker->packageObject;
        context.directory = root;
        context.log = [](const std::string& line) {
            std::printf("%s\n", line.c_str());
        };
        int written = 0;
        int failed = 0;
        for (size_t i = 0; i < linker->file().exports().size(); i++) {
            rune::UClass* cls = rune::cast<rune::UClass>(linker->object(static_cast<int32_t>(i + 1)));
            if (!cls || !cls->scriptText) {
                continue;
            }
            std::istringstream lines(cls->scriptText->text);
            std::string line;
            while (std::getline(lines, line)) {
                std::string_view text = line;
                while (!text.empty() && (text.back() == '\r' || text.back() == '\n')) {
                    text.remove_suffix(1);
                }
                if (!rune::compiler::parseCommand(text, "#exec")) {
                    continue;
                }
                std::string_view objText = text;
                if (rune::compiler::parseCommand(objText, "OBJ") && rune::compiler::parseCommand(objText, "LOAD")) {
                    continue;
                }
                if (rune::compiler::exportAssetSource(context, std::string(text))) {
                    written++;
                } else {
                    std::printf("Can't export source for #exec %s\n", std::string(text).c_str());
                    failed++;
                }
            }
        }
        std::printf("Exported sources for %d #exec directives, %d failed\n", written, failed);
        return failed == 0 ? 0 : 1;
    }

    std::vector<std::filesystem::path> classFiles(const std::filesystem::path& directory)
    {
        std::vector<std::filesystem::path> files;
        for (const auto& item : std::filesystem::directory_iterator(directory)) {
            if (item.is_regular_file() && lower(item.path().extension().string()) == ".uc") {
                files.push_back(item.path());
            }
        }
        std::sort(files.begin(), files.end(), [](const std::filesystem::path& a, const std::filesystem::path& b) {
            return lower(a.filename().string()) < lower(b.filename().string());
        });
        return files;
    }

    bool buildPackage(const std::filesystem::path& root, const std::string& name, const std::filesystem::path& sources, const std::filesystem::path& output)
    {
        std::printf("Building %s from %s\n", name.c_str(), sources.string().c_str());
        std::vector<std::filesystem::path> files = classFiles(sources);
        if (files.empty()) {
            std::printf("No files for package: %s\n", sources.string().c_str());
            return false;
        }
        rune::UObject* package = rune::objects::packageObject(rune::Name(name));
        rune::compiler::PackageCompiler compiler(package, [](const std::string& line) {
            std::printf("%s\n", line.c_str());
        });
        std::filesystem::path shipped = root / "System" / (name + ".u");
        if (std::filesystem::exists(shipped)) {
            rune::Package original(shipped);
            std::vector<rune::Name> natives;
            for (size_t i = 0; i < original.exports().size(); i++) {
                const rune::ExportEntry& entry = original.exports()[i];
                if (entry.classRef == 0 && (entry.flags & rune::RF_Native)) {
                    natives.emplace_back(original.nameAt(entry.name));
                }
            }
            compiler.setNativeClasses(std::move(natives));
        }
        for (const std::filesystem::path& file : files) {
            std::ifstream in(file, std::ios::binary);
            std::stringstream text;
            text << in.rdbuf();
            compiler.importClass(file, text.str());
        }
        if (!compiler.compile()) {
            std::printf("Failure - %d error(s)\n", compiler.errors());
            return false;
        }
        std::string error;
        if (!rune::compiler::saveScriptPackage(package, output, &error)) {
            std::printf("Failed to save %s: %s\n", output.string().c_str(), error.c_str());
            return false;
        }
        std::printf("Saved %s\n", output.string().c_str());
        for (rune::UObject* object : rune::compiler::packageContents(package)) {
            if (rune::UClass* cls = rune::cast<rune::UClass>(object)) {
                cls->defaultObject->destroyed = true;
            }
            object->destroyed = true;
        }
        rune::Linker* linker = rune::objects::loadPackageFile(rune::Name(name), output, false);
        if (!linker) {
            std::printf("Failed to reload %s\n", output.string().c_str());
            return false;
        }
        loadFully(linker);
        return true;
    }

    int make(const std::filesystem::path& root)
    {
        std::filesystem::path user = rune::config::userDirectory();
        std::vector<std::string> packages = rune::config::system().getAll("Editor.EditorEngine", "EditPackages");
        for (const std::string& name : packages) {
            std::printf("--------------------%s--------------------\n", name.c_str());
            std::filesystem::path output = user / "System" / (name + ".u");
            std::filesystem::path userSources = user / name / "Classes";
            std::filesystem::path gameSources = root / name / "Classes";
            if (std::filesystem::exists(output)) {
                rune::Linker* linker = rune::objects::loadPackage(rune::Name(name));
                if (!linker) {
                    std::printf("Failed to load %s\n", output.string().c_str());
                    return 1;
                }
                loadFully(linker);
                continue;
            }
            std::filesystem::path sources;
            if (std::filesystem::is_directory(userSources)) {
                sources = userSources;
            } else if (rune::Linker* linker = rune::objects::loadPackage(rune::Name(name))) {
                loadFully(linker);
                continue;
            } else if (std::filesystem::is_directory(gameSources)) {
                sources = gameSources;
            } else {
                std::printf("No files for package: %s\n", name.c_str());
                return 1;
            }
            if (!buildPackage(root, name, sources, output)) {
                return 1;
            }
        }
        std::printf("Success\n");
        return 0;
    }

    int makePackage(const std::filesystem::path& root, const std::vector<std::string>& args)
    {
        if (args.size() != 3) {
            usage();
            return 1;
        }
        for (const std::string& name : rune::config::system().getAll("Editor.EditorEngine", "EditPackages")) {
            if (rune::Linker* linker = rune::objects::loadPackage(rune::Name(name))) {
                loadFully(linker);
            }
        }
        std::filesystem::path output = args[2];
        std::filesystem::create_directories(output.parent_path());
        return buildPackage(root, args[0], args[1], output) ? 0 : 1;
    }
}

namespace {
    volatile std::sig_atomic_t stopRequested = 0;

    void requestStop(int)
    {
        stopRequested = 1;
    }

    int server(const std::filesystem::path& root, const std::vector<std::string>& args)
    {
        rune::engine::initialize(root.string());
        std::string url = args.empty() ? rune::config::system().get("URL", "LocalMap").value_or("DM-Hel") : args[0];
        std::string portText = rune::urlOption(url, "port");
        if (portText.empty()) {
            portText = rune::config::system().get("URL", "Port").value_or(std::to_string(rune::net::DefaultPort));
        }
        std::string error;
        std::unique_ptr<rune::net::Driver> driver = rune::net::Driver::listen(static_cast<uint16_t>(std::atoi(portText.c_str())), error);
        if (!driver) {
            std::fprintf(stderr, "%s\n", error.c_str());
            return 1;
        }
        int rate = std::atoi(rune::config::system().get("IpDrv.TcpNetDriver", "NetServerMaxTickRate").value_or("20").c_str());
        rate = std::clamp(rate, 10, 120);
        std::signal(SIGINT, requestStop);
        std::signal(SIGTERM, requestStop);
        std::unique_ptr<rune::World> world;
        while (!stopRequested) {
            if (world) {
                driver->attach(nullptr);
                if (world->level) {
                    rune::objects::unloadPackage(world->level->outermost()->name);
                }
                world.reset();
            }
            world = std::make_unique<rune::World>();
            world->netMode = rune::net::NM_DedicatedServer;
            world->netDriver = driver.get();
            if (!world->loadMap(url)) {
                std::fprintf(stderr, "cannot load %s\n", url.c_str());
                return 1;
            }
            driver->attach(world.get());
            world->spawnServerActors(rune::config::system().getAll("Engine.GameEngine", "ServerActors"));
            std::printf("Server running %s on port %u\n", url.c_str(), static_cast<unsigned>(driver->port()));
            std::fflush(stdout);
            auto step = std::chrono::duration<double>(1.0 / rate);
            auto next = std::chrono::steady_clock::now();
            while (!stopRequested && !world->travelPending) {
                world->tick(static_cast<float>(step.count()));
                next += std::chrono::duration_cast<std::chrono::steady_clock::duration>(step);
                std::this_thread::sleep_until(next);
            }
            if (world->travelPending) {
                url = rune::travelUrl(world->url, world->travelUrl, world->travelType);
            }
        }
        driver->attach(nullptr);
        world.reset();
        return 0;
    }
}

int main(int argc, char** argv)
{
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty()) {
        usage();
        return 0;
    }
    std::filesystem::path root = rune::config::findGameRoot(RUNE_GAME_DATA);
    if (lower(args[0]) == "server") {
        return server(root, std::vector<std::string>(args.begin() + 1, args.end()));
    }
    rune::objects::setEditorMode(true);
    rune::config::setApplyClassDefaults(false);
    std::string command = lower(args[0]);
    args.erase(args.begin());
    rune::engine::initializeCore(root.string());
    if (command == "batchexport") {
        return batchExport(args);
    }
    if (command == "exportsources") {
        return exportSources(args);
    }
    if (command == "make") {
        return make(root);
    }
    if (command == "makepackage") {
        return makePackage(root, args);
    }
    usage();
    return 1;
}
