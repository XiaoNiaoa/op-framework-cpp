// 邮箱业务：检查 QQ 邮箱绑定、绑定邮箱、生成验证码和验证。
// 验证码直接保存在 MySQL，供客服人工查询；不依赖外部邮件服务。
// 玩家状态和进程内限流只在 Core 主线程访问。

#include "email.hpp"

#include <fmt/format.h>
#include <sodium.h>
#include <sdk.hpp>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>

#include "core/log/log.hpp"
#include "core/module.hpp"
#include "core/mysql.hpp"
#include "core/pointers.hpp"

namespace
{
using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;
using PlayerId = decltype(std::declval<IPlayer&>().getID());

constexpr unsigned int kDuplicateEntryError = 1062;
constexpr auto kEmailCooldown = std::chrono::seconds(60);
constexpr auto kIpCooldown = std::chrono::seconds(30);
constexpr auto kServerCooldown = std::chrono::seconds(10);
constexpr auto kRateLimitCleanupInterval = std::chrono::minutes(1);
constexpr unsigned int kMaxVerificationAttempts = 5;

// 邮箱冷却由数据库 verification_requested_at 统一负责。
// IP 和全服限制是进程级保护，不承诺跨服务器进程生效。
std::unordered_map<std::string, TimePoint> g_ipRequests;
TimePoint g_lastServerRequest{};
bool g_verificationSavePending = false;
TimePoint g_nextRateLimitCleanup{};
std::uint64_t g_nextConnectSequence = 0;

struct PlayerEmail final : IExtension
{
    PROVIDE_EXT_UID(0x4F50456D61696C21);

    enum class CheckState
    {
        Pending,
        Ready,
        Failed,
        InvalidBinding
    };

    const std::uint64_t connectSequence;
    const std::string name;
    std::string email;
    CheckState checkState = CheckState::Pending;
    bool verified = false;
    bool operationPending = false;

    PlayerEmail(std::uint64_t sequence, std::string playerName)
        : connectSequence(sequence)
        , name(std::move(playerName))
    {
    }

    static PlayerEmail* of(IPlayer& player)
    {
        return queryExtension<PlayerEmail>(player);
    }

    void freeExtension() override
    {
        delete this;
    }

    // GMX 不取消已经提交的数据库操作；玩家断线时 Extension 随连接销毁。
    void reset() override
    {
    }
};

struct WipeStringOnExit final
{
    std::string& value;

    explicit WipeStringOnExit(std::string& value) noexcept
        : value(value)
    {
    }

    WipeStringOnExit(const WipeStringOnExit&) = delete;
    WipeStringOnExit& operator=(const WipeStringOnExit&) = delete;

    ~WipeStringOnExit() noexcept
    {
        if (!value.empty())
            sodium_memzero(value.data(), value.size());
    }
};

struct WipeQueryResultOnExit final
{
    OPCoreMySQL::QueryResult& result;
    std::size_t codeColumn;

    explicit WipeQueryResultOnExit(
        OPCoreMySQL::QueryResult& result,
        std::size_t codeColumn = static_cast<std::size_t>(-1)) noexcept
        : result(result)
        , codeColumn(codeColumn)
    {
    }

    WipeQueryResultOnExit(const WipeQueryResultOnExit&) = delete;
    WipeQueryResultOnExit& operator=(const WipeQueryResultOnExit&) = delete;

    ~WipeQueryResultOnExit() noexcept
    {
        if (!result.query.empty())
            sodium_memzero(result.query.data(), result.query.size());

        if (codeColumn == static_cast<std::size_t>(-1))
            return;

        for (auto& row : result.rows)
        {
            if (row.size() > codeColumn && row[codeColumn] && !row[codeColumn]->empty())
                sodium_memzero(row[codeColumn]->data(), row[codeColumn]->size());
        }
    }
};

struct SecureCode final
{
    char value[6]{};

    explicit SecureCode(std::string_view code) noexcept
    {
        if (code.size() == sizeof(value))
            std::copy(code.begin(), code.end(), value);
    }

    SecureCode(const SecureCode&) = delete;
    SecureCode& operator=(const SecureCode&) = delete;

    ~SecureCode() noexcept
    {
        sodium_memzero(value, sizeof(value));
    }
};

std::uint64_t nextConnectSequence() noexcept
{
    ++g_nextConnectSequence;
    if (g_nextConnectSequence == 0)
        ++g_nextConnectSequence;
    return g_nextConnectSequence;
}

PlayerEmail* getCurrentEmail(
    PlayerId playerId,
    std::uint64_t sequence,
    IPlayer*& player)
{
    ICore* core = OPCoreSharedPtr::core();
    player = core ? core->getPlayers().get(playerId) : nullptr;
    if (!player)
        return nullptr;

    PlayerEmail* email = PlayerEmail::of(*player);
    if (!email || email->connectSequence != sequence)
    {
        player = nullptr;
        return nullptr;
    }

    return email;
}

void finishOperation(
    PlayerId playerId,
    std::uint64_t sequence,
    const OPEmail::EmailCallback& callback,
    OPEmail::EmailResult result)
{
    IPlayer* player = nullptr;
    PlayerEmail* email = getCurrentEmail(playerId, sequence, player);
    if (!email)
        return;

    email->operationPending = false;
    callback(*player, result);
}

bool isValidQQEmail(std::string_view email)
{
    if (email.empty() || email.size() > OPEmail::EmailRules::MaximumLength)
        return false;

    const std::size_t at = email.find('@');
    if (at == std::string_view::npos || at == 0
        || email.find('@', at + 1) != std::string_view::npos)
        return false;

    const std::string_view local = email.substr(0, at);
    const std::string_view domain = email.substr(at + 1);
    if (local.size() > 64 || domain.size() != 6
        || local.front() == '.' || local.back() == '.'
        || local.find("..") != std::string_view::npos)
        return false;

    constexpr std::string_view qqDomain = "qq.com";
    for (std::size_t i = 0; i < qqDomain.size(); ++i)
    {
        unsigned char c = static_cast<unsigned char>(domain[i]);
        if (c >= 'A' && c <= 'Z')
            c = static_cast<unsigned char>(c - 'A' + 'a');
        if (c != static_cast<unsigned char>(qqDomain[i]))
            return false;
    }

    for (unsigned char c : local)
    {
        const bool alphaNumeric = (c >= 'a' && c <= 'z')
            || (c >= 'A' && c <= 'Z')
            || (c >= '0' && c <= '9');
        if (!alphaNumeric && c != '.' && c != '_' && c != '-')
            return false;
    }

    return true;
}

std::string normalizeQQEmail(std::string_view email)
{
    std::string result(email);
    for (char& c : result)
    {
        const unsigned char value = static_cast<unsigned char>(c);
        if (value >= 'A' && value <= 'Z')
            c = static_cast<char>(value - 'A' + 'a');
    }
    return result;
}

std::string getPlayerIpAddress(IPlayer& player)
{
    PeerAddress::AddressString address;
    PeerAddress::ToString(player.getNetworkData().networkID.address, address);
    return address.length() == 0
        ? std::string{}
        : std::string(address.data(), address.length());
}

bool isCoolingDown(
    const std::unordered_map<std::string, TimePoint>& requests,
    const std::string& key,
    TimePoint now,
    Clock::duration cooldown)
{
    const auto it = requests.find(key);
    return it != requests.end() && now - it->second < cooldown;
}

void cleanRateLimits(TimePoint now)
{
    if (g_nextRateLimitCleanup != TimePoint{} && now < g_nextRateLimitCleanup)
        return;

    for (auto it = g_ipRequests.begin(); it != g_ipRequests.end();)
    {
        if (now - it->second >= kIpCooldown)
            it = g_ipRequests.erase(it);
        else
            ++it;
    }

    g_nextRateLimitCleanup = now + kRateLimitCleanupInterval;
}

std::string generateVerificationCode()
{
    return fmt::format("{:06}", randombytes_uniform(1000000));
}

// 保留 sendVerificationEmail 这个业务名称：当前实现负责把验证码保存到数据库，
// 不代表已经调用外部邮件服务或实际发送邮件。
bool sendVerificationEmail(
    const std::string& playerName,
    const std::string& email,
    const std::string& code,
    OPCoreMySQL::QueryCallback onSuccess,
    OPCoreMySQL::QueryErrorCallback onError)
{
    if (code.size() != 6)
        return false;

    std::string escapedName;
    std::string escapedEmail;
    std::string codeCopy(code);
    WipeStringOnExit wipeCodeCopy(codeCopy);
    std::string escapedCode;
    WipeStringOnExit wipeEscapedCode(escapedCode);

    if (!OPCoreMySQL::escapeString(playerName, escapedName)
        || !OPCoreMySQL::escapeString(email, escapedEmail)
        || !OPCoreMySQL::escapeString(codeCopy, escapedCode))
        return false;

    std::string query = fmt::format(
        "UPDATE `emails` SET `verification_code` = '{}', "
        "`verification_failed_attempts` = 0, `verification_requested_at` = UTC_TIMESTAMP() "
        "WHERE `name` = '{}' AND `email` = '{}' AND `verified` = 0 "
        "AND (`verification_requested_at` IS NULL "
        "OR `verification_requested_at` <= UTC_TIMESTAMP() - INTERVAL {} SECOND) LIMIT 1",
        escapedCode, escapedName, escapedEmail, kEmailCooldown.count());

    return OPCoreMySQL::queryAsync(
        std::move(query), std::move(onSuccess), std::move(onError));
}

void checkVerificationStatus(
    PlayerId playerId,
    std::uint64_t sequence,
    const std::string& playerName,
    const std::string& email,
    OPEmail::EmailCallback callback)
{
    IPlayer* player = nullptr;
    if (!getCurrentEmail(playerId, sequence, player))
        return;

    std::string escapedName;
    std::string escapedEmail;
    if (!OPCoreMySQL::escapeString(playerName, escapedName)
        || !OPCoreMySQL::escapeString(email, escapedEmail))
    {
        finishOperation(playerId, sequence, callback, OPEmail::EmailResult::Failed);
        return;
    }

    const std::string query = fmt::format(
        "SELECT `verified`, "
        "(`verification_requested_at` IS NOT NULL AND `verification_requested_at` "
        "> UTC_TIMESTAMP() - INTERVAL {} SECOND) "
        "FROM `emails` WHERE `name` = '{}' AND `email` = '{}' LIMIT 1",
        kEmailCooldown.count(), escapedName, escapedEmail);

    if (!OPCoreMySQL::queryAsync(
        query,
        [playerId, sequence, callback](OPCoreMySQL::QueryResult result)
        {
            IPlayer* currentPlayer = nullptr;
            PlayerEmail* current = getCurrentEmail(playerId, sequence, currentPlayer);
            if (!current)
                return;

            if (result.rows.empty())
            {
                // 数据库记录已不存在，不能继续把旧缓存当作有效绑定。
                current->email.clear();
                current->verified = false;
                current->checkState = PlayerEmail::CheckState::Ready;
                finishOperation(playerId, sequence, callback, OPEmail::EmailResult::NotBound);
                return;
            }

            const auto& row = result.rows.front();
            if (row.size() < 2 || !row[0] || !row[1]
                || (*row[0] != "0" && *row[0] != "1")
                || (*row[1] != "0" && *row[1] != "1"))
            {
                finishOperation(playerId, sequence, callback, OPEmail::EmailResult::Failed);
                return;
            }

            if (*row[0] == "1")
            {
                current->verified = true;
                finishOperation(playerId, sequence, callback, OPEmail::EmailResult::AlreadyVerified);
                return;
            }

            if (*row[1] == "1")
            {
                finishOperation(playerId, sequence, callback, OPEmail::EmailResult::RequestTooSoon);
                return;
            }

            finishOperation(playerId, sequence, callback, OPEmail::EmailResult::Failed);
        },
        [playerId, sequence, callback](unsigned int code, const std::string&)
        {
            IPlayer* currentPlayer = nullptr;
            PlayerEmail* current = getCurrentEmail(playerId, sequence, currentPlayer);
            if (current)
                OPCORE_LOG_ERROR("[OPEmail] 查询玩家 {} 的验证码申请状态失败：[{}]", current->name, code);
            finishOperation(playerId, sequence, callback, OPEmail::EmailResult::Failed);
        }))
    {
        finishOperation(playerId, sequence, callback, OPEmail::EmailResult::Failed);
    }
}

// 1062 可能来自 name 或 email 唯一索引；一次查询检查两种冲突。
void resolveDuplicateBinding(
    PlayerId playerId,
    std::uint64_t sequence,
    const std::string& attemptedEmail,
    const OPEmail::EmailCallback& callback)
{
    IPlayer* player = nullptr;
    PlayerEmail* current = getCurrentEmail(playerId, sequence, player);
    if (!current)
        return;

    std::string escapedName;
    std::string escapedEmail;
    if (!OPCoreMySQL::escapeString(current->name, escapedName)
        || !OPCoreMySQL::escapeString(attemptedEmail, escapedEmail))
    {
        finishOperation(playerId, sequence, callback, OPEmail::EmailResult::Failed);
        return;
    }

    const std::string query = fmt::format(
        "SELECT 'name' AS `conflict`, `email`, `verified` FROM `emails` WHERE `name` = '{}' "
        "UNION ALL "
        "SELECT 'email' AS `conflict`, NULL, NULL FROM `emails` WHERE `email` = '{}'",
        escapedName, escapedEmail);

    if (!OPCoreMySQL::queryAsync(
        query,
        [playerId, sequence, callback](OPCoreMySQL::QueryResult result)
        {
            IPlayer* currentPlayer = nullptr;
            PlayerEmail* current = getCurrentEmail(playerId, sequence, currentPlayer);
            if (!current)
                return;

            bool emailUsed = false;
            for (const auto& row : result.rows)
            {
                if (row.empty() || !row[0])
                    continue;

                if (*row[0] == "name")
                {
                    if (row.size() < 3 || !row[1] || row[1]->empty() || !row[2]
                        || !isValidQQEmail(*row[1])
                        || (*row[2] != "0" && *row[2] != "1"))
                    {
                        current->checkState = PlayerEmail::CheckState::InvalidBinding;
                        finishOperation(playerId, sequence, callback, OPEmail::EmailResult::InvalidBinding);
                        return;
                    }

                    current->email = normalizeQQEmail(*row[1]);
                    current->verified = (*row[2] == "1");
                    current->checkState = PlayerEmail::CheckState::Ready;
                    finishOperation(playerId, sequence, callback, OPEmail::EmailResult::AlreadyBound);
                    return;
                }

                if (*row[0] == "email")
                    emailUsed = true;
            }

            finishOperation(
                playerId, sequence, callback,
                emailUsed ? OPEmail::EmailResult::EmailAlreadyUsed : OPEmail::EmailResult::Failed);
        },
        [playerId, sequence, callback](unsigned int code, const std::string&)
        {
            IPlayer* currentPlayer = nullptr;
            PlayerEmail* current = getCurrentEmail(playerId, sequence, currentPlayer);
            if (current)
                OPCORE_LOG_ERROR("[OPEmail] 查询邮箱重复绑定原因失败：[{}]", code);
            finishOperation(playerId, sequence, callback, OPEmail::EmailResult::Failed);
        }))
    {
        finishOperation(playerId, sequence, callback, OPEmail::EmailResult::Failed);
    }
}

} // namespace

namespace OPEmail
{
void checkBinding(IPlayer& player, CheckCallback onFinished)
{
    if (!onFinished)
        return;

    PlayerEmail* email = PlayerEmail::of(player);
    if (email)
    {
        if (email->checkState == PlayerEmail::CheckState::Pending || email->operationPending)
        {
            onFinished(player, CheckResult::InProgress);
            return;
        }

        if (email->checkState == PlayerEmail::CheckState::Ready)
        {
            onFinished(player, email->email.empty() ? CheckResult::NotBound : CheckResult::Bound);
            return;
        }

        // Failed / InvalidBinding 都允许调用方显式重试检查。
        email->checkState = PlayerEmail::CheckState::Pending;
        email->email.clear();
        email->verified = false;
    }
    else
    {
        const StringView playerName = player.getName();
        if (playerName.length() == 0)
        {
            onFinished(player, CheckResult::Failed);
            return;
        }

        email = new PlayerEmail(
            nextConnectSequence(),
            std::string(playerName.data(), playerName.length()));

        if (!player.addExtension(email, true))
        {
            delete email;
            onFinished(player, CheckResult::Failed);
            return;
        }
    }

    const PlayerId playerId = player.getID();
    const std::uint64_t sequence = email->connectSequence;
    std::string escapedName;
    if (!OPCoreMySQL::escapeString(email->name, escapedName))
    {
        email->checkState = PlayerEmail::CheckState::Failed;
        onFinished(player, CheckResult::Failed);
        return;
    }

    const std::string query = fmt::format(
        "SELECT `email`, `verified` FROM `emails` WHERE `name` = '{}' LIMIT 1",
        escapedName);

    if (!OPCoreMySQL::queryAsync(
        query,
        [playerId, sequence, onFinished](OPCoreMySQL::QueryResult result)
        {
            IPlayer* currentPlayer = nullptr;
            PlayerEmail* current = getCurrentEmail(playerId, sequence, currentPlayer);
            if (!current)
                return;

            if (result.rows.empty())
            {
                current->email.clear();
                current->verified = false;
                current->checkState = PlayerEmail::CheckState::Ready;
                onFinished(*currentPlayer, CheckResult::NotBound);
                return;
            }

            const auto& row = result.rows.front();
            if (row.size() < 2 || !row[0] || row[0]->empty() || !row[1]
                || !isValidQQEmail(*row[0])
                || (*row[1] != "0" && *row[1] != "1"))
            {
                current->checkState = PlayerEmail::CheckState::InvalidBinding;
                OPCORE_LOG_ERROR(
                    "[OPEmail] 玩家 {} 的邮箱记录无效，需要修复数据库记录",
                    current->name);
                onFinished(*currentPlayer, CheckResult::InvalidBinding);
                return;
            }

            current->email = normalizeQQEmail(*row[0]);
            current->verified = (*row[1] == "1");
            current->checkState = PlayerEmail::CheckState::Ready;
            onFinished(*currentPlayer, CheckResult::Bound);
        },
        [playerId, sequence, onFinished](unsigned int code, const std::string&)
        {
            IPlayer* currentPlayer = nullptr;
            PlayerEmail* current = getCurrentEmail(playerId, sequence, currentPlayer);
            if (!current)
                return;

            current->checkState = PlayerEmail::CheckState::Failed;
            OPCORE_LOG_ERROR("[OPEmail] 检查玩家 {} 的邮箱绑定失败：[{}]", current->name, code);
            onFinished(*currentPlayer, CheckResult::Failed);
        }))
    {
        email->checkState = PlayerEmail::CheckState::Failed;
        OPCORE_LOG_ERROR("[OPEmail] 检查玩家 {} 的邮箱请求未能进入执行队列", email->name);
        onFinished(player, CheckResult::Failed);
    }
}

void bind(IPlayer& player, std::string email, EmailCallback onFinished)
{
    if (!onFinished)
        return;

    PlayerEmail* playerEmail = PlayerEmail::of(player);
    if (!playerEmail)
    {
        onFinished(player, EmailResult::NotChecked);
        return;
    }
    if (playerEmail->checkState == PlayerEmail::CheckState::Pending || playerEmail->operationPending)
    {
        onFinished(player, EmailResult::InProgress);
        return;
    }
    if (playerEmail->checkState == PlayerEmail::CheckState::InvalidBinding)
    {
        onFinished(player, EmailResult::InvalidBinding);
        return;
    }
    if (playerEmail->checkState != PlayerEmail::CheckState::Ready)
    {
        onFinished(player, EmailResult::Failed);
        return;
    }
    if (!playerEmail->email.empty())
    {
        onFinished(player, EmailResult::AlreadyBound);
        return;
    }
    if (!isValidQQEmail(email))
    {
        onFinished(player, EmailResult::InvalidEmail);
        return;
    }

    email = normalizeQQEmail(email);
    playerEmail->operationPending = true;
    const PlayerId playerId = player.getID();
    const std::uint64_t sequence = playerEmail->connectSequence;
    const std::string attemptedEmail = email;

    std::string escapedName;
    std::string escapedEmail;
    if (!OPCoreMySQL::escapeString(playerEmail->name, escapedName)
        || !OPCoreMySQL::escapeString(email, escapedEmail))
    {
        playerEmail->operationPending = false;
        onFinished(player, EmailResult::Failed);
        return;
    }

    const std::string query = fmt::format(
        "INSERT INTO `emails` (`name`, `email`, `created_at`) "
        "VALUES ('{}', '{}', UTC_TIMESTAMP())",
        escapedName, escapedEmail);

    if (!OPCoreMySQL::queryAsync(
        query,
        [playerId, sequence, email = std::move(email), onFinished](OPCoreMySQL::QueryResult result) mutable
        {
            PlayerEmail* current = nullptr;
            IPlayer* currentPlayer = nullptr;
            current = getCurrentEmail(playerId, sequence, currentPlayer);
            if (!current)
                return;

            if (result.affectedRows != 1)
            {
                finishOperation(playerId, sequence, onFinished, EmailResult::Failed);
                return;
            }

            current->email = std::move(email);
            current->verified = false;
            current->checkState = PlayerEmail::CheckState::Ready;
            finishOperation(playerId, sequence, onFinished, EmailResult::Success);
        },
        [playerId, sequence, attemptedEmail, onFinished](unsigned int code, const std::string&)
        {
            if (code == kDuplicateEntryError)
            {
                resolveDuplicateBinding(playerId, sequence, attemptedEmail, onFinished);
                return;
            }

            IPlayer* currentPlayer = nullptr;
            PlayerEmail* current = getCurrentEmail(playerId, sequence, currentPlayer);
            if (current)
                OPCORE_LOG_ERROR("[OPEmail] 为玩家 {} 绑定邮箱失败：[{}]", current->name, code);
            finishOperation(playerId, sequence, onFinished, EmailResult::Failed);
        }))
    {
        playerEmail->operationPending = false;
        OPCORE_LOG_ERROR("[OPEmail] 为玩家 {} 的绑定请求未能进入数据库队列", playerEmail->name);
        onFinished(player, EmailResult::Failed);
    }
}

void requestVerification(IPlayer& player, EmailCallback onFinished)
{
    if (!onFinished)
        return;

    PlayerEmail* playerEmail = PlayerEmail::of(player);
    if (!playerEmail)
    {
        onFinished(player, EmailResult::NotChecked);
        return;
    }
    if (playerEmail->checkState == PlayerEmail::CheckState::Pending || playerEmail->operationPending)
    {
        onFinished(player, EmailResult::InProgress);
        return;
    }
    if (playerEmail->checkState == PlayerEmail::CheckState::InvalidBinding)
    {
        onFinished(player, EmailResult::InvalidBinding);
        return;
    }
    if (playerEmail->checkState != PlayerEmail::CheckState::Ready)
    {
        onFinished(player, EmailResult::Failed);
        return;
    }
    if (playerEmail->email.empty())
    {
        onFinished(player, EmailResult::NotBound);
        return;
    }
    if (playerEmail->verified)
    {
        onFinished(player, EmailResult::AlreadyVerified);
        return;
    }

    const TimePoint now = Clock::now();
    cleanRateLimits(now);

    const std::string emailKey = playerEmail->email;
    const std::string playerName = playerEmail->name;
    const std::string ipKey = getPlayerIpAddress(player);
    if (ipKey.empty())
    {
        onFinished(player, EmailResult::Failed);
        return;
    }

    if (isCoolingDown(g_ipRequests, ipKey, now, kIpCooldown))
    {
        onFinished(player, EmailResult::IpRateLimited);
        return;
    }
    if (g_verificationSavePending
        || (g_lastServerRequest != TimePoint{} && now - g_lastServerRequest < kServerCooldown))
    {
        onFinished(player, EmailResult::ServerRateLimited);
        return;
    }

    std::string code = generateVerificationCode();
    WipeStringOnExit wipeCode(code);
    playerEmail->operationPending = true;

    const PlayerId playerId = player.getID();
    const std::uint64_t sequence = playerEmail->connectSequence;

    g_verificationSavePending = true;
    const bool queued = sendVerificationEmail(
        playerName,
        emailKey,
        code,
        [playerId, sequence, playerName, emailKey, ipKey, onFinished](OPCoreMySQL::QueryResult result)
        {
            WipeQueryResultOnExit wipeResult(result);
            g_verificationSavePending = false;
            if (result.affectedRows != 1)
            {
                // 即使数据库因邮箱冷却拒绝，也计入全服间隔，避免重复查库。
                g_lastServerRequest = Clock::now();
                checkVerificationStatus(playerId, sequence, playerName, emailKey, onFinished);
                return;
            }

            const TimePoint savedAt = Clock::now();
            g_ipRequests.insert_or_assign(ipKey, savedAt);
            g_lastServerRequest = savedAt;
            OPCORE_LOG_INFO("[OPEmail] 玩家 {} 的 QQ 邮箱验证码已保存，等待人工审核", playerName);
            finishOperation(playerId, sequence, onFinished, EmailResult::Success);
        },
        [playerId, sequence, onFinished](unsigned int code, const std::string&)
        {
            g_verificationSavePending = false;
            g_lastServerRequest = Clock::now();
            IPlayer* currentPlayer = nullptr;
            PlayerEmail* current = getCurrentEmail(playerId, sequence, currentPlayer);
            if (current)
                OPCORE_LOG_ERROR("[OPEmail] 保存玩家 {} 的验证码失败：[{}]", current->name, code);
            finishOperation(playerId, sequence, onFinished, EmailResult::Failed);
        });

    if (!queued)
    {
        g_verificationSavePending = false;
        playerEmail->operationPending = false;
        OPCORE_LOG_ERROR("[OPEmail] 玩家 {} 的验证码请求未能进入数据库队列", playerEmail->name);
        onFinished(player, EmailResult::Failed);
    }
}

void verify(IPlayer& player, std::string code, EmailCallback onFinished)
{
    WipeStringOnExit wipeInputCode(code);
    if (!onFinished)
        return;

    PlayerEmail* playerEmail = PlayerEmail::of(player);
    if (!playerEmail)
    {
        onFinished(player, EmailResult::NotChecked);
        return;
    }
    if (playerEmail->checkState == PlayerEmail::CheckState::Pending || playerEmail->operationPending)
    {
        onFinished(player, EmailResult::InProgress);
        return;
    }
    if (playerEmail->checkState == PlayerEmail::CheckState::InvalidBinding)
    {
        onFinished(player, EmailResult::InvalidBinding);
        return;
    }
    if (playerEmail->checkState != PlayerEmail::CheckState::Ready)
    {
        onFinished(player, EmailResult::Failed);
        return;
    }
    if (playerEmail->email.empty())
    {
        onFinished(player, EmailResult::NotBound);
        return;
    }
    if (playerEmail->verified)
    {
        onFinished(player, EmailResult::AlreadyVerified);
        return;
    }

    if (code.size() != 6)
    {
        onFinished(player, EmailResult::InvalidCode);
        return;
    }
    for (unsigned char c : code)
    {
        if (c < '0' || c > '9')
        {
            onFinished(player, EmailResult::InvalidCode);
            return;
        }
    }

    auto inputCode = std::make_shared<SecureCode>(code);
    playerEmail->operationPending = true;
    const PlayerId playerId = player.getID();
    const std::uint64_t sequence = playerEmail->connectSequence;
    const std::string emailKey = playerEmail->email;
    const std::string playerName = playerEmail->name;

    std::string escapedName;
    std::string escapedEmail;
    if (!OPCoreMySQL::escapeString(playerName, escapedName)
        || !OPCoreMySQL::escapeString(emailKey, escapedEmail))
    {
        playerEmail->operationPending = false;
        onFinished(player, EmailResult::Failed);
        return;
    }

    const std::string query = fmt::format(
        "SELECT `verified`, `verification_code`, `verification_failed_attempts` "
        "FROM `emails` WHERE `name` = '{}' AND `email` = '{}' LIMIT 1",
        escapedName, escapedEmail);

    if (!OPCoreMySQL::queryAsync(
        query,
        [playerId, sequence, emailKey, inputCode, onFinished](OPCoreMySQL::QueryResult result)
        {
            WipeQueryResultOnExit wipeResult(result, 1);
            IPlayer* currentPlayer = nullptr;
            PlayerEmail* current = getCurrentEmail(playerId, sequence, currentPlayer);
            if (!current)
                return;

            if (result.rows.empty())
            {
                current->email.clear();
                current->verified = false;
                current->checkState = PlayerEmail::CheckState::Ready;
                finishOperation(playerId, sequence, onFinished, EmailResult::NotBound);
                return;
            }

            const auto& row = result.rows.front();
            if (row.size() < 3 || !row[0] || !row[2]
                || (*row[0] != "0" && *row[0] != "1"))
            {
                finishOperation(playerId, sequence, onFinished, EmailResult::Failed);
                return;
            }

            if (*row[0] == "1")
            {
                current->verified = true;
                finishOperation(playerId, sequence, onFinished, EmailResult::AlreadyVerified);
                return;
            }

            unsigned int failedAttempts = 0;
            const char* begin = row[2]->data();
            const char* end = begin + row[2]->size();
            const auto parsed = std::from_chars(begin, end, failedAttempts);
            if (parsed.ec != std::errc{} || parsed.ptr != end)
            {
                finishOperation(playerId, sequence, onFinished, EmailResult::Failed);
                return;
            }

            // 优先判断次数上限，使达到上限后的重复调用仍返回 TooManyAttempts。
            if (failedAttempts >= kMaxVerificationAttempts)
            {
                finishOperation(playerId, sequence, onFinished, EmailResult::TooManyAttempts);
                return;
            }
            if (!row[1] || row[1]->empty())
            {
                finishOperation(playerId, sequence, onFinished, EmailResult::CodeNotRequested);
                return;
            }

            std::string storedCode = *row[1];
            WipeStringOnExit wipeStoredCode(storedCode);
            if (storedCode.size() != 6)
            {
                finishOperation(playerId, sequence, onFinished, EmailResult::Failed);
                return;
            }

            const bool matched = sodium_memcmp(inputCode->value, storedCode.data(), 6) == 0;
            std::string escapedName;
            std::string escapedEmail;
            std::string escapedCode;
            WipeStringOnExit wipeEscapedCode(escapedCode);
            if (!OPCoreMySQL::escapeString(current->name, escapedName)
                || !OPCoreMySQL::escapeString(emailKey, escapedEmail)
                || !OPCoreMySQL::escapeString(storedCode, escapedCode))
            {
                finishOperation(playerId, sequence, onFinished, EmailResult::Failed);
                return;
            }

            if (!matched)
            {
                // 条件中包含验证码和当前错误次数，避免覆盖并发更新。
                const std::string updateQuery = fmt::format(
                    "UPDATE `emails` SET "
                    "`verification_code` = IF(`verification_failed_attempts` + 1 >= {}, NULL, `verification_code`), "
                    "`verification_failed_attempts` = `verification_failed_attempts` + 1 "
                    "WHERE `name` = '{}' AND `email` = '{}' AND `verified` = 0 "
                    "AND `verification_code` = '{}' AND `verification_failed_attempts` = {} LIMIT 1",
                    kMaxVerificationAttempts,
                    escapedName, escapedEmail, escapedCode, failedAttempts);

                if (!OPCoreMySQL::queryAsync(
                    updateQuery,
                    [playerId, sequence, failedAttempts, onFinished](OPCoreMySQL::QueryResult updateResult)
                    {
                        WipeQueryResultOnExit wipeUpdate(updateResult);
                        if (updateResult.affectedRows != 1)
                        {
                            finishOperation(playerId, sequence, onFinished, EmailResult::Failed);
                            return;
                        }

                        finishOperation(
                            playerId, sequence, onFinished,
                            failedAttempts + 1 >= kMaxVerificationAttempts
                                ? EmailResult::TooManyAttempts
                                : EmailResult::IncorrectCode);
                    },
                    [playerId, sequence, onFinished](unsigned int errorCode, const std::string&)
                    {
                        IPlayer* currentPlayer = nullptr;
                        PlayerEmail* current = getCurrentEmail(playerId, sequence, currentPlayer);
                        if (current)
                            OPCORE_LOG_ERROR(
                                "[OPEmail] 更新玩家 {} 的验证码错误次数失败：[{}]",
                                current->name, errorCode);
                        finishOperation(playerId, sequence, onFinished, EmailResult::Failed);
                    }))
                {
                    finishOperation(playerId, sequence, onFinished, EmailResult::Failed);
                }
                return;
            }

            const std::string updateQuery = fmt::format(
                "UPDATE `emails` SET `verified` = 1, `verified_at` = UTC_TIMESTAMP(), "
                "`verification_code` = NULL, `verification_failed_attempts` = 0 "
                "WHERE `name` = '{}' AND `email` = '{}' AND `verified` = 0 "
                "AND `verification_code` = '{}' AND `verification_failed_attempts` = {} LIMIT 1",
                escapedName, escapedEmail, escapedCode, failedAttempts);

            if (!OPCoreMySQL::queryAsync(
                updateQuery,
                [playerId, sequence, onFinished](OPCoreMySQL::QueryResult updateResult)
                {
                    WipeQueryResultOnExit wipeUpdate(updateResult);
                    if (updateResult.affectedRows != 1)
                    {
                        finishOperation(playerId, sequence, onFinished, EmailResult::Failed);
                        return;
                    }

                    IPlayer* currentPlayer = nullptr;
                    PlayerEmail* current = getCurrentEmail(playerId, sequence, currentPlayer);
                    if (current)
                        current->verified = true;
                    finishOperation(playerId, sequence, onFinished, EmailResult::Success);
                },
                [playerId, sequence, onFinished](unsigned int errorCode, const std::string&)
                {
                    IPlayer* currentPlayer = nullptr;
                    PlayerEmail* current = getCurrentEmail(playerId, sequence, currentPlayer);
                    if (current)
                        OPCORE_LOG_ERROR(
                            "[OPEmail] 验证玩家 {} 的邮箱失败：[{}]",
                            current->name, errorCode);
                    finishOperation(playerId, sequence, onFinished, EmailResult::Failed);
                }))
            {
                finishOperation(playerId, sequence, onFinished, EmailResult::Failed);
            }
        },
        [playerId, sequence, onFinished](unsigned int errorCode, const std::string&)
        {
            IPlayer* currentPlayer = nullptr;
            PlayerEmail* current = getCurrentEmail(playerId, sequence, currentPlayer);
            if (current)
                OPCORE_LOG_ERROR("[OPEmail] 读取玩家 {} 的验证码失败：[{}]", current->name, errorCode);
            finishOperation(playerId, sequence, onFinished, EmailResult::Failed);
        }))
    {
        playerEmail->operationPending = false;
        onFinished(player, EmailResult::Failed);
    }
}

bool isBound(IPlayer& player)
{
    const PlayerEmail* email = PlayerEmail::of(player);
    return email
        && email->checkState == PlayerEmail::CheckState::Ready
        && !email->email.empty();
}

bool isVerified(IPlayer& player)
{
    const PlayerEmail* email = PlayerEmail::of(player);
    return email
        && email->checkState == PlayerEmail::CheckState::Ready
        && !email->email.empty()
        && email->verified;
}

std::string getEmail(IPlayer& player)
{
    const PlayerEmail* email = PlayerEmail::of(player);
    if (!email || email->checkState != PlayerEmail::CheckState::Ready)
        return {};
    return email->email;
}
}

namespace
{
class OPEmailModule final : public OPCoreModuleManager
{
public:
    bool onAllModulesReady() override
    {
        if (sodium_init() < 0)
        {
            OPCORE_LOG_CRITICAL("[OPEmail] libsodium 初始化失败");
            return false;
        }

        if (!OPCoreSharedPtr::core())
        {
            OPCORE_LOG_CRITICAL("[OPEmail] Core 尚未就绪");
            return false;
        }

        const std::string createTable = fmt::format(
            "CREATE TABLE IF NOT EXISTS `emails` ("
            "`id` INT UNSIGNED NOT NULL AUTO_INCREMENT, "
            "`name` VARCHAR(24) CHARACTER SET utf8mb4 COLLATE utf8mb4_general_ci NOT NULL, "
            "`email` VARCHAR({}) CHARACTER SET ascii COLLATE ascii_general_ci NOT NULL, "
            "`verified` TINYINT(1) NOT NULL DEFAULT 0, "
            "`verification_code` CHAR(6) CHARACTER SET ascii COLLATE ascii_bin NULL DEFAULT NULL, "
            "`verification_failed_attempts` TINYINT UNSIGNED NOT NULL DEFAULT 0, "
            "`verification_requested_at` DATETIME NULL, "
            "`created_at` DATETIME NOT NULL, "
            "`verified_at` DATETIME NULL, "
            "PRIMARY KEY (`id`), "
            "UNIQUE KEY `uk_emails_name` (`name`), "
            "UNIQUE KEY `uk_emails_email` (`email`)"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci",
            OPEmail::EmailRules::MaximumLength);

        OPCoreMySQL::QueryError error;
        if (OPCoreMySQL::executeSync(createTable, &error))
            return true;

        OPCORE_LOG_CRITICAL("[OPEmail] 创建 emails 表失败：[{}] {}", error.code, error.message);
        return false;
    }

    StringView moduleName() const override
    {
        return "OPEmail";
    }
};

OPCORE_REGISTER_MODULE(OPEmailModule);
}
