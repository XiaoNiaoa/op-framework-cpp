#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include <sdk.hpp>

namespace OPAccount
{
/**
 * @brief 账号密码长度限制。
 */
namespace PasswordRules
{
inline constexpr std::size_t MinimumLength = 6;
inline constexpr std::size_t MaximumLength = 32;
}

/**
 * @brief 检查玩家昵称是否已注册的结果。
 */
enum class AccountCheckResult
{
    Registered,
    NotRegistered,
    Failed
};

/**
 * @brief 登录或注册的结果。
 */
enum class AuthenticationResult
{
    Success,
    IncorrectPassword,
    InvalidPassword,
    NameAlreadyTaken,
    AlreadyOnline,
    InProgress,
    Failed
};

/**
 * @brief 账号检查完成回调。
 *
 * @param player 完成检查的玩家。
 * @param result 账号检查结果。
 */
using AccountCheckCallback = std::function<void(IPlayer&, AccountCheckResult)>;

/**
 * @brief 登录或注册完成回调。
 *
 * @param player 完成认证的玩家。
 * @param result 认证结果。
 */
using AuthenticationCallback = std::function<void(IPlayer&, AuthenticationResult)>;

/**
 * @brief 检查玩家昵称是否已经注册。
 *
 * @param player 需要检查的玩家。
 * @param onFinished 检查完成后的回调。
 *
 * @note 每次玩家连接只能调用一次；重复调用返回 Failed。
 * @note 必须在 Core 主线程调用，回调也在 Core 主线程执行。
 */
void checkAccountExists(IPlayer& player, AccountCheckCallback onFinished);

/**
 * @brief 验证密码并登录账号。
 *
 * @param player 需要登录的玩家。
 * @param password 玩家输入的密码。
 * @param onFinished 登录完成后的回调。
 *
 * @note 必须先通过 checkAccountExists() 确认账号已注册。
 * @note 玩家断线后回调不会执行；Hash 模块卸载时，尚未派发的哈希任务回调也不会执行。
 * @note 必须在 Core 主线程调用，回调也在 Core 主线程执行。
 */
void login(IPlayer& player, std::string password, AuthenticationCallback onFinished);

/**
 * @brief 创建账号并登录玩家。
 *
 * @param player 需要注册的玩家。
 * @param password 玩家设置的密码。
 * @param onFinished 注册完成后的回调。
 *
 * @note 必须先通过 checkAccountExists() 确认账号未注册。
 * @note 玩家断线后回调不会执行；Hash 模块卸载时，尚未派发的哈希任务回调也不会执行。
 * @note 必须在 Core 主线程调用，回调也在 Core 主线程执行。
 */
void registerAccount(IPlayer& player, std::string password, AuthenticationCallback onFinished);

/**
 * @brief 检查玩家是否已登录账号。
 *
 * @param player 需要检查的玩家。
 * @return 已登录返回 true，否则返回 false。
 */
bool isLoggedIn(IPlayer& player);

/**
 * @brief 获取玩家当前登录的账号 ID。
 *
 * @param player 需要查询的玩家。
 * @return 已登录返回账号 ID，未登录返回 0。
 */
std::uint32_t getID(IPlayer& player);

/**
 * @brief 获取玩家连接时记录的昵称。
 *
 * @param player 需要查询的玩家。
 * @return 玩家昵称。返回非空昵称不代表账号已经认证。
 *
 * @note 返回的 StringView 在玩家账号数据有效期间有效。
 */
StringView getName(IPlayer& player);

/**
 * @brief 账号登录和登出事件处理接口。
 */
struct EventHandler
{
    /**
     * @brief 玩家成功登录账号时调用。
     */
    virtual void onAccountLogin(IPlayer&) { }

    /**
     * @brief 已登录玩家断开连接时调用。
     */
    virtual void onAccountLogout(IPlayer&) { }
};

/**
 * @brief 获取账号事件分发器。
 *
 * @return 账号事件分发器。
 */
IEventDispatcher<EventHandler>& getEventDispatcher();
}
