# SA-MP-MySQL 9524fcc9088004948770ddd6e05a6074581d3095.
# MySQL implementation is compiled directly into OPFramework.

# Generate MySQL-specific files in this module's build directory.
set(MYSQL_PLUGIN_VERSION "R41-4")

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/version.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/version.hpp"
    @ONLY
)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/a_mysql.inc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/a_mysql.inc"
    @ONLY
)

# Only MySQL needs its generated version.hpp directory.
target_include_directories(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}"
)

# Register MySQL sources.
target_sources(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/CCallback.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/CCallback.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/CConnection.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/CConnection.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/CDispatcher.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/CDispatcher.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/CError.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/CHandle.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/CHandle.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/CDebugInfo.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/CDebugInfo.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/COptions.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/COptions.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/COrm.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/COrm.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/CQuery.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/CQuery.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/CResult.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/CResult.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/CSingleton.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/natives.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/natives.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/main.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/misc.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/mysql.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/sdk.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/types.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/pawn.hpp"
    "${PROJECT_SOURCE_DIR}/lib/pawn/source/amx/amxdbg.c"
)

# Install MySQL-specific resources.
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/a_mysql.inc"
    DESTINATION pawno/include
)

install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
    DESTINATION licenses/SA-MP-MySQL
)

install(FILES
    "${PROJECT_SOURCE_DIR}/lib/fmt/LICENSE"
    DESTINATION licenses/SA-MP-MySQL/fmt
)