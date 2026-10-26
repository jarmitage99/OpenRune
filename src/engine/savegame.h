#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace rune {
    class World;
    class Linker;
    class UObject;

    namespace savegame {
        void setSaveRoot(const std::filesystem::path& root);
        std::filesystem::path saveRoot();
        std::filesystem::path slotFile(int32_t index);
        std::string mapBaseName(const std::string& url);

        bool saveLevel(World& world, const std::filesystem::path& file, bool includePlayer, std::string* error);
        bool saveGame(World& world, int32_t index);
        bool deleteGame(int32_t index);
        bool loadRequested(const std::string& url, int32_t& index);
        bool prepareLoad(int32_t index);
        Linker* takePendingLevel();
        void levelRestored(World& world);
    }
}
