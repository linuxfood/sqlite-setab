#pragma once

#include "sqlite3.h"

#ifdef __cplusplus
extern "C" {
#endif

SQLITE_API int SQLITE_STDCALL sqlite3_stmt_ddl(sqlite3_stmt*);

#ifdef __cplusplus
}
#endif
