// 登录流程：显示登录/注册界面、处理输入和登录超时。
// 账号检查、密码验证、注册和最终登录由 account.cpp 负责。

#include "account.hpp"

#include <fmt/format.h>
#include <Impl/events_impl.hpp>
#include <sdk.hpp>

#include <chrono>
#include <string>
#include <string_view>
#include <utility>

#include "core/log/log.hpp"
#include "core/module.hpp"
#include "core/pointers.hpp"
#include "../utils/dialog.hpp"
#include "../utils/kick.hpp"

namespace
{
constexpr int kMaximumPasswordAttempts = 3;
constexpr auto kLoginTimeout = std::chrono::seconds(60);
constexpr int kPlayersCheckedPerTick = 8;

constexpr const char* kLoginDialogTitle = "账户登录";
constexpr const char* kRegistrationDialogTitle = "创建账户";
constexpr const char* kLeaveButtonText = "离开";
constexpr const char* kLoginConfirmButtonText = "登录";
constexpr const char* kRegistrationConfirmButtonText = "创建";
constexpr const char* kPasswordRuleMessage = "密码需为 6 至 32 位可见字符。";
constexpr const char* kServiceUnavailableMessage = "服务暂时不可用，请稍后重试。";
constexpr const char* kAccountAlreadyOnlineMessage = "该账户当前已在线，连接已断开。";
constexpr const char* kLoginCancelledMessage = "你已取消登录，期待下次再会。";
constexpr const char* kRegistrationCancelledMessage = "你已取消创建账户，期待下次再会。";
constexpr const char* kTooManyPasswordAttemptsMessage = "密码错误次数过多，连接已断开。";
constexpr const char* kLoginTimeoutMessage = "长时间未登录，连接已断开。";
constexpr const char* kNicknameTakenMessage = "该昵称刚刚已被他人注册，请更换昵称后重新连接。";

class AccountLoginState final : public IExtension
{
public:
    PROVIDE_EXT_UID(0x4F504163634C6F67);

    static AccountLoginState* of(IPlayer& player)
    {
        return queryExtension<AccountLoginState>(player);
    }

    explicit AccountLoginState(TimePoint connectedAt)
        : connectedAt(connectedAt)
    {
    }

    void freeExtension() override { delete this; }
    void reset() override { }

    const TimePoint connectedAt;
    int failedPasswordAttempts = 0;
};

AccountLoginState* getLoginState(IPlayer& player)
{
    AccountLoginState* state = AccountLoginState::of(player);
    return state && !OPAccount::isLoggedIn(player) ? state : nullptr;
}

void showLoginDialog(IPlayer& player, std::string_view notice);
void showRegistrationDialog(IPlayer& player, std::string_view notice);

void handleAuthenticationResult(IPlayer& player, OPAccount::AuthenticationResult result, bool registering)
{
    if (!AccountLoginState::of(player)
        || (result != OPAccount::AuthenticationResult::Success && OPAccount::isLoggedIn(player)))
        return;

    switch (result)
    {
    case OPAccount::AuthenticationResult::Success:
    case OPAccount::AuthenticationResult::InProgress:
        return;

    case OPAccount::AuthenticationResult::InvalidPassword:
        if (registering)
            showRegistrationDialog(player, kPasswordRuleMessage);
        else
            showLoginDialog(player, kPasswordRuleMessage);
        return;

    case OPAccount::AuthenticationResult::IncorrectPassword:
        if (AccountLoginState* state = getLoginState(player))
        {
            ++state->failedPasswordAttempts;
            if (state->failedPasswordAttempts >= kMaximumPasswordAttempts)
            {
                OPUtil::kickWithMessage(player, kTooManyPasswordAttemptsMessage);
                return;
            }

            showLoginDialog(player, fmt::format(
                "密码不正确，你还有 {} 次机会。",
                kMaximumPasswordAttempts - state->failedPasswordAttempts));
        }
        return;

    case OPAccount::AuthenticationResult::NameAlreadyTaken:
        OPUtil::kickWithMessage(player, kNicknameTakenMessage);
        return;

    case OPAccount::AuthenticationResult::AlreadyOnline:
        OPUtil::kickWithMessage(player, kAccountAlreadyOnlineMessage);
        return;

    case OPAccount::AuthenticationResult::Failed:
        OPUtil::kickWithMessage(player, kServiceUnavailableMessage);
        return;
    }
}

void handlePasswordDialogResponse(
    IPlayer& player,
    const OPUtil::Dialog::Response& response,
    bool registering)
{
    if (!getLoginState(player))
        return;

    if (!response.accepted)
    {
        OPUtil::kickWithMessage(
            player,
            registering ? kRegistrationCancelledMessage : kLoginCancelledMessage);
        return;
    }

    // 使用动态缓冲区，避免短密码留在 std::string 的小字符串缓冲区中。
    std::string password;
    password.reserve(OPAccount::PasswordRules::MaximumLength + 1);
    if (response.inputText.length() != 0)
        password.assign(response.inputText.data(), response.inputText.length());

    auto onFinished = [registering](IPlayer& player, OPAccount::AuthenticationResult result)
    {
        handleAuthenticationResult(player, result, registering);
    };

    if (registering)
        OPAccount::registerAccount(player, std::move(password), std::move(onFinished));
    else
        OPAccount::login(player, std::move(password), std::move(onFinished));
}

void showPasswordPrompt(
    IPlayer& player,
    std::string_view title,
    std::string body,
    std::string_view confirmText,
    OPUtil::Dialog::Callback onResponse)
{
    OPUtil::Dialog::show(
        player,
        DialogStyle_PASSWORD,
        StringView(title.data(), title.size()),
        StringView(body.data(), body.size()),
        StringView(confirmText.data(), confirmText.size()),
        kLeaveButtonText,
        std::move(onResponse));
}

void showLoginDialog(IPlayer& player, std::string_view notice)
{
    const StringView playerName = player.getName();
    showPasswordPrompt(
        player,
        kLoginDialogTitle,
        OPUtil::Dialog::withNotice(notice, fmt::format(
            "欢迎回来，{}。\n\n请输入账户密码以继续。",
            std::string(playerName.data(), playerName.length()))),
        kLoginConfirmButtonText,
        [](IPlayer& player, const OPUtil::Dialog::Response& response)
        {
            handlePasswordDialogResponse(player, response, false);
        });
}

void showRegistrationDialog(IPlayer& player, std::string_view notice)
{
    const StringView playerName = player.getName();
    showPasswordPrompt(
        player,
        kRegistrationDialogTitle,
        OPUtil::Dialog::withNotice(notice, fmt::format(
            "你好，{}。\n\n这个昵称尚未注册，请为你的账户设置一个密码。\n"
            "{{95A5A6}}密码需为 {} 至 {} 位可见字符，请妥善保管。",
            std::string(playerName.data(), playerName.length()),
            OPAccount::PasswordRules::MinimumLength,
            OPAccount::PasswordRules::MaximumLength)),
        kRegistrationConfirmButtonText,
        [](IPlayer& player, const OPUtil::Dialog::Response& response)
        {
            handlePasswordDialogResponse(player, response, true);
        });
}

class OPAccountLoginModule final
    : public OPCoreModuleManager
    , public PlayerConnectEventHandler
    , public CoreEventHandler
{
public:
    bool onModuleInit() override
    {
        ICore* core = OPCoreSharedPtr::core();
        if (!core)
            return false;

        listen(core->getPlayers().getPlayerConnectDispatcher(), *this);
        listen(core->getEventDispatcher(), *this);
        return true;
    }

    StringView moduleName() const override { return "OPAccountLogin"; }

    void onPlayerConnect(IPlayer& player) override
    {
        if (player.isBot())
            return;

        auto* state = new AccountLoginState(std::chrono::steady_clock::now());
        if (!player.addExtension(state, true))
        {
            delete state;
            const StringView playerName = player.getName();
            OPCORE_LOG_ERROR(
                "[OPAccount] 无法为玩家 {} 创建登录状态",
                std::string(playerName.data(), playerName.length()));
            OPUtil::kickWithMessage(player, kServiceUnavailableMessage);
            return;
        }

        // 登录前锁定视角，避免未认证玩家活动。
        player.setSpectating(true);

        OPAccount::checkAccountExists(
            player,
            [](IPlayer& player, OPAccount::AccountCheckResult result)
            {
                switch (result)
                {
                case OPAccount::AccountCheckResult::Registered:
                    showLoginDialog(player, {});
                    return;

                case OPAccount::AccountCheckResult::NotRegistered:
                    showRegistrationDialog(player, {});
                    return;

                case OPAccount::AccountCheckResult::Failed:
                    OPUtil::kickWithMessage(player, kServiceUnavailableMessage);
                    return;
                }
            });
    }

    void onTick(Microseconds, TimePoint now) override
    {
        ICore* core = OPCoreSharedPtr::core();
        if (!core)
            return;

        auto& players = core->getPlayers();
        for (int i = 0; i < kPlayersCheckedPerTick; ++i)
        {
            IPlayer* player = players.get(cursor_);
            cursor_ = (cursor_ + 1) % PLAYER_POOL_SIZE;
            if (!player)
                continue;

            AccountLoginState* state = getLoginState(*player);
            if (state && now - state->connectedAt >= kLoginTimeout)
                OPUtil::kickWithMessage(*player, kLoginTimeoutMessage);
        }
    }

private:
    int cursor_ = 0;
};

OPCORE_REGISTER_MODULE(OPAccountLoginModule);
}
