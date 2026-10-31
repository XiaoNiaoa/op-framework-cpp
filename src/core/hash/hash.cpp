#include "hash.hpp"

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include <sodium.h>

#include "core/log/log.hpp"
#include "core/module.hpp"
#include "core/pointers.hpp"

static_assert(crypto_pwhash_STRBYTES <= OPCoreHash::kHashCapacity, "kHashCapacity 必须能容纳 crypto_pwhash_STRBYTES");

namespace
{
// OWASP 推荐的 argon2id 最低参数（m = 19 MiB, t = 2, p = 1）。
// 服务器是 32 位进程，内存上限刻意保守；参数会编码进哈希串，日后调整不影响已有账号。
constexpr unsigned long long kOpsLimit = 2;
constexpr std::size_t kMemLimit = 19 * 1024 * 1024;

struct Job
{
    enum class Kind { Hash, Verify };

    Kind kind;
    std::string password;
    std::string hash;
    OPCoreHash::HashCallback onHashed;
    OPCoreHash::VerifyCallback onVerified;
    OPCoreHash::VerifyResult verifyResult;
};

class OPCoreHashModule final
    : public OPCoreModuleManager
    , public CoreEventHandler
{
public:
    static OPCoreHashModule* instance() { return instance_; }

    bool onModuleInit() override
    {
        if (sodium_init() < 0)
        {
            OPCORE_LOG_CRITICAL("[OPCore::Hash] libsodium 初始化失败");
            return false;
        }

        static_assert(
            kOpsLimit >= crypto_pwhash_OPSLIMIT_MIN
            && kMemLimit >= crypto_pwhash_MEMLIMIT_MIN,
            "argon2id 参数低于 libsodium 允许的最小值");

        // 先注册监听。若注册失败，还没有启动工作线程。
        listen(OPCoreSharedPtr::core()->getEventDispatcher(), *this);

        running_ = true;

        try
        {
            worker_ = std::thread([this] { workerLoop(); });
        }
        catch (...)
        {
            running_ = false;
            throw;
        }

        instance_ = this;
        return true;
    }

    bool onModuleExit() override
    {
        instance_ = nullptr;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            running_ = false;
        }

        wake_.notify_all();

        if (worker_.joinable())
            worker_.join();

        for (Job& job : pending_)
            sodium_memzero(job.password.data(), job.password.size());

        pending_.clear();
        finished_.clear();

        return true;
    }

    StringView moduleName() const override { return "OPCore Hash"; }

    bool submit(Job job)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!running_)
            {
                sodium_memzero(job.password.data(), job.password.size());
                return false;
            }
            pending_.push_back(std::move(job));
        }
        wake_.notify_one();
        return true;
    }

    // 主线程：把已完成的任务交还给回调。
    void onTick(Microseconds, TimePoint) override
    {
        std::vector<Job> finished;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (finished_.empty())
                return;
            finished.swap(finished_);
        }

        for (Job& job : finished)
        {
            if (job.kind == Job::Kind::Hash)
                job.onHashed(std::move(job.hash));
            else
                job.onVerified(job.verifyResult);
        }
    }

private:
    inline static OPCoreHashModule* instance_ = nullptr;

    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable wake_;
    bool running_ = false;
    std::deque<Job> pending_;
    std::vector<Job> finished_;

    void workerLoop()
    {
        for (;;)
        {
            Job job;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                wake_.wait(lock, [this] { return !running_ || !pending_.empty(); });
                if (!running_)
                    return;
                job = std::move(pending_.front());
                pending_.pop_front();
            }

            process(job);
            sodium_memzero(job.password.data(), job.password.size());

            std::lock_guard<std::mutex> lock(mutex_);
            finished_.push_back(std::move(job));
        }
    }

    void process(Job& job) const
    {
        if (job.kind == Job::Kind::Hash)
        {
            char out[crypto_pwhash_STRBYTES];
            if (crypto_pwhash_str(out, job.password.data(), job.password.size(), kOpsLimit, kMemLimit) == 0)
                job.hash = out;
            else
                job.hash.clear();
            return;
        }

        job.verifyResult.matched = crypto_pwhash_str_verify(job.hash.c_str(), job.password.data(), job.password.size()) == 0;
        if (job.verifyResult.matched && crypto_pwhash_str_needs_rehash(job.hash.c_str(), kOpsLimit, kMemLimit) != 0)
        {
            char out[crypto_pwhash_STRBYTES];
            if (crypto_pwhash_str(out, job.password.data(), job.password.size(), kOpsLimit, kMemLimit) == 0)
                job.verifyResult.rehash = out;
        }
    }
};

OPCORE_REGISTER_MODULE(OPCoreHashModule);
}

namespace OPCoreHash
{
bool hashAsync(std::string password, HashCallback onDone)
{
    auto* module = OPCoreHashModule::instance();
    if (!module || !onDone)
    {
        wipe(password);
        return false;
    }

    Job job;
    job.kind = Job::Kind::Hash;
    job.password = std::move(password);
    job.onHashed = std::move(onDone);
    return module->submit(std::move(job));
}

bool verifyAsync(std::string password, std::string hash, VerifyCallback onDone)
{
    auto* module = OPCoreHashModule::instance();
    if (!module || !onDone)
    {
        wipe(password);
        return false;
    }

    Job job;
    job.kind = Job::Kind::Verify;
    job.password = std::move(password);
    job.hash = std::move(hash);
    job.onVerified = std::move(onDone);
    return module->submit(std::move(job));
}

void wipe(std::string& secret)
{
    sodium_memzero(secret.data(), secret.size());
    secret.clear();
}
}
