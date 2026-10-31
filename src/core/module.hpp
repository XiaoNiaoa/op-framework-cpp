
#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
#include <exception>

#include <spdlog/spdlog.h>
#include <events.hpp>

#include "pointers.hpp"

class OPFrameworkComponent;

/**
 * @brief OPFramework 模块基类及模块注册管理器。
 *
 * 每个游戏或框架模块继承此类，通过 OPCORE_REGISTER_MODULE() 注册。
 *
 * 模块管理器负责：
 * - 创建所有已注册的模块实例。
 * - 执行模块初始化和就绪回调。
 * - 管理模块注册的事件监听器。
 * - 处理模块重置和卸载。
 *
 * 模块之间不应依赖初始化调用顺序。
 */
class OPCoreModuleManager
{
private:
    /**
     * @brief 模块注册的事件监听记录。
     *
     * owner 为空时，监听器由模块自身的生命周期管理。
     * owner 非空时，所属组件释放前会自动移除对应监听器。
     */
    struct EventSubscription
    {
        IComponent* owner;
        std::function<void()> unsubscribe;
    };

    std::vector<EventSubscription> eventSubscriptions_;

protected:
    /**
     * @brief 注册事件监听器。
     *
     * 监听器由当前模块持有，模块卸载时自动移除。
     *
     * @tparam Handler 事件处理接口类型。
     * @tparam ModuleType 当前模块类型。
     * @param dispatcher 事件分发器。
     * @param module 实现了 Handler 的模块实例。
     * @param priority 事件监听优先级，不影响模块加载顺序。
     *
     * @note dispatcher 必须在监听器移除前保持有效。
     */
    template <typename Handler, typename ModuleType>
    void listen(
        IEventDispatcher<Handler>& dispatcher,
        ModuleType& module,
        event_order_t priority = EventPriority_Default)
    {
        static_assert(
            std::is_base_of_v<Handler, ModuleType>,
            "ModuleType必须实现事件处理程序接口");

        auto* handler = static_cast<Handler*>(&module);

        eventSubscriptions_.push_back({
            nullptr,
            [&dispatcher, handler]()
            {
                dispatcher.removeEventHandler(handler);
            }
        });

        try
        {
            dispatcher.addEventHandler(handler, priority);
        }
        catch (...)
        {
            eventSubscriptions_.pop_back();
            throw;
        }
    }

    /**
     * @brief 注册依赖指定组件的事件监听器。
     *
     * 当 owner 对应的组件即将释放时，自动移除该监听器。
     *
     * @tparam Handler 事件处理接口类型。
     * @tparam ModuleType 当前模块类型。
     * @param owner 提供事件分发器的组件。
     * @param dispatcher 事件分发器。
     * @param module 实现了 Handler 的模块实例。
     * @param priority 事件监听优先级。
     *
     * @note owner 必须与 dispatcher 的生命周期相匹配。
     */
    template <typename Handler, typename ModuleType>
    void listen(
        IComponent& owner,
        IEventDispatcher<Handler>& dispatcher,
        ModuleType& module,
        event_order_t priority = EventPriority_Default)
    {
        static_assert(
            std::is_base_of_v<Handler, ModuleType>,
            "ModuleType必须实现事件处理程序接口");

        auto* handler = static_cast<Handler*>(&module);

        eventSubscriptions_.push_back({
            &owner,
            [&dispatcher, handler]()
            {
                dispatcher.removeEventHandler(handler);
            }
        });

        try
        {
            dispatcher.addEventHandler(handler, priority);
        }
        catch (...)
        {
            eventSubscriptions_.pop_back();
            throw;
        }
    }

public:
    /**
     * @brief 模块实例工厂。
     *
     * 负责创建一个模块实例。
     */
    using Factory = std::function<std::unique_ptr<OPCoreModuleManager>()>;

    OPCoreModuleManager() = default;

    /**
     * @brief 模块析构函数。
     *
     * 确保模块销毁时移除尚未解除的事件监听器。
     */
    virtual ~OPCoreModuleManager()
    {
        removeEventHandlers();
    }

    /**
     * @brief 初始化当前模块自身。
     *
     * 所有模块实例创建完成后，模块管理器会依次调用此函数。
     *
     * 适合初始化模块自己拥有的资源，例如数据结构、线程和内部状态。
     *
     * @return 初始化成功返回 true，失败返回 false。
     *
     * @note 此阶段不应依赖其他游戏模块已经完成初始化。
     * @note 如果初始化失败，模块必须自行清理已经申请的资源。
     * @note 模块之间的事件监听和业务联系应放在 onAllModulesReady() 中建立。
     */
    virtual bool onModuleInit()
    {
        return true;
    }

    /**
     * @brief 某个模块即将释放时调用。
     *
     * 用于处理当前模块与目标模块之间的依赖关系。
     *
     * @param module 即将释放的模块。
     */
    virtual void onModuleFree(OPCoreModuleManager* module)
    {
    }

    /**
     * @brief 关闭当前模块并释放自身资源。
     *
     * 只会对成功完成 onModuleInit() 的模块调用。
     *
     * 模块管理器会在调用此函数前移除该模块注册的全部事件监听器。
     *
     * @return 清理成功返回 true，否则返回 false。
     *
     * @note 即使 onAllModulesReady() 尚未执行或执行失败，也可能调用此函数。
     *       模块应能正确处理这种情况。
     */
    virtual bool onModuleExit()
    {
        return true;
    }

    /**
     * @brief 所有模块完成初始化后的就绪回调。
     *
     * 只有全部模块的 onModuleInit() 成功执行后，
     * 模块管理器才会调用此函数。
     *
     * 适合建立模块之间的联系，例如：
     * - 注册其他模块的事件监听器。
     * - 获取和缓存其他模块提供的公开 API。
     * - 初始化需要其他模块配合的业务功能。
     *
     * @return 就绪成功返回 true，失败返回 false。
     *
     * @note 调用此函数时，所有模块实例均已创建并完成自身初始化。
     * @note 各模块的 onAllModulesReady() 仍然是依次调用的，
     *       不应假设其他模块已经执行完自己的就绪回调。
     */
    virtual bool onAllModulesReady()
    {
        return true;
    }

    /**
     * @brief 重置当前模块的游戏业务状态。
     *
     * 在 Game Mode Reset 等场景下调用。
     *
     * @note 组件和模块实例仍然存在，不应在这里重复初始化模块。
     * @note 底层资源通常应继续保留，例如数据库连接和工作线程。
     */
    virtual void onModuleReset()
    {
    }

    /**
     * @brief 获取模块名称。
     *
     * 用于日志输出和模块识别。
     */
    virtual StringView moduleName() const
    {
        return "UnnamedModule";
    }

    /**
     * @brief 获取全局模块注册表。
     *
     * 由 OPCORE_REGISTER_MODULE() 在静态初始化阶段注册模块工厂。
     *
     * @note 不保证不同源文件之间的静态注册顺序。
     */
    static std::vector<Factory>& getRegister()
    {
        static std::vector<Factory> factories;
        return factories;
    }

    /**
     * @brief 注册模块工厂。
     *
     * @param factory 用于创建模块实例的工厂函数。
     * @return 注册成功返回 true。
     */
    static bool registerFactory(Factory factory)
    {
        getRegister().push_back(std::move(factory));
        return true;
    }

private:
    friend class OPFrameworkComponent;

    /// 当前组件实例所管理的全部模块。
    inline static std::vector<std::unique_ptr<OPCoreModuleManager>> modules_;

    /// 已成功完成 onModuleInit() 的模块数量。
    inline static std::size_t initializedModuleCount_ = 0;

    /// 模块加载只允许尝试一次；失败后由组件关闭流程回收，不支持原地重试。
    inline static bool loadAttempted_ = false;
    
    /**
     * @brief 移除模块注册的事件监听器。
     *
     * @param owner 指定组件时，只移除该组件关联的监听器；
     *              为空时，移除当前模块的全部监听器。
     */
    void removeEventHandlers(IComponent* owner = nullptr)
    {
        auto it = eventSubscriptions_.begin();

        while (it != eventSubscriptions_.end())
        {
            if (owner != nullptr && it->owner != owner)
            {
                ++it;
                continue;
            }

            auto unsubscribe = std::move(it->unsubscribe);
            it = eventSubscriptions_.erase(it);

            try
            {
                unsubscribe();
            }
            catch (const std::exception& e)
            {
                spdlog::error(
                    "[OPCore::Module] 移除事件监听器时发生异常：{}",
                    e.what());
            }
            catch (...)
            {
                spdlog::error(
                    "[OPCore::Module] 移除事件监听器时发生未知异常。");
            }
        }
    }

    /**
     * @brief 处理外部组件释放。
     *
     * 在组件失效前，移除模块注册到该组件事件分发器的监听器。
     */
    static void onComponentFree(IComponent* component)
    {
        for (const auto& module : modules_)
        {
            module->removeEventHandlers(component);
        }
    }

    /**
     * @brief 创建并加载所有模块。
     *
     * 执行顺序：
     * 1. 创建全部模块实例。
     * 2. 依次调用全部模块的 onModuleInit()。
     * 3. 所有初始化成功后，依次调用全部模块的 onAllModulesReady()。
     *
     * @return 所有模块加载成功返回 true，否则返回 false。
     *
     * @note 不保证模块之间的初始化或就绪回调顺序。
     * @note 初始化或就绪失败时，保留已经创建的模块实例，
     *       由服务器正常卸载流程负责清理。
     */
    static bool loadModule()
    {
        if (loadAttempted_)
        {
            spdlog::critical(
                "[OPCore::Module] 模块加载只能执行一次，拒绝重复加载。");
            return false;
        }

        loadAttempted_ = true;

        const auto& factories = getRegister();
        const std::size_t totalModules = factories.size();

        initializedModuleCount_ = 0;

        spdlog::info(
            "[OPCore::Module] 正在创建 {} 个模块……",
            totalModules);

        // 第一阶段：创建所有模块实例。
        try
        {
            modules_.reserve(totalModules);
        }
        catch (const std::exception& e)
        {
            spdlog::critical(
                "[OPCore::Module] 无法为模块实例分配空间：{}",
                e.what());
            return false;
        }

        for (const auto& factory : factories)
        {
            try
            {
                auto module = factory();

                if (!module)
                {
                    spdlog::critical(
                        "[OPCore::Module] 模块工厂未能创建有效实例，服务器启动已中止。");
                    return false;
                }

                modules_.push_back(std::move(module));
            }
            catch (const std::exception& e)
            {
                spdlog::critical(
                    "[OPCore::Module] 创建模块实例时发生异常：{}",
                    e.what());
                return false;
            }
            catch (...)
            {
                spdlog::critical(
                    "[OPCore::Module] 创建模块实例时发生未知异常。");
                return false;
            }
        }

        // 第二阶段：每个模块初始化自身。
        for (const auto& module : modules_)
        {
            const StringView name = module->moduleName();
            const std::string moduleName(name.data(), name.length());

            bool initialized = false;

            try
            {
                initialized = module->onModuleInit();
            }
            catch (const std::exception& e)
            {
                spdlog::critical(
                    "[OPCore::Module] 模块 {} 初始化时发生异常：{}",
                    moduleName, e.what());
                return false;
            }
            catch (...)
            {
                spdlog::critical(
                    "[OPCore::Module] 模块 {} 初始化时发生未知异常。",
                    moduleName);
                return false;
            }

            if (!initialized)
            {
                spdlog::critical(
                    "[OPCore::Module] 模块 {} 初始化失败，服务器启动已中止。",
                    moduleName);
                return false;
            }

            // 只记录成功完成初始化的模块。
            ++initializedModuleCount_;

            spdlog::info(
                "[OPCore::Module] 模块 {} 初始化成功",
                moduleName);
        }

        // 第三阶段：所有模块初始化完成，建立模块之间的联系。
        for (const auto& module : modules_)
        {
            const StringView name = module->moduleName();
            const std::string moduleName(name.data(), name.length());

            bool ready = false;

            try
            {
                ready = module->onAllModulesReady();
            }
            catch (const std::exception& e)
            {
                spdlog::critical(
                    "[OPCore::Module] 模块 {} 就绪时发生异常：{}",
                    moduleName, e.what());
                return false;
            }
            catch (...)
            {
                spdlog::critical(
                    "[OPCore::Module] 模块 {} 就绪时发生未知异常。",
                    moduleName);
                return false;
            }

            if (!ready)
            {
                spdlog::critical(
                    "[OPCore::Module] 模块 {} 就绪失败，服务器启动已中止。",
                    moduleName);
                return false;
            }
        }

        spdlog::info(
            "[OPCore::Module] 模块加载完成：{}/{} 成功",
            initializedModuleCount_,
            totalModules);

        return true;
    }

    /**
     * @brief 卸载所有模块。
     *
     * 按模块注册顺序的逆序释放模块。
     * 释放每个模块前，先通知所有仍然存在的模块。
     *
     * 只对成功完成 onModuleInit() 的模块调用生命周期回调。
     */
    static void unloadModule()
    {
        const std::size_t totalModules = modules_.size();
        const std::size_t initializedCount = initializedModuleCount_;

        std::size_t unloadedModuleCount = 0;

        spdlog::info(
            "[OPCore::Module] 正在清理 {} 个模块……",
            initializedCount);

        // 尚未完成初始化的模块不执行生命周期回调，直接销毁。
        while (modules_.size() > initializedCount)
        {
            modules_.pop_back();
        }

        // 逐个释放模块。
        while (!modules_.empty())
        {
            OPCoreModuleManager* target = modules_.back().get();

            const StringView name = target->moduleName();
            const std::string moduleName(name.data(), name.length());

            // 通知所有仍然存在的模块，target 即将释放。
            // 包括 target 自身，与 open.mp 的组件释放机制保持一致。
            for (const auto& module : modules_)
            {
                const StringView dependentName = module->moduleName();
                const std::string dependentModuleName(
                    dependentName.data(), dependentName.length());

                try
                {
                    module->onModuleFree(target);
                }
                catch (const std::exception& e)
                {
                    spdlog::error(
                        "[OPCore::Module] 模块 {} 处理模块 {} 释放通知时发生异常：{}",
                        dependentModuleName, moduleName, e.what());
                }
                catch (...)
                {
                    spdlog::error(
                        "[OPCore::Module] 模块 {} 处理模块 {} 释放通知时发生未知异常。",
                        dependentModuleName, moduleName);
                }
            }

            // 移除目标模块注册的事件监听器。
            target->removeEventHandlers();

            // 清理目标模块自身资源。
            bool exited = false;

            try
            {
                exited = target->onModuleExit();
            }
            catch (const std::exception& e)
            {
                spdlog::error(
                    "[OPCore::Module] 模块 {} 退出时发生异常：{}",
                    moduleName, e.what());
            }
            catch (...)
            {
                spdlog::error(
                    "[OPCore::Module] 模块 {} 退出时发生未知异常。",
                    moduleName);
            }

            if (exited)
            {
                ++unloadedModuleCount;

                spdlog::info(
                    "[OPCore::Module] 模块 {} 清理成功",
                    moduleName);
            }
            else
            {
                spdlog::error(
                    "[OPCore::Module] 模块 {} 清理失败！",
                    moduleName);
            }

            // 销毁当前模块实例。
            modules_.pop_back();
            --initializedModuleCount_;
        }

        spdlog::info(
            "[OPCore::Module] 模块清理完成：{}/{} 成功，创建实例：{}",
            unloadedModuleCount,
            initializedCount,
            totalModules);
    }

    /**
     * @brief 重置所有模块。
     *
     * 只调用模块的 onModuleReset()，不销毁模块实例或重新初始化资源。
     */
    static void resetModule()
    {
        for (const auto& module : modules_)
        {
            const StringView name = module->moduleName();
            const std::string moduleName(name.data(), name.length());

            try
            {
                module->onModuleReset();
            }
            catch (const std::exception& e)
            {
                spdlog::error(
                    "[OPCore::Module] 模块 {} 重置时发生异常：{}",
                    moduleName, e.what());
            }
            catch (...)
            {
                spdlog::error(
                    "[OPCore::Module] 模块 {} 重置时发生未知异常。",
                    moduleName);
            }
        }
    }
};

/**
 * @brief 注册一个 OPFramework 模块。
 *
 * 为指定类型自动生成模块工厂，并加入全局注册表。
 *
 * @param ModuleType 继承 OPCoreModuleManager 的具体模块类型。
 *
 * @note 通常在模块实现文件中使用一次。
 */
#define OPCORE_DETAIL_JOIN_IMPL(left, right) left##right
#define OPCORE_DETAIL_JOIN(left, right) OPCORE_DETAIL_JOIN_IMPL(left, right)

#define OPCORE_REGISTER_MODULE(ModuleType) \
    static_assert(std::is_base_of_v<OPCoreModuleManager, ModuleType>, \
        #ModuleType " 必须继承自 OPCoreModuleManager"); \
    namespace { \
        [[maybe_unused]] const bool OPCORE_DETAIL_JOIN(opcore_module_registered_, __COUNTER__) = \
            OPCoreModuleManager::registerFactory([]() -> std::unique_ptr<OPCoreModuleManager> { \
                return std::make_unique<ModuleType>(); \
            }); \
    }
