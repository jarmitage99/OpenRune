#include "core/url.h"

#include "engine/package.h"

#include <vector>

namespace rune {
    namespace {
        std::vector<std::string> urlOptions(const std::string& url)
        {
            std::vector<std::string> options;
            size_t mark = url.find('?');
            while (mark != std::string::npos) {
                size_t next = url.find('?', mark + 1);
                std::string option = url.substr(mark + 1, next == std::string::npos ? std::string::npos : next - mark - 1);
                if (!option.empty()) {
                    options.push_back(option);
                }
                mark = next;
            }
            return options;
        }

        void addOption(std::vector<std::string>& options, const std::string& option)
        {
            size_t equals = option.find('=');
            std::string key = equals == std::string::npos ? option : option.substr(0, equals + 1);
            for (std::string& existing : options) {
                bool match = equals == std::string::npos ? iequals(existing, key) : existing.size() >= key.size() && iequals(existing.substr(0, key.size()), key);
                if (match) {
                    existing = option;
                    return;
                }
            }
            options.push_back(option);
        }
    }

    std::string urlOption(const std::string& url, const std::string& key)
    {
        for (const std::string& option : urlOptions(url)) {
            size_t equals = option.find('=');
            if (equals != std::string::npos && iequals(option.substr(0, equals), key)) {
                return option.substr(equals + 1);
            }
        }
        return {};
    }

    std::string travelUrl(const std::string& base, const std::string& text, int32_t travelType)
    {
        std::vector<std::string> options;
        if (travelType == TRAVEL_Relative || travelType == TRAVEL_Partial) {
            for (const std::string& option : urlOptions(base)) {
                if (!iequals(option, "PUSH") && !iequals(option, "POP") && !iequals(option, "PEER") && !iequals(option, "LOAD") && !iequals(option.substr(0, option.find('=')), "VIDEO")) {
                    options.push_back(option);
                }
            }
        }
        std::string map = text.substr(0, text.find('?'));
        if (map.empty() || map[0] == '#') {
            std::string baseMap = base.substr(0, base.find('?'));
            map = baseMap.substr(0, baseMap.find_first_of("#/")) + map;
        }
        for (const std::string& option : urlOptions(text)) {
            addOption(options, option);
        }
        std::string result = map;
        for (const std::string& option : options) {
            result += "?" + option;
        }
        return result;
    }
}
