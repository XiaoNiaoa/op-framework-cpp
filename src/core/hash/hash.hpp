
#pragma once

#include <cstddef>
#include <functional>
#include <string>

namespace OPCoreHash
{

/**
 * @brief 密码哈希服务。
 *
 * 使用 libsodium 的 Argon2id 算法生成和验证密码哈希。
 *
 * 哈希计算由模块自己的后台工作线程执行，完成后的回调统一在
 * OPFramework Core onTick 主线程中调用。
 *
 * @note 所有密码哈希参数由模块内部统一管理。
 * @note 模块退出时，尚未完成的任务不会继续执行，未派发的结果不会触发回调。
 */

/**
 * @brief 密码哈希字符串的最大存储容量。
 *
 * 包含字符串结尾的 NUL 字符。
 *
 * 用于确定数据库字段容量。
 */
inline constexpr std::size_t kHashCapacity = 128;

/**
 * @brief 密码验证结果。
 */
struct VerifyResult
{
    bool matched = false; ///< 原始密码是否与已存储的哈希匹配。

    /**
     * @brief 使用当前哈希参数重新生成的密码哈希。
     *
     * 仅当密码验证成功、原哈希需要升级且重新生成成功时提供。
     * 调用方可以将其写回数据库。
     *
     * 为空表示没有可用的新哈希。
     */
    std::string rehash;
};

/**
 * @brief 密码哈希完成回调。
 *
 * @param hash 生成的密码哈希；生成失败时为空字符串。
 */
using HashCallback = std::function<void(std::string hash)>;

/**
 * @brief 密码验证完成回调。
 *
 * @param result 密码验证结果及可选的新哈希。
 */
using VerifyCallback = std::function<void(VerifyResult result)>;

/**
 * @brief 异步生成密码哈希。
 *
 * 将密码哈希任务提交到后台工作线程。
 *
 * @param password 待哈希的明文密码。
 * @param onDone 哈希完成回调。
 *
 * @return 任务成功进入执行队列返回 true，否则返回 false。
 *
 * @note 成功提交后，无论哈希计算成功还是失败，都会调用回调。
 * @note 回调参数为空字符串表示哈希生成失败。
 * @note 回调在 Core onTick 主线程执行。
 * @note 返回 false 表示任务未提交，不会调用回调。
 * @note 模块接管密码数据后，会在计算完成时清除明文缓冲区。
 */
bool hashAsync(std::string password, HashCallback onDone);

/**
 * @brief 异步验证密码。
 *
 * 使用已存储的密码哈希验证明文密码。
 *
 * @param password 待验证的明文密码。
 * @param hash 数据库中已存储的密码哈希。
 * @param onDone 密码验证完成回调。
 *
 * @return 任务成功进入执行队列返回 true，否则返回 false。
 *
 * @note 密码不匹配时仍会调用回调，此时 result.matched 为 false。
 * @note 如果旧哈希需要升级，验证成功后会尝试生成新的哈希。
 * @note 新哈希生成成功后通过 result.rehash 返回。
 * @note 回调在 Core onTick 主线程执行。
 * @note 返回 false 表示任务未提交，不会调用回调。
 * @note 模块接管密码数据后，会在计算完成时清除明文缓冲区。
 */
bool verifyAsync(std::string password, std::string hash, VerifyCallback onDone);

/**
 * @brief 清除明文密码。
 *
 * 使用 libsodium 的 sodium_memzero() 清零字符串占用的有效数据，
 * 然后清空字符串。
 *
 * @param secret 需要清除的明文数据。
 *
 * @note 清零操作不会被编译器作为无用写入优化掉。
 */
void wipe(std::string& secret);

}
