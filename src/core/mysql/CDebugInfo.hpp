#pragma once

#include "sdk.hpp"
#include "CSingleton.hpp"

#include <amx/amxdbg.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct AmxFuncCallInfo
{
    int line = 0;
    std::string file;
    std::string function;
};

class CScopedDebugInfo;

class CDebugInfoManager : public CSingleton<CDebugInfoManager>
{
    friend class CSingleton<CDebugInfoManager>;
    friend class CScopedDebugInfo;
    struct DebugImage
    {
        AMX_DBG info{};
        ~DebugImage() { if (info.hdr) dbg_FreeInfo(&info); }
    };
    CDebugInfoManager() = default;
    ~CDebugInfoManager() = default;
    std::unordered_map<AMX*, std::shared_ptr<DebugImage>> m_Images;
    bool m_Available = false;
    AMX* m_Amx = nullptr;
    std::vector<AmxFuncCallInfo> m_Info;
    const char* m_NativeName = nullptr;
    void Update(AMX* amx, const char* func);
    void Clear();
public:
    void RegisterAmx(AMX* amx);
    void EraseAmx(AMX* amx);
    AMX* GetCurrentAmx() { return m_Amx; }
    const auto& GetCurrentInfo() { return m_Info; }
    bool IsInfoAvailable() { return m_Available; }
    const char* GetCurrentNativeName() { return m_NativeName; }
};

class CScopedDebugInfo
{
public:
    CScopedDebugInfo(AMX* amx, const char* func, 
        cell* params, const char* params_format = "");
    ~CScopedDebugInfo() 
    { 
        CDebugInfoManager::Get()->Clear(); 
    }
    CScopedDebugInfo(const CScopedDebugInfo&) = delete;
};