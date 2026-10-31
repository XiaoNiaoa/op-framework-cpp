#include "mysql.hpp"

#include <string>
#include <utility>

#include <spdlog/spdlog.h>

#include "module.hpp"
#include "pointers.hpp"
#include "log/log.hpp"
#include "mysql/main.hpp"

#include "mysql/CError.hpp"
#include "mysql/CHandle.hpp"
#include "mysql/COptions.hpp"
#include "mysql/CQuery.hpp"
#include "mysql/CResult.hpp"

namespace
{
std::string readConfigString(const IConfig& config, const char* key)
{
    const StringView value = config.getString(key);
    if (value.empty())
        return {};

    return std::string(value.data(), value.size());
}

Handle_t getDefaultConnection()
{
    if (OPCoreMySQL::defaultHandleId == 0)
        return nullptr;

    Handle_t handle = CHandleManager::Get()->GetHandle(OPCoreMySQL::defaultHandleId);
    if (!handle)
        OPCoreMySQL::defaultHandleId = 0;

    return handle;
}

OPCoreMySQL::QueryResult copyQueryResult(const ResultSet_t& resultSet)
{
    OPCoreMySQL::QueryResult result;
    if (!resultSet)
        return result;

    result.query = resultSet->GetExecutedQuery();
    result.executionTimeMilliseconds = resultSet->GetExecutionTime(CResultSet::TimeType::MILLISECONDS);
    result.executionTimeMicroseconds = resultSet->GetExecutionTime(CResultSet::TimeType::MICROSECONDS);

    CResult* activeResult = resultSet->GetActiveResult();
    if (!activeResult)
        return result;

    result.affectedRows = static_cast<std::uint64_t>(activeResult->AffectedRows());
    result.insertId = static_cast<std::uint64_t>(activeResult->InsertId());
    result.warningCount = activeResult->WarningCount();

    const unsigned int fieldCount = activeResult->GetFieldCount();
    result.fields.reserve(fieldCount);

    for (unsigned int field = 0; field < fieldCount; ++field)
    {
        std::string fieldName;
        if (activeResult->GetFieldName(field, fieldName))
            result.fields.push_back(std::move(fieldName));
        else
            result.fields.emplace_back();
    }

    const auto rowCount = static_cast<std::size_t>(activeResult->GetRowCount());
    result.rows.reserve(rowCount);

    for (std::size_t rowIndex = 0; rowIndex < rowCount; ++rowIndex)
    {
        std::vector<std::optional<std::string>> row;
        row.reserve(fieldCount);

        for (unsigned int field = 0; field < fieldCount; ++field)
        {
            const char* value = nullptr;
            if (activeResult->GetRowData(
                    static_cast<unsigned int>(rowIndex), field, &value) && value)
            {
                row.emplace_back(std::string(value));
            }
            else
            {
                row.emplace_back(std::nullopt);
            }
        }

        result.rows.push_back(std::move(row));
    }

    return result;
}

bool connectDefaultDatabase()
{
    ICore* core = OPCoreSharedPtr::core();
    if (!core)
    {
        OPCORE_LOGGER_ERROR(OPCoreLog::get("mysql"), "[OPCore::MySQL] Core 尚未就绪，无法创建数据库连接。");
        return false;
    }

    IConfig& config = core->getConfig();
    const std::string hostname = readConfigString(config, "mysql.hostname");
    const std::string username = readConfigString(config, "mysql.username");
    const std::string password = readConfigString(config, "mysql.password");
    const std::string database = readConfigString(config, "mysql.database");

    const int* configuredPort = config.getInt("mysql.port");
    const int port = configuredPort ? *configuredPort : 3306;

    const int* configuredPoolSize = config.getInt("mysql.pool_size");
    const int poolSize = configuredPoolSize ? *configuredPoolSize : 2;

    const bool* configuredAutoReconnect = config.getBool("mysql.auto_reconnect");
    const bool autoReconnect = configuredAutoReconnect ? *configuredAutoReconnect : true;

    if (hostname.empty() || username.empty() || database.empty())
    {
        OPCORE_LOGGER_ERROR(OPCoreLog::get("mysql"), "[OPCore::MySQL] 数据库配置不完整：mysql.hostname、mysql.username 和 mysql.database 不能为空。");
        return false;
    }

    if (port < 1 || port > 65535)
    {
        OPCORE_LOGGER_ERROR(OPCoreLog::get("mysql"), "[OPCore::MySQL] mysql.port 必须在 1 到 65535 之间，当前值为 {}。", port);
        return false;
    }

    if (poolSize < 0)
    {
        OPCORE_LOGGER_ERROR(OPCoreLog::get("mysql"), "[OPCore::MySQL] mysql.pool_size 不能小于 0，当前值为 {}。", poolSize);
        return false;
    }

    COptionManager* optionManager = COptionManager::Get();
    const OptionsId_t optionsId = optionManager->Create();
    COptions* options = optionManager->GetOptionHandle(optionsId);
    if (!options)
    {
        OPCORE_LOGGER_ERROR(OPCoreLog::get("mysql"), "[OPCore::MySQL] 无法创建数据库连接选项。");
        return false;
    }

    options->SetOption<bool>(COptions::Type::AUTO_RECONNECT, autoReconnect);
    options->SetOption<unsigned int>(COptions::Type::SERVER_PORT, static_cast<unsigned int>(port));
    options->SetOption<unsigned int>(COptions::Type::POOL_SIZE, static_cast<unsigned int>(poolSize));

    OPCORE_LOGGER_INFO(OPCoreLog::get("mysql"), "[OPCore::MySQL] 正在连接数据库 {}:{}/{}（连接池大小：{}）……",
        hostname, port, database, poolSize);

    CError<CHandle> error;
    CHandleManager* handleManager = CHandleManager::Get();
    Handle_t handle = handleManager->Create(
        hostname.c_str(), username.c_str(), password.c_str(), database.c_str(), options, error);

    if (error || !handle)
    {
        OPCORE_LOGGER_ERROR(OPCoreLog::get("mysql"), "[OPCore::MySQL] 创建默认数据库连接失败：{}",
            error ? error.msg() : std::string("底层没有返回有效的连接句柄"));
        return false;
    }

    // CHandleManager::Create() 在底层连接失败时仍可能返回句柄，因此必须验证真实连接。
    std::string status;
    if (!handle->GetStatus(status))
    {
        OPCORE_LOGGER_ERROR(OPCoreLog::get("mysql"), "[OPCore::MySQL] 数据库连接不可用，请检查地址、端口、账号、密码和数据库名称。详细错误见 MySQL 日志。");
        handleManager->Destroy(handle);
        return false;
    }

    OPCoreMySQL::defaultHandleId = handle->GetId();
    OPCORE_LOGGER_INFO(OPCoreLog::get("mysql"), "[OPCore::MySQL] 默认数据库连接已建立，句柄 ID：{}。", OPCoreMySQL::defaultHandleId);
    return true;
}

bool submitAsyncQuery(
    CHandle::ExecutionType executionType,
    std::string query,
    OPCoreMySQL::QueryCallback onSuccess,
    OPCoreMySQL::QueryErrorCallback onError)
{
    Handle_t handle = getDefaultConnection();
    if (!handle)
        return false;

    Query_t pendingQuery = CQuery::Create(std::move(query));

    if (onSuccess)
    {
        std::function<void(ResultSet_t)> callback =
            [success = std::move(onSuccess)](ResultSet_t resultSet) mutable
            {
                success(copyQueryResult(resultSet));
            };
        pendingQuery->OnExecutionFinished(std::move(callback));
    }

    if (onError)
    {
        std::function<void(unsigned int, std::string)> callback =
            [failure = std::move(onError)](unsigned int errorCode, std::string message) mutable
            {
                failure(errorCode, message);
            };
        pendingQuery->OnError(std::move(callback));
    }

    return handle->Execute(executionType, std::move(pendingQuery));
}

class OPCoreMySQLModule final
    : public OPCoreModuleManager
{
public:
    bool onModuleInit() override
    {
        return connectDefaultDatabase();
    }

    StringView moduleName() const override
    {
        return "OPCore MySQL";
    }
};

OPCORE_REGISTER_MODULE(OPCoreMySQLModule);
}

namespace OPCoreMySQL
{
HandleId getDefaultHandleId()
{
    return getDefaultConnection() ? OPCoreMySQL::defaultHandleId : 0;
}

bool isConnected()
{
    Handle_t handle = getDefaultConnection();
    if (!handle)
        return false;

    std::string status;
    return handle->GetStatus(status);
}

bool getStatus(std::string& status)
{
    status.clear();
    Handle_t handle = getDefaultConnection();
    return handle && handle->GetStatus(status);
}

bool getLastError(QueryError& error)
{
    error = {};
    Handle_t handle = getDefaultConnection();
    if (!handle)
        return false;

    const bool gotCode = handle->GetErrorId(error.code);
    const bool gotMessage = handle->GetErrorMessage(error.message);
    return gotCode && gotMessage;
}

std::uint32_t unprocessedQueryCount()
{
    Handle_t handle = getDefaultConnection();
    return handle ? handle->GetUnprocessedQueryCount() : 0;
}

bool querySync(std::string query, QueryResult& result, QueryError* error)
{
    result = {};
    if (error)
        *error = {};

    Handle_t handle = getDefaultConnection();
    if (!handle)
    {
        if (error)
            error->message = "默认数据库连接当前不可用。";
        return false;
    }

    Query_t pendingQuery = CQuery::Create(std::move(query));
    if (!handle->Execute(CHandle::ExecutionType::UNTHREADED, pendingQuery))
    {
        if (error)
        {
            handle->GetErrorId(error->code);
            handle->GetErrorMessage(error->message);
            if (error->message.empty())
                error->message = "SQL 执行失败，底层未返回详细错误信息。";
        }
        return false;
    }

    result = copyQueryResult(pendingQuery->GetResult());
    return true;
}

bool executeSync(std::string query, QueryError* error)
{
    QueryResult ignoredResult;
    return querySync(std::move(query), ignoredResult, error);
}

bool queryAsync(std::string query, QueryCallback onSuccess, QueryErrorCallback onError)
{
    return submitAsyncQuery(
        CHandle::ExecutionType::THREADED,
        std::move(query), std::move(onSuccess), std::move(onError));
}

bool queryParallel(std::string query, QueryCallback onSuccess, QueryErrorCallback onError)
{
    return submitAsyncQuery(
        CHandle::ExecutionType::PARALLEL,
        std::move(query), std::move(onSuccess), std::move(onError));
}

bool escapeString(const std::string& input, std::string& escaped)
{
    escaped.clear();
    Handle_t handle = getDefaultConnection();
    return handle && handle->EscapeString(input.c_str(), escaped);
}

bool setCharacterSet(const std::string& characterSet)
{
    Handle_t handle = getDefaultConnection();
    return handle && handle->SetCharacterSet(characterSet);
}

bool getCharacterSet(std::string& characterSet)
{
    characterSet.clear();
    Handle_t handle = getDefaultConnection();
    return handle && handle->GetCharacterSet(characterSet);
}
}
