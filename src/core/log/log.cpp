#include "log.hpp"

#include <spdlog/async_logger.h>
#include <spdlog/details/thread_pool.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/stdout_sinks.h>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace OPCoreLog
{
namespace
{
std::atomic<std::uint64_t> errors{0};
constexpr const char* core_name = "opframework";

bool validLevel(Level level)
{
    return level >= Level::trace && level <= Level::off;
}

void requireLevel(Level level)
{
    if (!validLevel(level)) throw std::invalid_argument("invalid logging level");
}

void validate(const Config& config)
{
    requireLevel(config.level);
    requireLevel(config.console_level);
    requireLevel(config.flush_on);
    if (!config.console && !config.file)
        throw std::invalid_argument("enable at least console or file logging");
    if (config.asynchronous && config.queue_size == 0)
        throw std::invalid_argument("queue_size must be positive");
    if (config.flush_interval.count() < 0)
        throw std::invalid_argument("flush_interval cannot be negative");
    if (config.file)
    {
        if (config.max_file_size == 0 || config.max_files > 200000)
            throw std::invalid_argument("invalid file rotation limits");
        auto validFilename = [](const std::string& name)
        {
            const std::filesystem::path path(name);
            return !name.empty() && path == path.filename() &&
                   name != "." && name != "..";
        };
        if (config.directory.empty() || !validFilename(config.filename) ||
            (config.error_file && !validFilename(config.error_filename)))
            throw std::invalid_argument("use directory plus simple log filenames");
        // Conservative ASCII case-insensitive check also protects Windows paths.
        auto folded = [](std::string name)
        {
            for (auto& c : name) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
            return name;
        };
        if (config.error_file && folded(config.filename) == folded(config.error_filename))
            throw std::invalid_argument("main and error files must be different");
        if (config.error_file)
        {
            // An error filename such as opframework.1.log must not be overwritten
            // by the main sink's rotation (and vice versa).
            using RotatingSink = spdlog::sinks::rotating_file_sink_mt;
            for (std::size_t i = 1; i <= config.max_files; ++i)
                if (folded(RotatingSink::calc_filename(config.filename, i)) == folded(config.error_filename) ||
                    folded(RotatingSink::calc_filename(config.error_filename, i)) == folded(config.filename))
                    throw std::invalid_argument("main and error rotation filenames overlap");
        }
    }
    std::unordered_set<std::string> names{core_name};
    for (const auto& channel : config.channels)
    {
        requireLevel(channel.level);
        if (channel.name.empty() || !names.insert(channel.name).second)
            throw std::invalid_argument("empty or duplicate logger name");
    }
    // Never adopt/overwrite a foreign logger, especially an already-created CLog.
    for (const auto& name : names)
        if (spdlog::get(name))
            throw std::logic_error("logger already registered: " + name);
}

bool flushSinks(const std::vector<spdlog::sink_ptr>& sinks) noexcept
{
    bool ok = true;
    for (const auto& sink : sinks)
    {
        try { sink->flush(); }
        catch (const std::exception& e) { detail::emergency(e.what()); ok = false; }
        catch (...) { detail::emergency("unknown flush failure"); ok = false; }
    }
    return ok;
}

// This sink is ONLY used by a private marker logger. With one worker and block
// overflow policy its flush marker cannot overtake or evict earlier records.
class FlushBarrier final : public spdlog::sinks::sink
{
public:
    explicit FlushBarrier(std::vector<spdlog::sink_ptr> sinks) : sinks_(std::move(sinks)) {}
    void log(const spdlog::details::log_msg&) override {}
    void set_pattern(const std::string&) override {}
    void set_formatter(std::unique_ptr<spdlog::formatter>) override {}

    void flush() override
    {
        const bool ok = flushSinks(sinks_);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ok_ = ok;
            ++completed_;
        }
        changed_.notify_all();
    }

    std::uint64_t next()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return completed_ + 1;
    }

    bool wait(std::uint64_t ticket)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        changed_.wait(lock, [&] { return completed_ >= ticket; });
        return ok_;
    }
private:
    std::vector<spdlog::sink_ptr> sinks_;
    std::mutex mutex_;
    std::condition_variable changed_;
    std::uint64_t completed_ = 0;
    bool ok_ = true;
};

struct Runtime
{
    explicit Runtime(const Config& config)
    {
        if (config.console)
        {
            console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            console->set_level(config.console_level);
            sinks.push_back(console);
        }
        if (config.file)
        {
            const std::filesystem::path directory(config.directory);
            std::filesystem::create_directories(directory);
            sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                (directory / config.filename).string(), config.max_file_size, config.max_files));
            if (config.error_file)
            {
                auto sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                    (directory / config.error_filename).string(), config.max_file_size, config.max_files);
                sink->set_level(Level::err);
                sinks.push_back(std::move(sink));
            }
        }
        for (const auto& sink : sinks) sink->set_pattern(config.pattern);

        if (config.asynchronous)
            pool = std::make_shared<spdlog::details::thread_pool>(config.queue_size, 1);

        auto addLogger = [&](const std::string& name, Level level)
        {
            Logger logger;
            if (pool)
                logger = std::make_shared<spdlog::async_logger>(name,
                    sinks.begin(), sinks.end(), pool, spdlog::async_overflow_policy::block);
            else
                logger = std::make_shared<spdlog::logger>(name, sinks.begin(), sinks.end());
            logger->set_level(level);
            logger->flush_on(config.flush_on);
            logger->set_error_handler([](const std::string& error) { detail::emergency(error.c_str()); });
            loggers.emplace(name, std::move(logger));
        };
        addLogger(core_name, config.level);
        for (const auto& channel : config.channels) addLogger(channel.name, channel.level);
        if (pool)
        {
            barrier = std::make_shared<FlushBarrier>(sinks);
            marker = std::make_shared<spdlog::async_logger>("opframework.private.flush",
                barrier, pool, spdlog::async_overflow_policy::block);
            // Posting a marker must throw if it fails; otherwise waiting would hang.
            marker->set_error_handler([](const std::string& error) { throw std::runtime_error(error); });
        }
    }

    ~Runtime() { stop(); }

    bool flush() noexcept
    {
        try
        {
            std::lock_guard<std::mutex> lock(flush_mutex);
            if (!pool) return flushSinks(sinks);
            const auto ticket = barrier->next();
            marker->flush();
            return barrier->wait(ticket);
        }
        catch (const std::exception& e) { detail::emergency(e.what()); }
        catch (...) { detail::emergency("flush barrier failed"); }
        return false;
    }

    void startTimer(std::chrono::milliseconds interval)
    {
        if (interval.count() == 0) return;
        timer = std::thread([this, interval]
        {
            std::unique_lock<std::mutex> lock(timer_mutex);
            while (!timer_changed.wait_for(lock, interval, [&] { return stopping; }))
            {
                lock.unlock();
                flush();
                lock.lock();
            }
        });
    }

    void stop() noexcept
    {
        {
            std::lock_guard<std::mutex> lock(timer_mutex);
            stopping = true;
        }
        timer_changed.notify_all();
        if (timer.joinable()) timer.join();
        flush();
        // No producer may be running here. Retained handles become disabled;
        // reacquire them after any subsequent init().
        for (const auto& entry : loggers) entry.second->set_level(Level::off);
        pool.reset(); // Joins the private worker. Does not touch spdlog's global pool.
        // Cached disabled handles must not retain open files across reinit,
        // especially on Windows where this can prevent the next rotation.
        for (const auto& entry : loggers) entry.second->sinks().clear();
    }

    std::shared_ptr<spdlog::details::thread_pool> pool;
    std::vector<spdlog::sink_ptr> sinks;
    spdlog::sink_ptr console;
    std::unordered_map<std::string, Logger> loggers;
    std::shared_ptr<FlushBarrier> barrier;
    Logger marker;
    Logger previous_default;
    bool default_installed = false;
    std::mutex flush_mutex;
    std::mutex timer_mutex;
    std::condition_variable timer_changed;
    bool stopping = false;
    std::thread timer;
};

struct Service
{
    std::mutex mutex;
    std::unique_ptr<Runtime> runtime;
    Logger core; // Access ONLY through C++17 atomic shared_ptr operations.
};

Service& service()
{
    static Service instance;
    return instance;
}

Logger fallback()
{
    static const Logger logger = []
    {
        auto sink = std::make_shared<spdlog::sinks::stderr_sink_mt>();
        auto value = std::make_shared<spdlog::logger>("opframework.bootstrap", sink);
        value->set_pattern("[%l] [%n] %v");
        value->set_level(Level::info);
        value->flush_on(Level::info);
        value->set_error_handler([](const std::string& error) { detail::emergency(error.c_str()); });
        return value;
    }();
    return logger;
}

void unregisterOwned(Runtime& runtime)
{
    const auto core = runtime.loggers.at(core_name);
    if (runtime.default_installed && spdlog::default_logger() == core)
        spdlog::set_default_logger(runtime.previous_default);
    for (const auto& entry : runtime.loggers)
        if (spdlog::get(entry.first) == entry.second) spdlog::drop(entry.first);
}
}

namespace detail
{
void emergency(const char* message) noexcept
{
    const auto count = errors.fetch_add(1, std::memory_order_relaxed) + 1;
    // Bound stderr noise on a persistent disk failure; every failure is counted.
    if (count <= 3 || (count & (count - 1)) == 0)
    {
        std::fprintf(stderr, "[OPCoreLog internal error #%llu] %s\n",
            static_cast<unsigned long long>(count), message ? message : "unknown");
        std::fflush(stderr);
    }
}
}

void init(const Config& config)
{
    auto& owner = service();
    std::lock_guard<std::mutex> lock(owner.mutex);
    if (owner.runtime) throw std::logic_error("OPCoreLog already initialized");
    validate(config);
    auto runtime = std::make_unique<Runtime>(config);
    runtime->previous_default = spdlog::default_logger();
    try
    {
        for (const auto& entry : runtime->loggers) spdlog::register_logger(entry.second);
        if (config.install_as_default)
        {
            spdlog::set_default_logger(runtime->loggers.at(core_name));
            runtime->default_installed = true;
        }
        runtime->startTimer(config.flush_interval);
    }
    catch (...)
    {
        unregisterOwned(*runtime);
        throw;
    }
    std::atomic_store(&owner.core, runtime->loggers.at(core_name));
    owner.runtime = std::move(runtime);
}

void shutdown()
{
    auto& owner = service();
    std::lock_guard<std::mutex> lock(owner.mutex);
    if (!owner.runtime) return;
    std::atomic_store(&owner.core, Logger{});
    unregisterOwned(*owner.runtime);
    owner.runtime.reset(); // Stops timer, flushes and joins the queue worker.
}

bool initialized()
{
    return static_cast<bool>(std::atomic_load(&service().core));
}

Logger get()
{
    auto value = std::atomic_load(&service().core);
    return value ? value : fallback();
}

Logger get(const std::string& name)
{
    auto& owner = service();
    std::lock_guard<std::mutex> lock(owner.mutex);
    if (!owner.runtime) return fallback();
    const auto it = owner.runtime->loggers.find(name);
    if (it == owner.runtime->loggers.end()) throw std::invalid_argument("unknown logger: " + name);
    return it->second;
}

void setLevel(Level level)
{
    requireLevel(level);
    auto& owner = service();
    std::lock_guard<std::mutex> lock(owner.mutex);
    if (!owner.runtime) throw std::logic_error("OPCoreLog is not initialized");
    for (const auto& entry : owner.runtime->loggers) entry.second->set_level(level);
}

void setLevel(const std::string& name, Level level)
{
    requireLevel(level);
    auto& owner = service();
    std::lock_guard<std::mutex> lock(owner.mutex);
    if (!owner.runtime) throw std::logic_error("OPCoreLog is not initialized");
    owner.runtime->loggers.at(name)->set_level(level);
}

void setConsoleLevel(Level level)
{
    requireLevel(level);
    auto& owner = service();
    std::lock_guard<std::mutex> lock(owner.mutex);
    if (!owner.runtime) throw std::logic_error("OPCoreLog is not initialized");
    if (owner.runtime->console) owner.runtime->console->set_level(level);
}

bool flush()
{
    auto& owner = service();
    std::lock_guard<std::mutex> lock(owner.mutex);
    return owner.runtime ? owner.runtime->flush() : true;
}

std::uint64_t errorCount() noexcept { return errors.load(std::memory_order_relaxed); }

void text(Level level, std::string_view message) noexcept
{
    try { get()->log(level, "{}", message); }
    catch (const std::exception& e) { detail::emergency(e.what()); }
    catch (...) { detail::emergency("text logging failed"); }
}
}
