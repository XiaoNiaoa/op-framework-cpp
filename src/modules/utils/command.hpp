#pragma once

#include <sdk.hpp>
#include <string>
#include <string_view>
#include <unordered_map>
#include <charconv>
#include <cctype>

namespace OPUtilCommandParams
{
    inline char toLowerAscii(char c) {
        return (c >= 'A' && c <= 'Z') ? (c + 32) : c;
    }

    inline std::string_view getArg(std::string_view params, int index = 0) {
        const char* p = params.data();
        const char* end = p + params.size();

        for (int i = 0; i <= index; ++i) {
            while (p < end && std::isspace(static_cast<unsigned char>(*p))) ++p;
            if (p == end) return {};

            const char* start = p;
            while (p < end && !std::isspace(static_cast<unsigned char>(*p))) ++p;

            if (i == index)
                return { start, static_cast<size_t>(p - start) };
        }
        return {};
    }

    inline int getInt(std::string_view params, int index = 0, int def = 0) {
        auto arg = getArg(params, index);
        if (arg.empty()) return def;
        int val = def;
        std::from_chars(arg.data(), arg.data() + arg.size(), val);
        return val;
    }

    inline float getFloat(std::string_view params, int index = 0, float def = 0.0f) {
        auto arg = getArg(params, index);
        if (arg.empty()) return def;
        float val = def;
        std::from_chars(arg.data(), arg.data() + arg.size(), val);
        return val;
    }

    inline std::string_view getRest(std::string_view params, int index = 0) {
        const char* p = params.data();
        const char* end = p + params.size();

        for (int i = 0; i < index; ++i) {
            while (p < end && std::isspace(static_cast<unsigned char>(*p))) ++p;
            if (p == end) return {};
            while (p < end && !std::isspace(static_cast<unsigned char>(*p))) ++p;
        }

        while (p < end && std::isspace(static_cast<unsigned char>(*p))) ++p;
        if (p == end) return {};
        return { p, static_cast<size_t>(end - p) };
    }

    inline int argCount(std::string_view params) {
        int count = 0;
        const char* p = params.data();
        const char* end = p + params.size();
        while (p < end) {
            while (p < end && std::isspace(static_cast<unsigned char>(*p))) ++p;
            if (p == end) break;
            ++count;
            while (p < end && !std::isspace(static_cast<unsigned char>(*p))) ++p;
        }
        return count;
    }
}

using OPUtilCommandHandler = bool(*)(IPlayer&, std::string_view);

std::unordered_map<std::string, OPUtilCommandHandler>& getCommandMap();

inline void registerCommand(const char* name, OPUtilCommandHandler handler) {
    std::string lowerName = name;
    for (char& c : lowerName) {
        if (c >= 'A' && c <= 'Z') c += 32;
    }
    getCommandMap()[lowerName] = handler;
}

#define OPUTIL_CMD(name) \
    static bool cmd_##name(IPlayer& player, std::string_view params); \
    static bool cmd_##name##_reg = (registerCommand(#name, cmd_##name), true); \
    static bool cmd_##name(IPlayer& player, std::string_view params)