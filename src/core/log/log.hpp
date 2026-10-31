
#pragma once

#include "config.hpp"

#include <spdlog/spdlog.h>

#include <cstdarg>
#include <cstdint>
#include <memory>
#include <string_view>

namespace OPCoreLog
{

/**
 * @brief 日志器句柄。
 */
using Logger = std::shared_ptr<spdlog::logger>;

/**
 * @brief 初始化日志系统。
 *
 * 必须在启动模块和 MySQL 之前调用。
 *
 * @param config 日志系统配置。
 *
 * @note 以下情况会抛出异常：
 *       - 配置无效。
 *       - 日志文件或其他输出目标无法打开。
 *       - 日志系统已经初始化。
 *       - 日志器名称发生冲突。
 *
 * @note 重新初始化后，必须重新获取之前缓存的日志器句柄。
 */
void init(const Config& config = {});

/**
 * @brief 关闭日志系统。
 *
 * 排空尚未处理的日志队列，并等待内部线程退出。
 *
 * @note 可以重复调用。
 * @note 调用前必须确保所有日志生产者（包括 MySQL）已经停止。
 */
void shutdown();

/**
 * @brief 检查日志系统是否已经初始化。
 */
bool initialized();

/**
 * @brief 获取默认日志器句柄。
 *
 * @return 默认日志器句柄。
 *
 * @note 建议在模块初始化时获取并缓存日志器句柄。
 * @note 不会访问 spdlog 的日志器注册表。
 * @note 日志系统初始化前或关闭后，返回同步写入 stderr 的备用日志器。
 */
Logger get();

/**
 * @brief 获取指定名称的日志器句柄。
 *
 * @param name 日志器名称。
 * @return 对应的日志器句柄。
 *
 * @note 建议在模块初始化时获取并缓存日志器句柄。
 * @note 不会访问 spdlog 的日志器注册表。
 * @note 日志系统初始化前或关闭后，返回同步写入 stderr 的备用日志器。
 * @note 日志系统运行期间，未知的日志器名称属于配置错误，会抛出异常。
 */
Logger get(const std::string& name);

/**
 * @brief 设置所有日志通道的输出级别。
 *
 * @param level 日志输出级别。
 *
 * @note 可以在正常的并发日志记录期间调用。
 */
void setLevel(Level level);

/**
 * @brief 设置指定日志通道的输出级别。
 *
 * @param name 日志器名称。
 * @param level 日志输出级别。
 *
 * @note 可以在正常的并发日志记录期间调用。
 */
void setLevel(const std::string& name, Level level);

/**
 * @brief 设置控制台输出级别。
 *
 * @param level 控制台日志输出级别。
 */
void setConsoleLevel(Level level);

/**
 * @brief 等待日志队列处理完成并刷新所有输出目标。
 *
 * 等待此前提交的日志记录全部处理完成，然后刷新所有 sinks。
 *
 * @return 刷新成功返回 true，否则返回 false。
 *
 * @note 此函数提供日志处理完成屏障，不等同于 async_logger::flush()。
 * @note 不保证数据已经通过 fsync 写入物理存储，也不提供崩溃持久性保证。
 * @warning 请从应用程序线程调用，不要从 sink 或日志错误处理器内部调用。
 */
bool flush();

/**
 * @brief 获取日志系统记录的错误数量。
 */
std::uint64_t errorCount() noexcept;

/**
 * @brief 直接记录文本消息。
 *
 * @param level 日志级别。
 * @param message 日志文本。
 *
 * @note message 被作为普通文本数据处理，而不是格式字符串。
 * @note 外部输入的消息应使用此函数记录，不要直接作为格式字符串传递。
 */
void text(Level level, std::string_view message) noexcept;

namespace detail
{

/**
 * @brief 输出紧急错误信息。
 *
 * 供日志系统内部及日志宏在异常情况下使用。
 *
 * @param message 错误信息。
 *
 * @internal
 */
void emergency(const char* message) noexcept;

}

}

// 编译期日志过滤级别，与 SPDLOG_ACTIVE_LEVEL 独立。
// 所有翻译单元应保持一致。
#ifndef OPCORE_LOG_ACTIVE_LEVEL
#  ifdef NDEBUG
#    define OPCORE_LOG_ACTIVE_LEVEL SPDLOG_LEVEL_INFO
#  else
#    define OPCORE_LOG_ACTIVE_LEVEL SPDLOG_LEVEL_DEBUG
#  endif
#endif

/**
 * @brief 日志宏的内部实现。
 *
 * 在求值格式参数之前检查日志级别。
 *
 * @note logger_expr 只会求值一次。
 * @note 使用已缓存的日志器句柄时，不需要查询 spdlog 注册表。
 * @note 日志记录发生异常时，通过 emergency() 输出错误信息。
 */
#define OPCORE_LOG_DETAIL(logger_expr, severity, ...)                              \
    do {                                                                     \
        const auto& op_log_detail_handle_ = (logger_expr);                    \
        if (op_log_detail_handle_ &&                                         \
            op_log_detail_handle_->should_log(severity)) {                   \
            try {                                                            \
                op_log_detail_handle_->log(                                  \
                    spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION},  \
                    severity, __VA_ARGS__);                                  \
            } catch (const std::exception& op_log_detail_error_) {            \
                ::OPCoreLog::detail::emergency(op_log_detail_error_.what());  \
            } catch (...) {                                                  \
                ::OPCoreLog::detail::emergency("unknown log exception");     \
            }                                                                \
        }                                                                    \
    } while (false)

/**
 * @brief 使用指定日志器输出日志。
 *
 * 支持 TRACE、DEBUG、INFO、WARN、ERROR 和 CRITICAL 六个日志级别。
 *
 * @param logger 已获取的日志器句柄。
 * @param ... 日志格式字符串及其参数。
 *
 * @note 同时受到 OPCORE_LOG_ACTIVE_LEVEL 编译期过滤和日志器运行时级别控制。
 * @note 被编译期禁用的日志宏不会求值其参数。
 */
#if OPCORE_LOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
#  define OPCORE_LOGGER_TRACE(logger, ...) OPCORE_LOG_DETAIL(logger, ::spdlog::level::trace, __VA_ARGS__)
#else
#  define OPCORE_LOGGER_TRACE(...) ((void)0)
#endif

#if OPCORE_LOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_DEBUG
#  define OPCORE_LOGGER_DEBUG(logger, ...) OPCORE_LOG_DETAIL(logger, ::spdlog::level::debug, __VA_ARGS__)
#else
#  define OPCORE_LOGGER_DEBUG(...) ((void)0)
#endif

#if OPCORE_LOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_INFO
#  define OPCORE_LOGGER_INFO(logger, ...) OPCORE_LOG_DETAIL(logger, ::spdlog::level::info, __VA_ARGS__)
#else
#  define OPCORE_LOGGER_INFO(...) ((void)0)
#endif

#if OPCORE_LOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_WARN
#  define OPCORE_LOGGER_WARN(logger, ...) OPCORE_LOG_DETAIL(logger, ::spdlog::level::warn, __VA_ARGS__)
#else
#  define OPCORE_LOGGER_WARN(...) ((void)0)
#endif

#if OPCORE_LOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_ERROR
#  define OPCORE_LOGGER_ERROR(logger, ...) OPCORE_LOG_DETAIL(logger, ::spdlog::level::err, __VA_ARGS__)
#else
#  define OPCORE_LOGGER_ERROR(...) ((void)0)
#endif

#if OPCORE_LOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_CRITICAL
#  define OPCORE_LOGGER_CRITICAL(logger, ...) OPCORE_LOG_DETAIL(logger, ::spdlog::level::critical, __VA_ARGS__)
#else
#  define OPCORE_LOGGER_CRITICAL(...) ((void)0)
#endif

/**
 * @brief 使用默认日志器输出日志。
 *
 * 默认日志器通过 OPCoreLog::get() 获取。
 *
 * 支持 TRACE、DEBUG、INFO、WARN、ERROR 和 CRITICAL 六个日志级别。
 *
 * @param ... 日志格式字符串及其参数。
 *
 * @note 同时受到 OPCORE_LOG_ACTIVE_LEVEL 编译期过滤和日志器运行时级别控制。
 * @note 被编译期禁用的日志宏不会求值参数，也不会调用 get()。
 */
#define OPCORE_LOG_TRACE(...) OPCORE_LOGGER_TRACE(::OPCoreLog::get(), __VA_ARGS__)
#define OPCORE_LOG_DEBUG(...) OPCORE_LOGGER_DEBUG(::OPCoreLog::get(), __VA_ARGS__)
#define OPCORE_LOG_INFO(...) OPCORE_LOGGER_INFO(::OPCoreLog::get(), __VA_ARGS__)
#define OPCORE_LOG_WARN(...) OPCORE_LOGGER_WARN(::OPCoreLog::get(), __VA_ARGS__)
#define OPCORE_LOG_ERROR(...) OPCORE_LOGGER_ERROR(::OPCoreLog::get(), __VA_ARGS__)
#define OPCORE_LOG_CRITICAL(...) OPCORE_LOGGER_CRITICAL(::OPCoreLog::get(), __VA_ARGS__)
