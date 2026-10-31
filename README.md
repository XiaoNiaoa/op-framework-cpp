# OPFramework CPP

一个简单的 open.mp C++ 组件项目底子，适合想自己编写组件、插件和服务器功能的开发者参考，也可以直接 Fork 后按自己的需求修改。

## 项目说明

- 使用 C++17 和 CMake。
- 本项目集成并迁移了 [SA-MP-MySQL R41-4](https://github.com/pBlueG/SA-MP-MySQL) 的 C++ 实现，保留原有 MySQL 插件接口，并集成至 open.mp 组件中，原始代码版权及许可证信息请参阅对应的上游项目。感谢 BlueG 及相关贡献者的工作。
- > MySQL 是基于 SA-MP-MySQL 原版移植的，原版已经在社区经过多年发展和实际使用。OPFramework 这边的主要目标是保证它能够正确接入框架并正常运行，而不是重新开发或重构一套 MySQL，因此尽可能保留了原版实现，以维持与原版的高度兼容性。不建议未经充分验证就擅自优化或重构，避免破坏原版SA-MP MySQL逻辑和兼容性，甚至引入新的问题
- 可作为 SA-MP-MySQL 插件使用，实测直接部署进 [openmp-SC-RP](https://github.com/XiaoNiaoa/openmp-SC-RP) 并移除原有的 SA-MP-MySQL 插件后，无需修改任何现有脚本即可正常运行。
- 支持 Windows x86 和 Linux（WSL）的 Debug / Release 构建。

## Pawn可直接使用 不需要手动连接

### .pwn
```c
#include <a_mysql>

new MySQL:g_DefaultMySQL;

public OnGameModeInit()
{
    g_DefaultMySQL = mysql_get_default_handle();
    if (g_DefaultMySQL == 0)
    {
        return 0;
    }
    return 1;
}
```

### config.json
```json
    "mysql": {
        "hostname": "127.0.0.1",
        "username": "",
        "password": "",
        "database": "",
        "port": 3306,
        "pool_size": 2,
        "auto_reconnect": true
    }
```
## 获取源码

```bash
git clone --recursive https://github.com/XiaoNiaoa/op-framework-cpp.git
```

## 开发环境

### 需要的库

- Boost 1.90.0 32位: [Boost 官方发布页面](https://www.boost.org/releases/1.90.0/)
- MariaDB Connector/C 3.4.11-GA 32 位开发文件: [MariaDB Connector/C 官方下载页面](https://mariadb.com/downloads/connectors/c-connector/)

## 生命周期与运行顺序说明

1. **组件生命周期阶段**：服务器会先完成一批组件的 `onLoad()`，再统一进入 `onInit()`，然后统一进入 `onReady()`。这是最重要的阶段屏障。
2. **同一阶段内不同组件的执行顺序**：当前 open.mp 源码使用以 UID 为键的 `FlatHashMap` 保存组件，并遍历它来调用各阶段回调。不能假定组件回调严格按文件名、加载目录顺序或依赖顺序执行。
3. **运行时事件顺序**：例如 `onTick()`、玩家连接事件和 Pawn 脚本加载/卸载事件。这些不是生命周期阶段本身，而是在相应事件发生时调用已经注册的处理器。它们会在 `onLoad()` / `onInit()` 注册监听器之后触发。

### 加载动态库并取得组件对象
服务器扫描/选择 `components` 目录中的组件二进制，加载动态库并寻找导出的 `ComponentEntryPoint`。

### provideConfiguration()（配置准备阶段）
在正常启动流程中，服务器先让组件提供它们的配置项，再执行正式生命周期回调。这个回调接收 `ILogger`、`IEarlyConfig` 和 `defaults` 参数，用于注册/提供默认配置。

### onLoad(ICore* core)
SDK 对它的定义是：**每个组件都会调用；适合缓存 Core 接口并注册 Core / 玩家事件处理器。**

### onInit(IComponentList* components)
当所有组件都已加载后调用；适合查询其他组件并注册它们的事件处理器，但不应在此阶段与其他组件交互或触发事件

### onReady()
当所有组件完成初始化后调用；适合与其他组件交互、触发事件和执行更复杂的逻辑，基本上是 `onLoad → onInit → onReady`

### onTick()
持续的运行期回调，不是只执行一次的启动回调。实际频率受 Core 的 `sleep` / 动态 tick 配置和本次循环耗时影响

### onFree() 和 free()
- `IComponent::onFree(IComponent* component)` 组件即将被释放前的通知，适合断开对目标组件的引用

- 随后调用 `free()` 是真正释放本组件自身数据的入口。在 `free()` 调用 `delete this`，析构函数再移除本组件注册到 Pawn / Core / Player dispatcher 的事件处理器

### 脚本卸载 / GMX

当前 Pawn 实现卸载脚本时，核心步骤包括：

1. 卸载 gamemode 时，先对普通玩家调用 `OnPlayerDisconnect`（排除 NPC）；卸载 filterscript 时没有这一步；
2. 对当前连接的玩家调用 `OnScriptUnloadPlayer`；
3. 卸载 gamemode 时调用 main script 的 `OnGameModeExit`，随后向 side scripts 分发 `OnGameModeExit`；卸载 filterscript 时调用该 filterscript 的 `OnFilterScriptExit`；
4. 清理该 AMX 的 Pawn timers；
5. 通知 legacy Pawn plugins `AmxUnload`；
6. 分发 `PawnEventHandler::onAmxUnload(script)`；
7. 移除 AMX 与脚本对象的映射，并释放脚本对象。

Game-mode restart 与组件卸载不同。`ICore::resetAll()` 会调用所有组件的 `reset()`，然后重置玩家状态。组件本身通常仍然存在；`reset()` 用来清理或重置与当前 gamemode 有关的数据。Pawn 之后会加载新的 main script。不要把 GMX 当作整个 open.mp 组件系统重新启动，也不要假定各组件会再次经过 `onLoad()` / `onInit()` / `onReady()`。

## 说明

本项目主要用于学习、参考和二次开发。它提供的是基础结构，不保证包含完整的服务器功能，但未来可能会更新更多功能

使用前请根据自己的开发环境检查依赖版本和构建结果
