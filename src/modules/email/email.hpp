#pragma once

#include <cstddef>
#include <functional>
#include <string>

#include <sdk.hpp>

/**
 * @brief 独立的 QQ 邮箱绑定和人工验证码验证模块。
 *
 * 本模块只负责邮箱绑定、验证码生成和验证码验证，不负责账号认证、权限判断、
 * 玩家业务状态或人工审核流程。上述规则由外部业务层实现。
 * 模块使用 IPlayer::getName() 作为 emails.name 关联键。
 *
 * 所有公开函数必须在 Core 主线程调用，所有回调也在 Core 主线程执行。
 * 玩家断线或连接 ID 被复用时，旧连接的异步回调会被丢弃。
 */
namespace OPEmail
{
namespace EmailRules
{
// 64 个本地部分字符 + "@qq.com"。
inline constexpr std::size_t MaximumLength = 71;
}

enum class CheckResult
{
    Bound,
    NotBound,
    InProgress,
    InvalidBinding,
    Failed
};

enum class EmailResult
{
    Success,
    Failed,
    InProgress,
    NotChecked,
    InvalidEmail,
    InvalidBinding,
    AlreadyBound,
    EmailAlreadyUsed,
    NotBound,
    AlreadyVerified,
    RequestTooSoon,
    IpRateLimited,
    ServerRateLimited,
    CodeNotRequested,
    InvalidCode,
    IncorrectCode,
    TooManyAttempts
};

using CheckCallback = std::function<void(IPlayer&, CheckResult)>;
using EmailCallback = std::function<void(IPlayer&, EmailResult)>;

/**
 * @brief 检查当前玩家名称对应的 QQ 邮箱绑定状态。
 *
 * 首次调用从数据库读取，并缓存当前连接的结果。查询失败后再次调用会重新查询。
 * 重复调用正在执行的检查不会创建第二条数据库请求，而是返回 InProgress。
 *
 * @note 本模块不检查账号认证或权限；调用方负责这些业务规则。
 * @note 数据库以 player.getName() 作为 name 键。改名时外部业务必须同步更新 emails.name。
 * @note 必须在 Core 主线程调用；回调也在 Core 主线程执行。
 * @note 回调可能同步执行，也可能在后续 Core Tick 执行。
 * @note 玩家断线或连接 ID 被复用时，旧连接回调会被丢弃。
 */
void checkBinding(IPlayer& player, CheckCallback onFinished);

/**
 * @brief 为当前玩家名称绑定 QQ 邮箱。
 *
 * 必须先调用 checkBinding() 并得到 NotBound。即使调用方遗漏这一步，模块也会
 * 返回 NotChecked，不会直接写入数据库。
 *
 * 每个 player.getName() 只能绑定一个邮箱，每个 QQ 邮箱只能属于一个 name；
 * 最终由数据库唯一索引保证并发情况下的数据唯一性。
 *
 * @note 本模块不检查账号认证或权限。
 * @note 必须在 Core 主线程调用；回调也在 Core 主线程执行。
 */
void bind(IPlayer& player, std::string email, EmailCallback onFinished);

/**
 * @brief 生成六位验证码并保存到 MySQL，供客服人工查询。
 *
 * 验证码永久有效，直到验证成功、连续输错五次或重新申请时被替换。
 * 邮箱冷却 60 秒由数据库保证；同 IP 冷却 30 秒和全服生成间隔 10 秒由当前进程保证。
 * Success 表示验证码已写入数据库，不表示发送了真实邮件。
 *
 * @note 本模块不接入账号认证或权限系统。
 * @note 必须先通过 checkBinding() 确认邮箱已绑定。
 * @note 必须在 Core 主线程调用；回调也在 Core 主线程执行。
 */
void requestVerification(IPlayer& player, EmailCallback onFinished);

/**
 * @brief 验证六位数字验证码。
 *
 * 验证码永久有效。连续输错五次后数据库会清除验证码；验证成功后也会清除验证码。
 * 非六位数字输入返回 InvalidCode，不计入错误次数。
 *
 * @note 必须先通过 checkBinding() 确认邮箱已绑定。
 * @note 必须在 Core 主线程调用；回调也在 Core 主线程执行。
 */
void verify(IPlayer& player, std::string code, EmailCallback onFinished);

/** @brief 当前连接是否已成功读取到邮箱绑定。 */
bool isBound(IPlayer& player);

/** @brief 当前连接是否已验证邮箱。 */
bool isVerified(IPlayer& player);

/**
 * @brief 获取当前绑定邮箱的副本。
 *
 * 检查未完成、检查失败或未绑定时返回空字符串。
 * 调用权限由外部业务层负责。
 */
std::string getEmail(IPlayer& player);
}
