#pragma once

#ifdef WIN32
# include <WinSock2.h>
# undef ERROR
#endif

// FindMySQLCAPI supplies the directory containing mysql.h on both platforms.
#include <mysql.h>
