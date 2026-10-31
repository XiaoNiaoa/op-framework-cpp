#include "dialog.hpp"

#include <array>
#include <utility>

#include "core/module.hpp"
#include "core/pointers.hpp"

namespace
{
constexpr int kDialogIdBase = 30000;
constexpr int kDialogIdRange = 1000;

struct PendingDialog
{
    int id = 0;
    OPUtil::Dialog::Callback callback;
};

class OPUtilDialogModule final
    : public OPCoreModuleManager
    , public PlayerDialogEventHandler
    , public PlayerConnectEventHandler
{
public:
    static OPUtilDialogModule* instance() { return instance_; }

    bool onModuleInit() override
    {
        auto* dialogs = OPCoreSharedPtr::dialogs();
        if (!dialogs)
            return true;

        listen(*dialogs, dialogs->getEventDispatcher(), *this);
        listen(OPCoreSharedPtr::core()->getPlayers().getPlayerConnectDispatcher(), *this);
        instance_ = this;
        return true;
    }

    bool onModuleExit() override
    {
        instance_ = nullptr;
        return true;
    }

    StringView moduleName() const override { return "OPUtil Dialog"; }

    void show(IPlayer& player, DialogStyle style, StringView title, StringView body, StringView button1, StringView button2, OPUtil::Dialog::Callback onResponse)
    {
        auto* data = queryExtension<IPlayerDialogData>(player);
        if (!data)
            return;

        PendingDialog& pending = pending_[player.getID()];
        pending.id = kDialogIdBase + nextId_;
        pending.callback = std::move(onResponse);
        nextId_ = (nextId_ + 1) % kDialogIdRange;

        data->show(player, pending.id, style, title, body, button1, button2);
    }

    void onDialogResponse(IPlayer& player, int dialogId, DialogResponse response, int listItem, StringView inputText) override
    {
        PendingDialog& pending = pending_[player.getID()];
        if (pending.id != dialogId || !pending.callback)
            return;

        OPUtil::Dialog::Callback callback = std::move(pending.callback);
        pending = {};

        OPUtil::Dialog::Response result;
        result.accepted = response == DialogResponse_Left;
        result.listItem = listItem;
        result.inputText = inputText;
        callback(player, result);
    }

    void onPlayerDisconnect(IPlayer& player, PeerDisconnectReason) override
    {
        pending_[player.getID()] = {};
    }

private:
    inline static OPUtilDialogModule* instance_ = nullptr;

    std::array<PendingDialog, PLAYER_POOL_SIZE> pending_;
    int nextId_ = 0;
};

OPCORE_REGISTER_MODULE(OPUtilDialogModule);
}

namespace OPUtil::Dialog
{
void show(IPlayer& player, DialogStyle style, StringView title, StringView body, StringView button1, StringView button2, Callback onResponse)
{
    if (auto* module = OPUtilDialogModule::instance())
        module->show(player, style, title, body, button1, button2, std::move(onResponse));
}
}

std::string OPUtil::Dialog::withNotice(std::string_view notice, std::string body)
{
    return notice.empty() ? body : fmt::format("{{E74C3C}}{}{{FFFFFF}}\n\n{}", notice, body);
}
