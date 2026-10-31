#include <cstdlib>
#include <filesystem>
#include <memory>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>

#include <Server/Components/Console/console.hpp>

#include "component.hpp"
#include "module.hpp"
#include "pointers.hpp"
#include "mysql/main.hpp"
#include "log/log.hpp"


// --- 组件对象与基本信息 --- 

OPFrameworkComponent* OPFrameworkComponent::getInstance()
{
    if (instance_ == nullptr)
    {
        instance_ = new OPFrameworkComponent();
    }

    return instance_;
}

StringView OPFrameworkComponent::componentName() const
{
    return "OPFramework";
}

SemanticVersion OPFrameworkComponent::componentVersion() const
{
    return SemanticVersion(1, 0, 1, 1);
}

// --- 配置准备阶段 --- 

void OPFrameworkComponent::provideConfiguration(ILogger&, IEarlyConfig& config, bool)
{
    OPCoreMySQL::SetDefaultConfig(config);
}

// --- 启动阶段：onLoad -> onInit -> onReady --- 

void OPFrameworkComponent::onLoad(ICore* core)
{
    OPCoreSharedPtr::initCoreSharedPtr(core);

    // Log
    try
    {
        OPCoreLog::init();
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "OPFramework log init failed: %s\n", e.what());
        std::abort();
    }

    if (!OPCoreMySQL::Load())
    {
        OPCORE_LOG_CRITICAL("MySQL library initialization failed");
        OPCoreLog::flush();
        std::abort();
    }
    // Log end

    core->getEventDispatcher().addEventHandler(this);
}

void OPFrameworkComponent::onInit(IComponentList* components)
{
    OPCoreSharedPtr::initSharedPtr(components);

    if (auto* pawn = OPCoreSharedPtr::pawn())
    {
        pawn->getEventDispatcher().addEventHandler(this);

        //除了监听后续脚本加载事件，也必须为当前已存在的 AMX 注册 natives。
        if (auto* script = pawn->mainScript())
            onAmxLoad(*script);

        for (auto* script : pawn->sideScripts())
            onAmxLoad(*script);
    }
}

void OPFrameworkComponent::onReady()
{
    // 创建所有模块实例
    if (OPCoreModuleManager::loadModule())
        return;

    OPCORE_LOG_CRITICAL(
        "OPFramework 模块加载失败，正在请求关闭服务器。");

    if (auto* console = OPCoreSharedPtr::console())
    {
        console->send("exit");
        return;
    }

    // 无法取得 Console 组件时，已经没有可用的正常关服入口。
    std::abort();
}

// --- 运行阶段：Core 与 Pawn 事件 --- 

void OPFrameworkComponent::onTick(Microseconds, TimePoint)
{
    OPCoreMySQL::ProcessTick();
}

void OPFrameworkComponent::onAmxLoad(IPawnScript& script)
{
    OPCoreSharedPtr::registerPawnNatives(script.GetAMX());
    OPCoreMySQL::AmxLoad(script.GetAMX());
}

void OPFrameworkComponent::onAmxUnload(IPawnScript& script)
{
    OPCoreMySQL::AmxUnload(script.GetAMX());
}

// --- GMX / Game Mode 重置

void OPFrameworkComponent::reset()
{
    // GMX 只重置业务模块，不关闭底层库或默认数据库连接。
    OPCoreModuleManager::resetModule();
}

// --- 组件依赖释放与自身卸载 --- 

void OPFrameworkComponent::onFree(IComponent* component)
{
    OPCoreModuleManager::onComponentFree(component);

    if (auto* pawn = OPCoreSharedPtr::pawn(); pawn && component == pawn)
    {
        OPCoreMySQL::DetachScripts();
        pawn->getEventDispatcher().removeEventHandler(this);
    }

    OPCoreSharedPtr::freeSharedPtr(component);
}

void OPFrameworkComponent::free()
{
    OPCoreModuleManager::unloadModule();

    OPCoreMySQL::disconnectDefaultDatabase();

    delete this;
}

OPFrameworkComponent::~OPFrameworkComponent()
{
    if (auto* core = OPCoreSharedPtr::core())
        core->getEventDispatcher().removeEventHandler(this);

    if (auto* pawn = OPCoreSharedPtr::pawn())
    {
        pawn->getEventDispatcher().removeEventHandler(this);
    }

    OPCoreMySQL::Unload();
    OPCoreLog::shutdown();
    OPCoreSharedPtr::freeSharedPtr();
    instance_ = nullptr;
}