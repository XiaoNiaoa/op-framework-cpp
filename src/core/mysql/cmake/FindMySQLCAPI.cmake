# Locate MariaDB Connector/C.
# On Windows, search only the explicit installation root or the standard
# 32-bit MSI installation directory. Do not fall back to other MySQL installs.

set(MYSQLCAPI_INSTALL_ROOT "" CACHE PATH
    "MariaDB Connector/C installation prefix")

set(_MYSQLCAPI_ROOT_HINTS)

if(MYSQLCAPI_INSTALL_ROOT)
    list(APPEND _MYSQLCAPI_ROOT_HINTS
        "${MYSQLCAPI_INSTALL_ROOT}"
    )
endif()

if(WIN32)
    # Derive the x86 Program Files directory without referencing the
    # invalid CMake environment-variable syntax $ENV{ProgramFiles(x86)}.
    set(_MYSQLCAPI_PROGRAM_FILES "$ENV{ProgramFiles}")

    if(_MYSQLCAPI_PROGRAM_FILES MATCHES "\\(x86\\)$")
        set(_MYSQLCAPI_PROGRAM_FILES_X86
            "${_MYSQLCAPI_PROGRAM_FILES}")
    elseif(NOT _MYSQLCAPI_PROGRAM_FILES STREQUAL "")
        set(_MYSQLCAPI_PROGRAM_FILES_X86
            "${_MYSQLCAPI_PROGRAM_FILES} (x86)")
    endif()

    if(_MYSQLCAPI_PROGRAM_FILES_X86)
        list(APPEND _MYSQLCAPI_ROOT_HINTS
            "${_MYSQLCAPI_PROGRAM_FILES_X86}/MariaDB/MariaDB Connector C"
        )
    endif()

    find_path(_MYSQLCAPI_INCLUDE_DIR
        NAMES mysql.h
        HINTS ${_MYSQLCAPI_ROOT_HINTS}
        PATH_SUFFIXES include include/mariadb include/mysql
        NO_DEFAULT_PATH
    )

    find_library(_MYSQLCAPI_LIBRARY
        NAMES libmariadb mariadb
        HINTS ${_MYSQLCAPI_ROOT_HINTS}
        PATH_SUFFIXES lib lib/mariadb lib32
        NO_DEFAULT_PATH
    )

    find_file(_MYSQLCAPI_LIBRARY_SHARED
        NAMES libmariadb.dll
        HINTS ${_MYSQLCAPI_ROOT_HINTS}
        PATH_SUFFIXES lib bin
        NO_DEFAULT_PATH
    )
else()
    # On Unix-like systems, use the system package or an explicit prefix.
    find_path(_MYSQLCAPI_INCLUDE_DIR
        NAMES mysql.h
        HINTS "${MYSQLCAPI_INSTALL_ROOT}"
        PATH_SUFFIXES include include/mariadb include/mysql
    )

    find_library(_MYSQLCAPI_LIBRARY
        NAMES mariadb libmariadb
        HINTS "${MYSQLCAPI_INSTALL_ROOT}"
        PATH_SUFFIXES lib lib64 lib/mariadb lib32
    )
endif()

# Read MariaDB's version from its installed headers.
set(MYSQLCAPI_VERSION "")
set(_mysqlcapi_is_mariadb FALSE)

if(_MYSQLCAPI_INCLUDE_DIR
    AND EXISTS "${_MYSQLCAPI_INCLUDE_DIR}/mariadb_version.h")

    set(_mysqlcapi_is_mariadb TRUE)

    file(STRINGS
        "${_MYSQLCAPI_INCLUDE_DIR}/mariadb_version.h"
        _mariadb_version_line
        REGEX "^#define[ \t]+MARIADB_PACKAGE_VERSION[ \t]+"
    )

    string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+"
        MYSQLCAPI_VERSION
        "${_mariadb_version_line}"
    )
endif()

# Preserve the conventional FindPackage variables for consumers.
set(MYSQLCAPI_INCLUDE_DIR "${_MYSQLCAPI_INCLUDE_DIR}")
set(MYSQLCAPI_LIBRARY "${_MYSQLCAPI_LIBRARY}")

if(WIN32)
    set(MYSQLCAPI_LIBRARY_SHARED "${_MYSQLCAPI_LIBRARY_SHARED}")
endif()

set(_MYSQLCAPI_REQUIRED_VARS
    _MYSQLCAPI_INCLUDE_DIR
    _MYSQLCAPI_LIBRARY
    _mysqlcapi_is_mariadb
)

if(WIN32)
    list(APPEND _MYSQLCAPI_REQUIRED_VARS
        _MYSQLCAPI_LIBRARY_SHARED
    )
endif()

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(MySQLCAPI
    REQUIRED_VARS ${_MYSQLCAPI_REQUIRED_VARS}
    VERSION_VAR MYSQLCAPI_VERSION
    HANDLE_VERSION_RANGE
    REASON_FAILURE_MESSAGE
        "Install the 32-bit MariaDB Connector/C MSI, or set MYSQLCAPI_INSTALL_ROOT to its installation directory."
)

if(MySQLCAPI_FOUND AND NOT TARGET MySQLCAPI::MySQLCAPI)
    if(WIN32)
        # IMPORTED_LOCATION is the runtime DLL; IMPORTED_IMPLIB is the .lib.
        add_library(MySQLCAPI::MySQLCAPI SHARED IMPORTED GLOBAL)

        set_target_properties(MySQLCAPI::MySQLCAPI PROPERTIES
            IMPORTED_IMPLIB "${_MYSQLCAPI_LIBRARY}"
            IMPORTED_LOCATION "${_MYSQLCAPI_LIBRARY_SHARED}"
            INTERFACE_INCLUDE_DIRECTORIES "${_MYSQLCAPI_INCLUDE_DIR}"
        )
    else()
        add_library(MySQLCAPI::MySQLCAPI UNKNOWN IMPORTED GLOBAL)

        set_target_properties(MySQLCAPI::MySQLCAPI PROPERTIES
            IMPORTED_LOCATION "${_MYSQLCAPI_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${_MYSQLCAPI_INCLUDE_DIR}"
        )
    endif()
endif()

mark_as_advanced(
    _MYSQLCAPI_INCLUDE_DIR
    _MYSQLCAPI_LIBRARY
    _MYSQLCAPI_LIBRARY_SHARED
)