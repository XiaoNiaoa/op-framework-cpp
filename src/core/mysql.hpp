
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace OPCoreMySQL
{

/**
 * @brief 数据库连接句柄 ID。
 */
using HandleId = std::uint32_t;

/**
 * @brief SQL 查询结果。
 *
 * SQL NULL 使用 std::nullopt 表示。
 */
struct QueryResult
{
    std::string query; ///< 执行的 SQL 语句。
    std::vector<std::string> fields; ///< 查询结果的字段名称。
    std::vector<std::vector<std::optional<std::string>>> rows; ///< 查询结果的所有数据行。

    std::uint64_t affectedRows = 0; ///< 受影响的行数。
    std::uint64_t insertId = 0; ///< 插入操作生成的 ID。
    std::uint32_t warningCount = 0; ///< 数据库警告数量。

    std::uint64_t executionTimeMilliseconds = 0; ///< 查询执行时间（毫秒）。
    std::uint64_t executionTimeMicroseconds = 0; ///< 查询执行时间（微秒）。
};

/**
 * @brief 数据库查询错误。
 */
struct QueryError
{
    unsigned int code = 0; ///< MySQL/MariaDB 错误码。
    std::string message; ///< 错误信息。
};

/**
 * @brief 查询成功回调。
 *
 * 异步查询的回调在 OPFramework Core onTick 主线程中执行。
 */
using QueryCallback = std::function<void(QueryResult)>;

/**
 * @brief 查询失败回调。
 *
 * @param code 数据库错误码。
 * @param message 错误信息。
 */
using QueryErrorCallback = std::function<void(unsigned int, const std::string&)>;

/**
 * @brief 获取自动创建的默认数据库连接句柄。
 *
 * @return 默认连接句柄。0 表示当前不可用。
 *
 * @note Pawn 端也可以通过 mysql_get_default_handle() 获取同一个句柄 ID。
 */
HandleId getDefaultHandleId();

/**
 * @brief 检查默认数据库连接是否可用。
 */
bool isConnected();

/**
 * @brief 获取默认数据库连接的状态文本。
 *
 * @param status 接收状态文本。
 */
bool getStatus(std::string& status);

/**
 * @brief 获取默认连接最近记录的数据库错误。
 *
 * @param error 接收错误信息。
 * @return 成功读取返回 true，否则返回 false。
 *
 * @note 默认连接不可用或无法读取错误信息时返回 false。
 * @note 错误码为 0 不一定代表查询失败。
 */
bool getLastError(QueryError& error);

/**
 * @brief 获取默认连接尚未完成的异步查询数量。
 */
std::uint32_t unprocessedQueryCount();

/**
 * @brief 执行同步 SQL 查询。
 *
 * 支持 SELECT、INSERT、UPDATE、DELETE 等语句。
 *
 * @param query SQL 语句。
 * @param result 接收查询结果。
 * @param error 可选的错误信息接收对象。
 * @return 查询成功返回 true，否则返回 false。
 *
 * @warning 此操作会阻塞服务器主线程。
 * @note 业务逻辑通常应优先使用 queryAsync()。
 */
bool querySync(std::string query, QueryResult& result, QueryError* error = nullptr);

/**
 * @brief 执行同步 SQL，不保留查询结果。
 *
 * 适用于不需要读取结果集的 SQL 语句。
 *
 * @param query SQL 语句。
 * @param error 可选的错误信息接收对象。
 * @return 执行成功返回 true，否则返回 false。
 *
 * @warning 此操作会阻塞服务器主线程。
 */
bool executeSync(std::string query, QueryError* error = nullptr);

/**
 * @brief 在线程化连接上异步执行 SQL。
 *
 * 查询完成后，根据执行结果调用对应回调。
 *
 * @param query SQL 语句。
 * @param onSuccess 查询成功时调用的回调。
 * @param onError 查询失败时调用的回调。
 * @return 查询成功进入执行队列返回 true，否则返回 false。
 *
 * @note 回调在 OPFramework Core onTick 主线程中执行。
 * @note 请从服务器主线程调用，以符合底层查询队列的约定。
 * @note 返回 false 表示查询未进入执行队列，不会调用任何回调。
 */
bool queryAsync(std::string query, QueryCallback onSuccess = {}, QueryErrorCallback onError = {});

/**
 * @brief 通过默认连接的连接池异步执行 SQL。
 *
 * 查询完成后，根据执行结果调用对应回调。
 *
 * @param query SQL 语句。
 * @param onSuccess 查询成功时调用的回调。
 * @param onError 查询失败时调用的回调。
 * @return 查询成功提交返回 true，否则返回 false。
 *
 * @note 连接池大小为 0 时无法提交查询。
 * @note 回调在 OPFramework Core onTick 主线程中执行。
 */
bool queryParallel(std::string query, QueryCallback onSuccess = {}, QueryErrorCallback onError = {});

/**
 * @brief 转义 SQL 字符串内容。
 *
 * @param input 待转义的原始字符串。
 * @param escaped 接收转义后的字符串。
 * @return 转义成功返回 true，否则返回 false。
 *
 * @note 返回的字符串不包含 SQL 引号。
 */
bool escapeString(const std::string& input, std::string& escaped);

/**
 * @brief 设置默认数据库连接的字符集。
 *
 * 同时设置主连接、线程化连接和连接池中的连接。
 *
 * @param characterSet 字符集名称。
 * @return 设置成功返回 true，否则返回 false。
 */
bool setCharacterSet(const std::string& characterSet);

/**
 * @brief 获取默认数据库连接的字符集。
 *
 * @param characterSet 接收字符集名称。
 */
bool getCharacterSet(std::string& characterSet);

}
