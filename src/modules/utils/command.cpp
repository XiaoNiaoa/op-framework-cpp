#include "core/pointers.hpp"
#include "core/module.hpp"
#include "command.hpp"

std::unordered_map<std::string, OPUtilCommandHandler>& getCommandMap() 
{
    static std::unordered_map<std::string, OPUtilCommandHandler> map;
    return map;
}

class OPUtilCommandProcessorModule final
    : public OPCoreModuleManager
    , public PlayerTextEventHandler
{
    bool onModuleInit() override 
    {
        auto& players = OPCoreSharedPtr::core()->getPlayers();
        listen(players.getPlayerTextDispatcher(), *this, EventPriority_Highest);
        return true;
    }

    StringView moduleName() const override { return "OPUtil Command"; }

    bool onPlayerCommandText(IPlayer& player, StringView cmdtext) override 
    {
        if (cmdtext.empty() || cmdtext[0] != '/')
            return false;

        const char* p = cmdtext.data();
        const char* end = p + cmdtext.length();

        ++p;

        while (p < end && std::isspace(static_cast<unsigned char>(*p))) ++p;
        if (p == end) return false;

        const char* cmdStart = p;
        while (p < end && !std::isspace(static_cast<unsigned char>(*p))) ++p;

        std::string name(cmdStart, p - cmdStart);
        for (char& c : name)
            if (c >= 'A' && c <= 'Z') c += 32;

        while (p < end && std::isspace(static_cast<unsigned char>(*p))) ++p;

        std::string_view params(p, end - p);

        auto& map = getCommandMap();
        auto it = map.find(name);
        if (it == map.end())
            return false;

        return it->second(player, params);
    }
};

OPCORE_REGISTER_MODULE(OPUtilCommandProcessorModule);