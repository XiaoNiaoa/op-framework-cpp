// 账号业务：检查昵称、登录、注册和登出。
// 登录界面及玩家连接后的交互由 account_login.cpp 负责。

#include "account.hpp"

#include <ankerl/unordered_dense.h>
#include <fmt/format.h>
#include <Impl/events_impl.hpp>
#include <sdk.hpp>

#include <charconv>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "core/hash/hash.hpp"
#include "core/log/log.hpp"
#include "core/module.hpp"
#include "core/mysql.hpp"
#include "core/pointers.hpp"

namespace
{
struct PlayerAccount final : IExtension
{
    PROVIDE_EXT_UID(0x4F504163636F756E);

    PlayerAccount(std::uint64_t connectSequence, std::string playerName)
        : connectSequence(connectSequence)
        , name(std::move(playerName))
    {
    }

    static PlayerAccount* of(IPlayer& player)
    {
        return queryExtension<PlayerAccount>(player);
    }

    void freeExtension() override { delete this; }

    // GMX 时保留当前玩家的账号状态。
    void reset() override { }

    const std::uint64_t connectSequence;
    const std::string name;
    std::uint32_t accountId = 0;
    std::string passwordHash;
    bool accountCheckPending = true;
    bool loggedIn = false;
    bool authenticationPending = false;
};

Impl::DefaultEventDispatcher<OPAccount::EventHandler> g_eventDispatcher;
ankerl::unordered_dense::set<std::uint32_t> g_onlineAccountIds;
std::uint64_t g_nextConnectSequence = 0;

constexpr unsigned int kDuplicateEntryError = 1062;

// 玩家 ID 可能在断线后复用，异步结果必须确认仍是发起请求的那次连接。
template <typename Callback>
auto forPlayerConnection(const IPlayer& player, const PlayerAccount& account, Callback callback)
{
    const auto playerId = player.getID();
    const auto connectSequence = account.connectSequence;

    return [playerId, connectSequence, callback = std::move(callback)](auto&&... args) mutable
    {
        ICore* core = OPCoreSharedPtr::core();
        IPlayer* currentPlayer = core ? core->getPlayers().get(playerId) : nullptr;
        PlayerAccount* currentAccount = currentPlayer ? PlayerAccount::of(*currentPlayer) : nullptr;
        if (!currentAccount || currentAccount->connectSequence != connectSequence)
            return;

        callback(*currentPlayer, *currentAccount, std::forward<decltype(args)>(args)...);
    };
}

bool isValidPassword(std::string_view password)
{
    if (password.size() < OPAccount::PasswordRules::MinimumLength
        || password.size() > OPAccount::PasswordRules::MaximumLength)
        return false;

    for (const unsigned char c : password)
    {
        if (c <= 0x20 || c >= 0x7F)
            return false;
    }
    return true;
}

// 密码验证成功或账号创建成功后，设置登录状态并通知其他模块。
OPAccount::AuthenticationResult logInPlayer(IPlayer& player, PlayerAccount& account)
{
    if (account.accountId == 0 || account.loggedIn)
        return OPAccount::AuthenticationResult::Failed;

    if (!g_onlineAccountIds.insert(account.accountId).second)
        return OPAccount::AuthenticationResult::AlreadyOnline;

    account.loggedIn = true;
    account.passwordHash.clear();

    PeerAddress::AddressString address;
    PeerAddress::ToString(player.getNetworkData().networkID.address, address);
    const std::string ipAddress(address.data(), address.length());
    std::string escapedIp;
    if (!OPCoreMySQL::escapeString(ipAddress, escapedIp)
        || !OPCoreMySQL::queryAsync(fmt::format(
            "UPDATE `accounts` SET `last_login_at` = UTC_TIMESTAMP(), `last_ip` = '{}' "
            "WHERE `id` = {} LIMIT 1",
            escapedIp, account.accountId)))
    {
        OPCORE_LOG_WARN(
            "[OPAccount] 账号 {} 已登录，但登录记录未能提交",
            account.accountId);
    }

    OPCORE_LOG_INFO("[OPAccount] {} (id {}) 登录", account.name, account.accountId);
    g_eventDispatcher.dispatch(&OPAccount::EventHandler::onAccountLogin, player);
    return OPAccount::AuthenticationResult::Success;
}
}

namespace OPAccount
{
void checkAccountExists(IPlayer& player, AccountCheckCallback onFinished)
{
    if (!onFinished)
        return;

    if (PlayerAccount::of(player))
    {
        onFinished(player, AccountCheckResult::Failed);
        return;
    }

    const StringView playerName = player.getName();
    auto* account = new PlayerAccount(
        ++g_nextConnectSequence,
        std::string(playerName.data(), playerName.length()));

    if (!player.addExtension(account, true))
    {
        delete account;
        onFinished(player, AccountCheckResult::Failed);
        return;
    }

    std::string escapedName;
    const bool queued =
        OPCoreMySQL::escapeString(account->name, escapedName)
        && OPCoreMySQL::queryAsync(
            fmt::format(
                "SELECT `id`, `password_hash` FROM `accounts` "
                "WHERE `name` = '{}' AND `deleted_at` IS NULL LIMIT 1",
                escapedName),
            forPlayerConnection(player, *account,
                [onFinished](IPlayer& player, PlayerAccount& account, OPCoreMySQL::QueryResult result)
            {
                if (result.rows.empty())
                {
                    account.accountCheckPending = false;
                    onFinished(player, AccountCheckResult::NotRegistered);
                    return;
                }

                const auto& row = result.rows.front();
                std::uint32_t accountId = 0;
                if (row.size() < 2 || !row[0] || row[0]->empty() || !row[1] || row[1]->empty())
                {
                    account.accountCheckPending = false;
                    OPCORE_LOG_ERROR(
                        "[OPAccount] 读取玩家 {} 的账号信息失败：数据库返回的数据不完整",
                        account.name);
                    onFinished(player, AccountCheckResult::Failed);
                    return;
                }

                const char* begin = row[0]->data();
                const char* end = begin + row[0]->size();
                const auto parsedId = std::from_chars(begin, end, accountId);
                if (parsedId.ec != std::errc{} || parsedId.ptr != end || accountId == 0)
                {
                    account.accountCheckPending = false;
                    OPCORE_LOG_ERROR(
                        "[OPAccount] 读取玩家 {} 的账号信息失败：账号 ID 无效",
                        account.name);
                    onFinished(player, AccountCheckResult::Failed);
                    return;
                }

                account.accountId = accountId;
                account.passwordHash = *row[1];
                account.accountCheckPending = false;
                onFinished(player, AccountCheckResult::Registered);
            }),
            forPlayerConnection(player, *account,
                [onFinished](
                    IPlayer& player,
                    PlayerAccount& account,
                    unsigned int code,
                    const std::string& message)
            {
                account.accountCheckPending = false;
                OPCORE_LOG_ERROR(
                    "[OPAccount] 检查玩家 {} 的账号失败：[{}] {}",
                    account.name, code, message);
                onFinished(player, AccountCheckResult::Failed);
            }));

    // escapeString 或 queryAsync 返回 false 时，都不会触发异步回调。
    if (!queued)
    {
        account->accountCheckPending = false;
        OPCORE_LOG_ERROR(
            "[OPAccount] 检查玩家 {} 的账号失败：昵称转义失败或数据库请求未能进入执行队列",
            account->name);
        onFinished(player, AccountCheckResult::Failed);
    }
}

void login(IPlayer& player, std::string password, AuthenticationCallback onFinished)
{
    if (!onFinished)
    {
        OPCoreHash::wipe(password);
        return;
    }

    PlayerAccount* account = PlayerAccount::of(player);
    if (!account || account->loggedIn)
    {
        OPCoreHash::wipe(password);
        onFinished(player, AuthenticationResult::Failed);
        return;
    }
    if (account->accountCheckPending || account->authenticationPending)
    {
        OPCoreHash::wipe(password);
        onFinished(player, AuthenticationResult::InProgress);
        return;
    }
    if (account->accountId == 0 || account->passwordHash.empty())
    {
        OPCoreHash::wipe(password);
        onFinished(player, AuthenticationResult::Failed);
        return;
    }
    if (!isValidPassword(password))
    {
        OPCoreHash::wipe(password);
        onFinished(player, AuthenticationResult::InvalidPassword);
        return;
    }

    account->authenticationPending = true;
    const bool queued = OPCoreHash::verifyAsync(
        std::move(password), account->passwordHash,
        forPlayerConnection(player, *account,
            [onFinished](
                IPlayer& player,
                PlayerAccount& account,
                OPCoreHash::VerifyResult result)
        {
            if (!result.matched)
            {
                account.authenticationPending = false;
                onFinished(player, AuthenticationResult::IncorrectPassword);
                return;
            }

            if (!result.rehash.empty())
            {
                std::string escapedHash;
                if (!OPCoreMySQL::escapeString(result.rehash, escapedHash)
                    || !OPCoreMySQL::queryAsync(fmt::format(
                        "UPDATE `accounts` SET `password_hash` = '{}' WHERE `id` = {} LIMIT 1",
                        escapedHash, account.accountId)))
                {
                    OPCORE_LOG_WARN(
                        "[OPAccount] 账号 {} 的密码哈希升级未能提交",
                        account.accountId);
                }
            }

            const AuthenticationResult loginResult = logInPlayer(player, account);
            account.authenticationPending = false;
            onFinished(player, loginResult);
        }));

    if (!queued)
    {
        OPCORE_LOG_ERROR("[OPAccount] {} 的密码验证任务未能提交", account->name);
        account->authenticationPending = false;
        onFinished(player, AuthenticationResult::Failed);
    }
}

void registerAccount(IPlayer& player, std::string password, AuthenticationCallback onFinished)
{
    if (!onFinished)
    {
        OPCoreHash::wipe(password);
        return;
    }

    PlayerAccount* account = PlayerAccount::of(player);
    if (!account || account->loggedIn)
    {
        OPCoreHash::wipe(password);
        onFinished(player, AuthenticationResult::Failed);
        return;
    }
    if (account->accountCheckPending || account->authenticationPending)
    {
        OPCoreHash::wipe(password);
        onFinished(player, AuthenticationResult::InProgress);
        return;
    }
    if (account->accountId != 0)
    {
        OPCoreHash::wipe(password);
        onFinished(player, AuthenticationResult::NameAlreadyTaken);
        return;
    }
    if (!isValidPassword(password))
    {
        OPCoreHash::wipe(password);
        onFinished(player, AuthenticationResult::InvalidPassword);
        return;
    }

    account->authenticationPending = true;
    const bool hashQueued = OPCoreHash::hashAsync(
        std::move(password),
        forPlayerConnection(player, *account,
            [onFinished](IPlayer& player, PlayerAccount& account, std::string passwordHash)
        {
            if (passwordHash.empty())
            {
                OPCORE_LOG_ERROR("[OPAccount] {} 的密码哈希生成失败", account.name);
                account.authenticationPending = false;
                onFinished(player, AuthenticationResult::Failed);
                return;
            }

            PeerAddress::AddressString address;
            PeerAddress::ToString(player.getNetworkData().networkID.address, address);
            const std::string ipAddress(address.data(), address.length());

            std::string escapedName;
            std::string escapedHash;
            std::string escapedIp;
            const bool queued =
                OPCoreMySQL::escapeString(account.name, escapedName)
                && OPCoreMySQL::escapeString(passwordHash, escapedHash)
                && OPCoreMySQL::escapeString(ipAddress, escapedIp)
                && OPCoreMySQL::queryAsync(
                    fmt::format(
                        "INSERT INTO `accounts` "
                        "(`name`, `password_hash`, `created_at`, `last_login_at`, `last_ip`) "
                        "VALUES ('{}', '{}', UTC_TIMESTAMP(), UTC_TIMESTAMP(), '{}')",
                        escapedName, escapedHash, escapedIp),
                    forPlayerConnection(player, account,
                        [onFinished](IPlayer& player, PlayerAccount& account, OPCoreMySQL::QueryResult result)
                    {
                        if (result.insertId == 0
                            || result.insertId > std::numeric_limits<std::uint32_t>::max())
                        {
                            OPCORE_LOG_ERROR(
                                "[OPAccount] 注册账号 {} 失败：数据库返回了无效的账号 ID",
                                account.name);
                            account.authenticationPending = false;
                            onFinished(player, AuthenticationResult::Failed);
                            return;
                        }

                        account.accountId = static_cast<std::uint32_t>(result.insertId);
                        OPCORE_LOG_INFO("[OPAccount] 新账号 {} 注册成功", account.name);
                        const AuthenticationResult loginResult = logInPlayer(player, account);
                        account.authenticationPending = false;
                        onFinished(player, loginResult);
                    }),
                    forPlayerConnection(player, account,
                        [onFinished](
                            IPlayer& player,
                            PlayerAccount& account,
                            unsigned int code,
                            const std::string& message)
                    {
                        if (code != kDuplicateEntryError)
                        {
                            OPCORE_LOG_ERROR(
                                "[OPAccount] 注册账号 {} 失败：[{}] {}",
                                account.name, code, message);
                        }

                        account.authenticationPending = false;
                        onFinished(
                            player,
                            code == kDuplicateEntryError
                                ? AuthenticationResult::NameAlreadyTaken
                                : AuthenticationResult::Failed);
                    }));

            if (!queued)
            {
                OPCORE_LOG_ERROR(
                    "[OPAccount] 注册账号 {} 失败：字符串转义失败或数据库请求未能进入执行队列",
                    account.name);
                account.authenticationPending = false;
                onFinished(player, AuthenticationResult::Failed);
            }
        }));

    if (!hashQueued)
    {
        OPCORE_LOG_ERROR("[OPAccount] {} 的密码哈希任务未能提交", account->name);
        account->authenticationPending = false;
        onFinished(player, AuthenticationResult::Failed);
    }
}

bool isLoggedIn(IPlayer& player)
{
    const PlayerAccount* account = PlayerAccount::of(player);
    return account && account->loggedIn;
}

std::uint32_t getID(IPlayer& player)
{
    const PlayerAccount* account = PlayerAccount::of(player);
    return account && account->loggedIn ? account->accountId : 0;
}

StringView getName(IPlayer& player)
{
    const PlayerAccount* account = PlayerAccount::of(player);
    if (!account)
        return {};

    return StringView(account->name.data(), account->name.size());
}

IEventDispatcher<EventHandler>& getEventDispatcher()
{
    return g_eventDispatcher;
}
}

namespace
{
class OPAccountModule final
    : public OPCoreModuleManager
    , public PlayerConnectEventHandler
{
public:
    bool onAllModulesReady() override
    {
        ICore* core = OPCoreSharedPtr::core();
        if (!core)
        {
            OPCORE_LOG_CRITICAL("[OPAccount] Core 尚未就绪，账号模块无法启动");
            return false;
        }

        listen(core->getPlayers().getPlayerConnectDispatcher(), *this);

        const std::string query = fmt::format(
            "CREATE TABLE IF NOT EXISTS `accounts` ("
            "  `id`            INT UNSIGNED NOT NULL AUTO_INCREMENT,"
            "  `name`          VARCHAR(24)  NOT NULL,"
            "  `original_name` VARCHAR(24)  NULL,"
            "  `password_hash` VARCHAR({})  NOT NULL,"
            "  `created_at`    DATETIME     NOT NULL,"
            "  `last_login_at` DATETIME     NULL,"
            "  `last_ip`       VARCHAR(45)  NULL,"
            "  `deleted_at`    DATETIME     NULL,"
            "  PRIMARY KEY (`id`),"
            "  UNIQUE KEY `uk_accounts_name` (`name`)"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci",
            OPCoreHash::kHashCapacity);

        OPCoreMySQL::QueryError error;
        if (OPCoreMySQL::executeSync(query, &error))
            return true;

        OPCORE_LOG_CRITICAL(
            "[OPAccount] 创建 accounts 表失败：[{}] {}",
            error.code, error.message);
        return false;
    }

    StringView moduleName() const override { return "OPAccount"; }

    void onPlayerDisconnect(IPlayer& player, PeerDisconnectReason) override
    {
        PlayerAccount* account = PlayerAccount::of(player);
        if (!account || !account->loggedIn)
            return;

        g_eventDispatcher.dispatch(&OPAccount::EventHandler::onAccountLogout, player);
        g_onlineAccountIds.erase(account->accountId);
    }
};

OPCORE_REGISTER_MODULE(OPAccountModule);
}
