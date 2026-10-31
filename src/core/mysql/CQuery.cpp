#include "CResult.hpp"
#include "mysql.hpp"
#include "CQuery.hpp"
#include "core/log/log.hpp"


bool CQuery::Execute(MYSQL *connection)
{
	OPCORE_LOGGER_DEBUG(OPCoreLog::get("mysql"), "CQuery::Execute(this={}, connection={})",
					 static_cast<const void *>(this),
					 static_cast<const void *>(connection));

	int error = 0;

	default_clock::time_point exec_timepoint = default_clock::now();
	error = mysql_real_query(connection, m_Query.c_str(), m_Query.length());
	default_clock::duration exec_time = default_clock::now() - exec_timepoint;

	if (error != 0)
	{
		const char *error_str = mysql_error(connection);
		string msg = fmt::format("error #{} while executing query \"{}\": {}", mysql_errno(connection), m_Query, error_str ? error_str : "(nullptr)");

		std::string logMessage = msg;
		if (!m_DbgInfo.empty())
		{
			logMessage += " (";
			bool first = true;
			for (const auto& info : m_DbgInfo)
			{
				if (!first) logMessage += " -> ";
				first = false;
				logMessage += fmt::format("{}:{} ({})", info.file, info.line, info.function);
			}
			logMessage += ')';
		}
		OPCORE_LOGGER_ERROR(OPCoreLog::get("mysql"), "{}", logMessage);
		return false;
	}

	auto
		query_exec_time_milli = std::chrono::duration_cast<std::chrono::milliseconds>(exec_time).count(),
		query_exec_time_micro = std::chrono::duration_cast<std::chrono::microseconds>(exec_time).count();

	OPCORE_LOGGER_INFO(OPCoreLog::get("mysql"),
		"query \"{}\" successfully executed within {}.{} milliseconds",
		m_Query, query_exec_time_milli,
		query_exec_time_micro - (query_exec_time_milli * 1000));

	m_Result = CResultSet::Create(connection, exec_time, m_Query);
	return m_Result != nullptr;
}
