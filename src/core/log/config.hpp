
#pragma once

#include <spdlog/common.h>

#include <chrono>
#include <cstddef>
#include <string>
#include <vector>

namespace OPCoreLog
{

/**
 * @brief 日志输出级别。
 *
 * 使用 spdlog 提供的日志级别。
 */
using Level = spdlog::level::level_enum;

/**
 * @brief 单个日志通道的配置。
 *
 * 日志通道用于区分不同模块产生的日志，例如 mysql、module。
 */
struct ChannelConfig
{
    std::string name; ///< 日志通道名称，对应 OPCoreLog::get(name)。
    Level level = Level::info; ///< 该通道的最低输出级别。
};

/**
 * @brief 日志系统配置。
 *
 * 集中管理日志输出、异步处理、文件轮转和日志格式。
 *
 * 所有路径均相对于服务器进程的当前工作目录。
 */
struct Config
{
    // 异步日志

    bool asynchronous = true; ///< 是否启用异步日志。
    std::size_t queue_size = 8192; ///< 异步日志队列容量，单位为条日志记录。

    /**
     * @note 异步模式使用独立的工作线程处理日志。
     * @note 队列采用有界阻塞策略，队列满时生产者等待空位，不丢弃日志。
     * @note 以上配置仅在 asynchronous 为 true 时生效。
     */

    // 控制台输出

    bool console = true; ///< 是否启用控制台日志输出。
    Level console_level = Level::info; ///< 控制台允许输出的最低日志级别。

    // 文件输出

    bool file = true; ///< 是否启用普通日志文件。
    bool error_file = true; ///< 是否启用独立的错误日志文件。

    std::string directory = "logs"; ///< 日志文件存放目录。
    std::string filename = "opframework.log"; ///< 普通日志文件名。
    std::string error_filename = "opframework.error.log"; ///< 错误日志文件名。

    std::size_t max_file_size = 10 * 1024 * 1024; ///< 单个日志文件的最大容量，单位为字节。
    std::size_t max_files = 5; ///< 保留的轮转备份文件数量，不包含当前正在写入的文件。

    // 日志级别

    Level level = Level::info; ///< 默认日志级别。
    Level flush_on = Level::err; ///< 达到此级别或更高级别时自动刷新日志。

    // 定时刷新

    std::chrono::milliseconds flush_interval{3000}; ///< 自动刷新间隔，单位为毫秒；0 表示禁用定时刷新。

    // 日志格式

    std::string pattern = "[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%n] %v";

    /**
     * @note 使用 spdlog 的日志格式语法。
     *
     * 默认格式示例：
     *
     * [2026-10-30 21:30:15.123] [info] [mysql] [t:1234] Connected.
     *
     * 常用格式标记：
     * %Y  年份
     * %m  月份
     * %d  日期
     * %H  小时
     * %M  分钟
     * %S  秒
     * %e  毫秒
     * %l  日志级别
     * %n  日志器名称
     * %t  线程 ID
     * %v  日志消息
     * %^  开始颜色标记
     * %$  结束颜色标记
     */

    // 日志通道

    std::vector<ChannelConfig> channels{
        {"mysql", Level::err},
        {"module", Level::info}
    }; ///< 预先注册的日志通道及其独立日志级别。

    // spdlog 默认日志器

    bool install_as_default = true; ///< 是否将本日志系统安装为 spdlog 的默认日志器。

    /**
     * @note 启用后，现有的 spdlog::info()、spdlog::error() 等调用将使用本日志系统。
     * @note 禁用后，不替换 spdlog 原有的默认日志器。
     * @warning 初始化和关闭日志系统时，必须确保所有日志生产者均已停止。
     */
};

}
