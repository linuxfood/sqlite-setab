#include "sqlite3.c"

/*
** Return true if the prepared statement is DDL (CREATE TABLE / CREATE INDEX).
*/
SQLITE_API int SQLITE_STDCALL sqlite3_stmt_ddl(sqlite3_stmt *pStmt){
  if(pStmt){
    return ((Vdbe*)pStmt)->pParse->pNewTable ? 1 : 0;
  }
  return 0;
}

