#include "CDebugInfo.hpp"
#include "core/log/log.hpp"

#include <fmt/format.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>

void CDebugInfoManager::RegisterAmx(AMX* amx)
{
    if (!amx || !amx->base || m_Images.count(amx)) return;
    // IPawnScript exposes the AMX image but no filename/debug metadata.
    // Match the image against server script files, including config.json setups.
    for (const char* directory : { "gamemodes", "filterscripts" })
    {
        std::error_code ec;
        std::filesystem::recursive_directory_iterator it(directory,
            std::filesystem::directory_options::skip_permission_denied, ec), end;
        while (!ec && it != end)
        {
            const auto path = it->path();
            if (it->is_regular_file(ec) && path.extension() == ".amx")
            {
                FILE* file = std::fopen(path.string().c_str(), "rb");
                if (file)
                {
                    AMX_HEADER header{};
                    const bool matches =
                        std::fread(&header, sizeof(header), 1, file) == 1 &&
                        std::memcmp(&header, amx->base, sizeof(header)) == 0;
                    if (matches)
                    {
                        auto image = std::make_shared<DebugImage>();
                        const int error = dbg_LoadInfo(&image->info, file);
                        std::fclose(file);
                        if (error == AMX_ERR_NONE)
                        {
                            m_Images.emplace(amx, std::move(image));
                            return;
                        }
                    }
                    else std::fclose(file);
                }
            }
            it.increment(ec);
        }
    }
}

void CDebugInfoManager::EraseAmx(AMX* amx)
{
    m_Images.erase(amx);
    if (m_Amx == amx) Clear();
}

void CDebugInfoManager::Update(AMX* amx, const char* func)
{
    m_Amx = amx;
    m_NativeName = func;
    m_Info.clear();
    m_Available = false;
    auto it = m_Images.find(amx);
    if (it == m_Images.end()) return;
    auto* debug = &it->second->info;
    auto append = [&](ucell address)
    {
        const char* file = nullptr;
        const char* function = nullptr;
        if (dbg_LookupFile(debug, address, &file) != AMX_ERR_NONE ||
            dbg_LookupFunction(debug, address, &function) != AMX_ERR_NONE) return false;
        // The current Pawn loader recovers overflowing 16-bit line counts,
        // but dbg_LookupLine still reads the truncated header count. Retain
        // the database logger's full-table lookup over the host-loaded data.
        if (!debug->symboltbl || !debug->symboltbl[0]) return false;
        const auto lineCount = (reinterpret_cast<const unsigned char*>(debug->symboltbl[0]) -
            reinterpret_cast<const unsigned char*>(debug->linetbl)) / sizeof(AMX_DBG_LINE);
        size_t index = 0;
        while (index < lineCount && debug->linetbl[index].address <= address) ++index;
        if (index == 0 || index >= lineCount) return false;
        const auto line = debug->linetbl[index - 1].line;
        // Own the strings: queued queries can outlive the originating script.
        m_Info.push_back({ static_cast<int>(line + 1), file, function });
        return true;
    };
    if (!append(static_cast<ucell>(amx->cip))) return;
    const auto* header = reinterpret_cast<const AMX_HEADER*>(amx->base);
    const auto* data = amx->data ? amx->data : amx->base + header->dat;
    cell frame = amx->frm;
    while (frame >= 0 && amx->stp >= static_cast<cell>(2 * sizeof(cell)) &&
           frame <= amx->stp - static_cast<cell>(2 * sizeof(cell)))
    {
        std::array<cell, 2> entries{};
        std::memcpy(entries.data(), data + frame, sizeof(entries));
        if (!entries[1]) break;
        if (!append(static_cast<ucell>(entries[1])))
            m_Info.push_back({ 0, "<unknown>", "<unknown>" });
        // Caller frames grow upwards; also reject corrupt/cyclic frame chains.
        if (entries[0] <= frame) break;
        frame = entries[0];
    }
    // Preserve the original logger's outermost caller line adjustment.
    if (m_Info.size() > 1) --m_Info.back().line;
    m_Available = !m_Info.empty();
}

void CDebugInfoManager::Clear()
{
    m_Amx = nullptr;
    m_NativeName = nullptr;
    m_Info.clear();
    m_Available = false;
}

CScopedDebugInfo::CScopedDebugInfo(AMX* amx, const char* func, cell* params, const char* params_format)
{
    CDebugInfoManager::Get()->Update(amx, func);
    if (!amx || !params) return;
    const auto logger = OPCoreLog::get("mysql");
    if (!logger->should_log(spdlog::level::debug))
        return;

    std::string text = std::string(func) + '(';
    const size_t count = static_cast<size_t>(params[0]) / sizeof(cell);
    for (size_t i = 0; params_format[i] != '\0' && i < count; ++i)
    {
        if (i) text += ", ";
        cell value = params[i + 1];
        switch (params_format[i])
        {
            case '*': text += "\"*****\""; break;
            case 's': text += fmt::format("\"{}\"", OPCoreMySQL::Pawn::GetString(amx, value)); break;
            case 'f': text += fmt::format("{:f}", amx_ctof(value)); break;
            case 'h': case 'x': text += fmt::format("{:x}", value); break;
            case 'b': text += fmt::format("{:b}", value); break;
            case 'p': text += fmt::format("{:#08x}", static_cast<ucell>(value)); break;
            case 'r':
            {
                cell* address = nullptr;
                amx_GetAddr(amx, value, &address);
                text += fmt::format("{:#08x}", reinterpret_cast<uintptr_t>(address));
                break;
            }
            case 'd': case 'i': text += fmt::format("{}", value); break;
            default: return;
        }
    }
    text += ')';

    const auto& callinfo = CDebugInfoManager::Get()->GetCurrentInfo();

    if (!callinfo.empty())
    {
        text += " (";
        for (size_t i = 0; i < callinfo.size(); ++i)
        {
            if (i) text += " -> ";
            const auto& info = callinfo[i];
            text += fmt::format("{}:{} ({})", info.file, info.line, info.function);
        }
        text += ')';
    }
    OPCORE_LOGGER_DEBUG(logger, "{}", text);
}
