#pragma once

// Host boundary: keeps the database MySQLLogLevel separate from open.mp's enum.
#include <amx/amx.h>

#include "types.hpp"

struct IEarlyConfig;

namespace OPCoreMySQL
{
// Framework-owned default connection handle. Zero means unavailable.
extern HandleId_t defaultHandleId;
}

namespace OPCoreMySQL
{
using LogFunction = void (*)(const char *, ...);
bool Load();
void Unload();
void ProcessTick();
int AmxLoad(AMX *amx);
int AmxUnload(AMX *amx);
void DetachScripts();

// Fill missing MySQL configuration defaults during open.mp configuration setup.
void SetDefaultConfig(IEarlyConfig& config);

void disconnectDefaultDatabase();
}
