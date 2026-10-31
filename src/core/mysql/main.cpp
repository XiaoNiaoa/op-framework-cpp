#include "sdk.hpp"
#include "natives.hpp"
#include "CHandle.hpp"
#include "CCallback.hpp"
#include "CResult.hpp"
#include "CDispatcher.hpp"
#include "COptions.hpp"
#include "COrm.hpp"
#include "CDebugInfo.hpp"
#include "version.hpp"

#include "core/log/log.hpp"

#include "mysql.hpp"
#include "main.hpp"
#include <core.hpp>
#include <unordered_set>

namespace OPCoreMySQL
{
HandleId_t defaultHandleId = 0;
}

namespace OPCoreMySQL
{
void SetDefaultConfig(IEarlyConfig& config)
{
    // 只补齐缺失的默认值，不覆盖用户已有配置。
    if (config.getType("mysql.hostname") == ConfigOptionType_None)
        config.setString("mysql.hostname", "127.0.0.1");

    if (config.getType("mysql.username") == ConfigOptionType_None)
        config.setString("mysql.username", "root");

    if (config.getType("mysql.password") == ConfigOptionType_None)
        config.setString("mysql.password", "");

    if (config.getType("mysql.database") == ConfigOptionType_None)
        config.setString("mysql.database", "");

    if (config.getType("mysql.port") == ConfigOptionType_None)
        config.setInt("mysql.port", 3306);

    if (config.getType("mysql.pool_size") == ConfigOptionType_None)
        config.setInt("mysql.pool_size", 2);

    if (config.getType("mysql.auto_reconnect") == ConfigOptionType_None)
        config.setBool("mysql.auto_reconnect", true);
}
}

namespace OPCoreMySQL
{
static bool loaded = false;
static std::unordered_set<AMX *> scripts;


bool Load()
{
	if (loaded)
		return true;

	if (mysql_library_init(0, NULL, NULL))
	{
		OPCORE_LOG_CRITICAL("[OPCore::MySQL] 无法初始化 MySQL 库.");
		return false;
	}

	OPCORE_LOG_INFO("[OPCore::MySQL] MySQL {} 已成功加载.", MYSQL_VERSION);

	loaded = true;
	return true;
}

void Unload()
{
	if (!loaded)
		return;
	OPCoreMySQL::defaultHandleId = 0;
	DetachScripts();

	OPCORE_LOG_INFO("[OPCore::MySQL] 正在卸载插件...");

	COrmManager::CSingleton::Destroy();
	CHandleManager::CSingleton::Destroy();
	CCallbackManager::CSingleton::Destroy();
	CResultSetManager::CSingleton::Destroy();
	CDispatcher::CSingleton::Destroy();
	COptionManager::CSingleton::Destroy();
	CDebugInfoManager::CSingleton::Destroy();

	mysql_library_end();

	OPCORE_LOG_INFO("[OPCore::MySQL] 插件已卸载.");
	loaded = false;
}

void ProcessTick()
{
	if (loaded)
		CDispatcher::Get()->Process();
}

void disconnectDefaultDatabase()
{
    const HandleId_t handleId = OPCoreMySQL::defaultHandleId;

    if (handleId == 0)
        return;

    OPCoreMySQL::defaultHandleId = 0;

    CHandleManager* handleManager = CHandleManager::Get();
    Handle_t handle = handleManager->GetHandle(handleId);

    if (handle)
        handleManager->Destroy(handle);

	OPCORE_LOG_INFO("[OPCore::MySQL] 默认数据库连接已关闭。");
}

static const AMX_NATIVE_INFO native_list[] =
{
	AMX_DEFINE_NATIVE(orm_create)
	AMX_DEFINE_NATIVE(orm_destroy)

	AMX_DEFINE_NATIVE(orm_errno)

	AMX_DEFINE_NATIVE(orm_apply_cache)

	AMX_DEFINE_NATIVE(orm_select)
	AMX_DEFINE_NATIVE(orm_update)
	AMX_DEFINE_NATIVE(orm_insert)
	AMX_DEFINE_NATIVE(orm_delete)

	AMX_DEFINE_NATIVE(orm_save)

	AMX_DEFINE_NATIVE(orm_addvar_int)
	AMX_DEFINE_NATIVE(orm_addvar_float)
	AMX_DEFINE_NATIVE(orm_addvar_string)

	AMX_DEFINE_NATIVE(orm_clear_vars)
	AMX_DEFINE_NATIVE(orm_delvar)
	AMX_DEFINE_NATIVE(orm_setkey)


	AMX_DEFINE_NATIVE(mysql_connect)
	AMX_DEFINE_NATIVE(mysql_connect_file)
	AMX_DEFINE_NATIVE(mysql_close)

	AMX_DEFINE_NATIVE(mysql_unprocessed_queries)
	AMX_DEFINE_NATIVE(mysql_global_options)

	AMX_DEFINE_NATIVE(mysql_init_options)
	AMX_DEFINE_NATIVE(mysql_set_option)

	AMX_DEFINE_NATIVE(mysql_pquery)
	AMX_DEFINE_NATIVE(mysql_tquery)
	AMX_DEFINE_NATIVE(mysql_query)
	AMX_DEFINE_NATIVE(mysql_tquery_file)
	AMX_DEFINE_NATIVE(mysql_query_file)

	AMX_DEFINE_NATIVE(mysql_errno)
	AMX_DEFINE_NATIVE(mysql_error)
	AMX_DEFINE_NATIVE(mysql_escape_string)
	AMX_DEFINE_NATIVE(mysql_format)
	AMX_DEFINE_NATIVE(mysql_get_charset)
	AMX_DEFINE_NATIVE(mysql_set_charset)
	AMX_DEFINE_NATIVE(mysql_stat)

	AMX_DEFINE_NATIVE(cache_get_row_count)
	AMX_DEFINE_NATIVE(cache_get_field_count)
	AMX_DEFINE_NATIVE(cache_get_result_count)
	AMX_DEFINE_NATIVE(cache_get_field_name)
	AMX_DEFINE_NATIVE(cache_get_field_type)
	AMX_DEFINE_NATIVE(cache_set_result)

	AMX_DEFINE_NATIVE(cache_get_value_index)
	AMX_DEFINE_NATIVE(cache_get_value_index_int)
	AMX_DEFINE_NATIVE(cache_get_value_index_float)
	AMX_DEFINE_NATIVE(cache_is_value_index_null)

	AMX_DEFINE_NATIVE(cache_get_value_name)
	AMX_DEFINE_NATIVE(cache_get_value_name_int)
	AMX_DEFINE_NATIVE(cache_get_value_name_float)
	AMX_DEFINE_NATIVE(cache_is_value_name_null)

	AMX_DEFINE_NATIVE(cache_save)
	AMX_DEFINE_NATIVE(cache_delete)
	AMX_DEFINE_NATIVE(cache_set_active)
	AMX_DEFINE_NATIVE(cache_unset_active)
	AMX_DEFINE_NATIVE(cache_is_any_active)
	AMX_DEFINE_NATIVE(cache_is_valid)

	AMX_DEFINE_NATIVE(cache_affected_rows)
	AMX_DEFINE_NATIVE(cache_insert_id)
	AMX_DEFINE_NATIVE(cache_warning_count)

	AMX_DEFINE_NATIVE(cache_get_query_exec_time)
	AMX_DEFINE_NATIVE(cache_get_query_string)

	// OPFramework integration native: returns the framework-owned default handle.
	AMX_DEFINE_NATIVE(mysql_get_default_handle)
	
	{ NULL, NULL }
};

int AmxLoad(AMX *amx)
{
	if (!loaded)
		return AMX_ERR_INIT;
	scripts.insert(amx);
	CDebugInfoManager::Get()->RegisterAmx(amx);
	CCallbackManager::Get()->AddAmx(amx);
	return amx_Register(amx, native_list, -1);
}

int AmxUnload(AMX *amx)
{
	if (!loaded || scripts.erase(amx) == 0)
		return AMX_ERR_NONE;
	CDebugInfoManager::Get()->EraseAmx(amx);
	CCallbackManager::Get()->RemoveAmx(amx);
	return AMX_ERR_NONE;
}

void DetachScripts()
{
	while (!scripts.empty())
		AmxUnload(*scripts.begin());
}
} // namespace OPCoreMySQL
