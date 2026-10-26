#include "core/objects.h"
#include "core/vm.h"
#include "engine/package.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <unordered_map>
#include <utility>
#include <vector>

namespace rune {
    namespace {
        using Variables = std::vector<std::pair<std::string, std::string>>;

        std::unordered_map<uint32_t, Variables>& requestVariables()
        {
            static std::unordered_map<uint32_t, Variables> table;
            return table;
        }

        std::unordered_map<uint32_t, Variables>& responseReplacements()
        {
            static std::unordered_map<uint32_t, Variables> table;
            return table;
        }

        std::filesystem::path includeFile(UObject* response, const std::string& name)
        {
            if (!name.empty() && (name[0] == '\\' || name[0] == '/' || name.find("..\\") != std::string::npos || name.find("../") != std::string::npos)) {
                std::fprintf(stderr, "Dangerous characters in filename: %s\n", name.c_str());
                return {};
            }
            uint8_t* address = response->propertyAddress(Name("IncludePath"));
            std::string includePath = address ? *reinterpret_cast<std::string*>(address) : std::string("../Web");
            if (includePath.empty()) {
                std::fprintf(stderr, "Bad IncludePath: %s\n", includePath.c_str());
                return {};
            }
            std::replace(includePath.begin(), includePath.end(), '\\', '/');
            std::filesystem::path directory(includePath);
            if (directory.is_relative()) {
                directory = objects::gameRoot() / "System" / directory;
            }
            return (directory / name).lexically_normal();
        }

        bool readFile(const std::filesystem::path& path, std::string& out)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file) {
                return false;
            }
            out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
            return true;
        }

        int base64Value(char c)
        {
            if (c >= 'A' && c <= 'Z') {
                return c - 'A';
            }
            if (c >= 'a' && c <= 'z') {
                return c - 'a' + 26;
            }
            if (c >= '0' && c <= '9') {
                return c - '0' + 52;
            }
            if (c == '+') {
                return 62;
            }
            return c == '/' ? 63 : -1;
        }

        void nativeDecodeBase64(UObject*, Frame& stack, void* result)
        {
            std::string encoded = vm::get<std::string>(stack);
            vm::finish(stack);
            std::string decoded;
            uint32_t bits = 0;
            int count = 0;
            for (char c : encoded) {
                int value = base64Value(c);
                if (value < 0) {
                    continue;
                }
                bits = bits << 6 | static_cast<uint32_t>(value);
                count += 6;
                if (count >= 8) {
                    count -= 8;
                    decoded += static_cast<char>((bits >> count) & 255);
                }
            }
            vm::ret<std::string>(result, decoded);
        }

        void nativeAddVariable(UObject* self, Frame& stack, void*)
        {
            std::string name = vm::get<std::string>(stack);
            std::string value = vm::get<std::string>(stack);
            vm::finish(stack);
            Variables& variables = requestVariables()[self->handle];
            variables.emplace(variables.begin(), name, value);
        }

        void nativeGetVariable(UObject* self, Frame& stack, void* result)
        {
            std::string name = vm::get<std::string>(stack);
            std::string fallback = vm::getOpt<std::string>(stack, std::string());
            vm::finish(stack);
            for (const auto& [key, value] : requestVariables()[self->handle]) {
                if (iequals(key, name)) {
                    vm::ret<std::string>(result, value);
                    return;
                }
            }
            vm::ret<std::string>(result, fallback);
        }

        void nativeGetVariableCount(UObject* self, Frame& stack, void* result)
        {
            std::string name = vm::get<std::string>(stack);
            vm::finish(stack);
            const Variables& variables = requestVariables()[self->handle];
            vm::ret<int32_t>(result, static_cast<int32_t>(std::count_if(variables.begin(), variables.end(), [&](const auto& item) {
                return iequals(item.first, name);
            })));
        }

        void nativeGetVariableNumber(UObject* self, Frame& stack, void* result)
        {
            std::string name = vm::get<std::string>(stack);
            int32_t number = vm::get<int32_t>(stack);
            std::string fallback = vm::getOpt<std::string>(stack, std::string());
            vm::finish(stack);
            for (const auto& [key, value] : requestVariables()[self->handle]) {
                if (iequals(key, name) && number-- == 0) {
                    vm::ret<std::string>(result, value);
                    return;
                }
            }
            vm::ret<std::string>(result, fallback);
        }

        void nativeSubst(UObject* self, Frame& stack, void*)
        {
            std::string name = vm::get<std::string>(stack);
            std::string value = vm::get<std::string>(stack);
            bool clear = vm::getBoolOpt(stack, false);
            vm::finish(stack);
            Variables& replacements = responseReplacements()[self->handle];
            if (clear) {
                replacements.clear();
            }
            for (auto& item : replacements) {
                if (iequals(item.first, name)) {
                    item.second = value;
                    return;
                }
            }
            replacements.emplace_back(name, value);
        }

        void nativeClearSubst(UObject* self, Frame& stack, void*)
        {
            vm::finish(stack);
            responseReplacements().erase(self->handle);
        }

        void sendText(UObject* response, const std::string& text)
        {
            static const Name nameSendText("SendText");
            if (text.empty()) {
                return;
            }
            vm::Call call(response, nameSendText);
            if (call.valid()) {
                call.s(text).b(true).run();
            }
        }

        void nativeIncludeUHTM(UObject* self, Frame& stack, void*)
        {
            std::string filename = vm::get<std::string>(stack);
            vm::finish(stack);
            std::string text;
            if (!readFile(includeFile(self, filename), text)) {
                std::fprintf(stderr, "IncludeUHTM: cannot open %s\n", filename.c_str());
                return;
            }
            const Variables& replacements = responseReplacements()[self->handle];
            std::string out;
            size_t position = 0;
            while (position < text.size()) {
                size_t mark = text.find('%', position);
                if (mark == std::string::npos) {
                    out += text.substr(position);
                    break;
                }
                out += text.substr(position, mark - position);
                size_t close = text.find('%', mark + 1);
                if (close == std::string::npos) {
                    out += text.substr(mark);
                    break;
                }
                if (close == mark + 1) {
                    out += '%';
                } else {
                    std::string name = text.substr(mark + 1, close - mark - 1);
                    for (const auto& [key, value] : replacements) {
                        if (iequals(key, name)) {
                            out += value;
                            break;
                        }
                    }
                }
                position = close + 1;
            }
            for (size_t i = 0; i < out.size(); i += 4096) {
                sendText(self, out.substr(i, 4096));
            }
        }

        void nativeIncludeBinaryFile(UObject* self, Frame& stack, void*)
        {
            static const Name nameSendBinary("SendBinary");
            std::string filename = vm::get<std::string>(stack);
            vm::finish(stack);
            std::string data;
            if (!readFile(includeFile(self, filename), data)) {
                std::fprintf(stderr, "IncludeBinaryFile: cannot open %s\n", filename.c_str());
                return;
            }
            for (size_t i = 0; i < data.size(); i += 255) {
                size_t count = std::min<size_t>(255, data.size() - i);
                vm::Call call(self, nameSendBinary);
                if (!call.valid()) {
                    return;
                }
                call.i(static_cast<int32_t>(count));
                std::memcpy(call.parameter(1), data.data() + i, count);
                call.run();
            }
        }
    }

    void registerUWebNatives()
    {
        vm::registerNative("WebRequest", "DecodeBase64", nativeDecodeBase64);
        vm::registerNative("WebRequest", "AddVariable", nativeAddVariable);
        vm::registerNative("WebRequest", "GetVariable", nativeGetVariable);
        vm::registerNative("WebRequest", "GetVariableCount", nativeGetVariableCount);
        vm::registerNative("WebRequest", "GetVariableNumber", nativeGetVariableNumber);
        vm::registerNative("WebResponse", "Subst", nativeSubst);
        vm::registerNative("WebResponse", "ClearSubst", nativeClearSubst);
        vm::registerNative("WebResponse", "IncludeUHTM", nativeIncludeUHTM);
        vm::registerNative("WebResponse", "IncludeBinaryFile", nativeIncludeBinaryFile);
    }
}
