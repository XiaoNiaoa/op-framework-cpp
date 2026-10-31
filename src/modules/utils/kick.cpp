#include "kick.hpp"

#include <chrono>
#include <string>

#include <Server/Components/Timers/Impl/timers_impl.hpp>

#include "core/pointers.hpp"
#include "../shared/colors.hpp"

namespace
{
using namespace std::chrono_literals;

constexpr auto kKickDelay = 500ms;

std::string addressOf(const IPlayer& player)
{
    PeerAddress::AddressString text;
    PeerAddress::ToString(player.getNetworkData().networkID.address, text);
    return std::string(text.data(), text.length());
}
}

namespace OPUtil
{
void kickWithMessage(IPlayer& player, StringView reason)
{
    player.sendClientMessage(OPShared::Color::kError, reason);

    auto* timers = OPCoreSharedPtr::timers();
    if (!timers)
    {
        player.kick();
        return;
    }

    const int playerId = player.getID();
    const unsigned short port = player.getNetworkData().networkID.port;
    timers->create(new Impl::SimpleTimerHandler([playerId, port, address = addressOf(player)]
    {
        IPlayer* current = OPCoreSharedPtr::core()->getPlayers().get(playerId);
        if (!current || current->getNetworkData().networkID.port != port || addressOf(*current) != address)
            return;
        current->kick();
    }), kKickDelay, false);
}
}
