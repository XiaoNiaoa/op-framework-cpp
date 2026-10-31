cmake_minimum_required(VERSION 3.19...3.31)

project(OPFramework LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# OPFramework currently supports Windows and Linux.
if(NOT WIN32 AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR
        "OPFramework currently supports Windows and Linux.")
endif()

# The plugin and MariaDB client must use the same 32-bit architecture.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR
        "OPFramework requires a 32-bit toolchain and client library.")
endif()

# Discover the project's external dependencies.
list(APPEND CMAKE_MODULE_PATH
    "${PROJECT_SOURCE_DIR}/src/core/mysql/cmake"
)

if(WIN32)
    find_package(MySQLCAPI 3.4.11 EXACT REQUIRED)
else()
    find_package(MySQLCAPI REQUIRED)
endif()

find_package(Threads REQUIRED)

# Boost is used through headers, including boost::variant.
# Support automatic discovery and user-provided installation paths.
find_path(OPFRAMEWORK_BOOST_INCLUDE_DIR
    NAMES boost/variant.hpp
    HINTS
        "${BOOST_ROOT}"
        "$ENV{BOOST_ROOT}"
        "${BOOST_INCLUDEDIR}"
        "$ENV{BOOST_INCLUDEDIR}"
    PATH_SUFFIXES include
    DOC "Directory containing Boost headers"
)

if(NOT OPFRAMEWORK_BOOST_INCLUDE_DIR)
    message(FATAL_ERROR
        "Could not find Boost headers (boost/variant.hpp). "
        "Set BOOST_ROOT or BOOST_INCLUDEDIR to the Boost installation directory.")
endif()

message(STATUS
    "Boost headers: ${OPFRAMEWORK_BOOST_INCLUDE_DIR}"
)

# Build the open.mp SDK.
add_subdirectory(lib/open.mp-sdk)

# Build fmt as a static dependency without leaking its build options.
function(add_fmt_dependency)
    set(FMT_DOC OFF)
    set(FMT_TEST OFF)
    set(FMT_INSTALL OFF)
    set(FMT_UNICODE OFF)
    set(BUILD_SHARED_LIBS OFF)

    add_subdirectory(
        "${PROJECT_SOURCE_DIR}/lib/fmt"
        "${PROJECT_BINARY_DIR}/_deps/fmt"
    )
endfunction()

add_fmt_dependency()

set_target_properties(fmt PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_options(fmt PRIVATE -m32)
endif()

# Build libsodium (lib/libsodium, tag 1.0.20-RELEASE) from source as a static library.
# Upstream ships no CMake project: every .c under src/libsodium is compiled and
# selects its own implementation via preprocessor checks. version.h is generated
# next to the other headers (the path is in libsodium's own .gitignore).
function(add_libsodium_dependency)
    set(SODIUM_SOURCE_DIR "${PROJECT_SOURCE_DIR}/lib/libsodium/src/libsodium")

    set(VERSION 1.0.20)
    set(SODIUM_LIBRARY_VERSION_MAJOR 26)
    set(SODIUM_LIBRARY_VERSION_MINOR 2)
    set(SODIUM_LIBRARY_MINIMAL_DEF "")
    configure_file(
        "${SODIUM_SOURCE_DIR}/include/sodium/version.h.in"
        "${SODIUM_SOURCE_DIR}/include/sodium/version.h"
        @ONLY
    )

    file(GLOB_RECURSE SODIUM_SOURCES CONFIGURE_DEPENDS
        "${SODIUM_SOURCE_DIR}/*.c"
    )

    add_library(sodium STATIC ${SODIUM_SOURCES})

    set_target_properties(sodium PROPERTIES
        C_STANDARD 99
        POSITION_INDEPENDENT_CODE ON
    )

    target_include_directories(sodium
        PUBLIC
            "${SODIUM_SOURCE_DIR}/include"
        PRIVATE
            "${SODIUM_SOURCE_DIR}/include/sodium"
    )

    target_compile_definitions(sodium
        PUBLIC
            SODIUM_STATIC
        PRIVATE
            CONFIGURED
            $<$<C_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>
    )

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_compile_options(sodium PRIVATE -m32)
    endif()
endfunction()

add_libsodium_dependency()

# Create the only project-owned binary target.
add_library(${PROJECT_NAME} SHARED)

target_compile_features(${PROJECT_NAME} PRIVATE
    cxx_std_17
)

# Common project definitions.
target_compile_definitions(${PROJECT_NAME} PRIVATE
    PROJECT_NAME="OPFramework"
    HAVE_STDINT_H=1
    FMT_UNICODE=0
    SPDLOG_FMT_EXTERNAL
    PAWN_CELL_SIZE=32
)

# Keep the SDK include directory explicit and first in the include order.
# OPFramework directly includes SDK headers, including Pawn interfaces.
target_include_directories(${PROJECT_NAME} BEFORE PRIVATE
    "${PROJECT_SOURCE_DIR}/lib/open.mp-sdk/include"
)

target_include_directories(${PROJECT_NAME} PRIVATE
    "${PROJECT_SOURCE_DIR}/lib/open.mp-sdk/include"
    "${PROJECT_SOURCE_DIR}/lib"
    "${PROJECT_SOURCE_DIR}/lib/pawn/source"
    "${PROJECT_SOURCE_DIR}/lib/pawn-natives"
    "${PROJECT_SOURCE_DIR}/lib/entt/src"
    "${PROJECT_SOURCE_DIR}/lib/unordered_dense/include"
    "${PROJECT_SOURCE_DIR}/lib/spdlog/include"
    "${PROJECT_SOURCE_DIR}/src"
)

# Boost is a third-party header dependency.
target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE
    "${OPFRAMEWORK_BOOST_INCLUDE_DIR}"
)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE
        /execution-charset:GBK
        /source-charset:UTF-8
    )

    target_link_options(${PROJECT_NAME} PRIVATE
        /LARGEADDRESSAWARE
    )

    target_compile_definitions(${PROJECT_NAME} PRIVATE
        WIN32
        WIN32_LEAN_AND_MEAN=1
        SUBHOOK_STATIC
        _CRT_SECURE_NO_DEPRECATE=1
        _CRT_SECURE_NO_WARNINGS
        _ENABLE_ATOMIC_ALIGNMENT_FIX
    )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_options(${PROJECT_NAME} PRIVATE
        -m32
        -Wall
    )

    target_link_options(${PROJECT_NAME} PRIVATE
        -m32
    )

    target_compile_definitions(${PROJECT_NAME} PRIVATE
        LINUX=1
        HAVE_ALLOCA_H=1
    )

    target_include_directories(${PROJECT_NAME} PRIVATE
        "${PROJECT_SOURCE_DIR}/lib/pawn/source/linux"
    )

    set_target_properties(${PROJECT_NAME} PROPERTIES
        PREFIX ""
        POSITION_INDEPENDENT_CODE ON
        INSTALL_RPATH "$ORIGIN/..:$ORIGIN"
    )
endif()

# All compiled sources belong to OPFramework.
# Dependencies are attached to this one target.
target_link_libraries(${PROJECT_NAME} PRIVATE
    OMP-SDK
    fmt::fmt
    Threads::Threads
    MySQLCAPI::MySQLCAPI
    sodium
)

# Register framework and MySQL sources.
add_subdirectory(src)

# Copy MariaDB Connector/C beside the final plugin.
if(WIN32 AND MYSQLCAPI_LIBRARY_SHARED)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${MYSQLCAPI_LIBRARY_SHARED}"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/libmariadb.dll"
        COMMENT "Copying MariaDB Connector/C runtime DLL"
        VERBATIM
    )

    install(FILES
        "${MYSQLCAPI_LIBRARY_SHARED}"
        DESTINATION .
    )
endif()

# Install the final plugin.
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION components
    LIBRARY DESTINATION components
)

# Install the spdlog license.
install(FILES
    "${PROJECT_SOURCE_DIR}/lib/spdlog/LICENSE"
    DESTINATION licenses/spdlog
)