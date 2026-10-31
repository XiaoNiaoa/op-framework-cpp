#pragma once

#include <Server/Components/Pawn/pawn.hpp>
#include <string>

// Boundary adapters over OPFramework's open.mp AMX forwarding API.
// No SA-MP SDK structures, entry-table macros or plugin exports are used.
namespace OPCoreMySQL::Pawn
{
inline std::string GetString(AMX* amx, cell parameter)
{
    cell* address = nullptr;
    amx_GetAddr(amx, parameter, &address);
    int length = 0;
    amx_StrLen(address, &length);
    std::string result(length, ' ');
    amx_GetString(result.data(), address, 0, length + 1);
    return result;
}

inline int SetString(AMX* amx, cell parameter, const char* value, size_t capacity)
{
    cell* address = nullptr;
    const int error = amx_GetAddr(amx, parameter, &address);
    if (error != AMX_ERR_NONE) return error;
    return amx_SetString(address, value, 0, 0, capacity);
}

inline int SetString(AMX* amx, cell parameter, const std::string& value, size_t capacity)
{
    return SetString(amx, parameter, value.c_str(), capacity);
}

inline int PushReference(AMX* amx, cell* address)
{
    cell parameter;
    const int error = amx_MakeAddr(amx, address, &parameter);
    if (error != AMX_ERR_NONE) return error;
    return amx_Push(amx, parameter);
}
}

// Use the host's C++ string macro, retaining upstream's nullptr-for-empty
// contract (several natives distinguish missing/empty arguments this way).
#define MYSQL_STR_PARAM(amx, parameter, result) \
    do { \
        amx_StrParamChar(amx, parameter, result); \
        if ((result) && *(result) == '\0') (result) = nullptr; \
    } while (0)
