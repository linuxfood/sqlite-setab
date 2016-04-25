/*
 * Copyright (c) 2016 Brian Smith <brian@linuxfood.net>
 *
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "Setab.h"

// Sqlite3 C-interface bridge functions
namespace {
int setab_create(sqlite3* db, void* pAux, int argc, const char* const* argv, sqlite3_vtab** ppVTab, char** pzErr) {
    try {
        SetabRegistry* registry = static_cast<SetabRegistry*>(pAux);
        auto* tab = new Setab(db, registry, argv[2], vector<string>(argv+3, argv+argc));
        *ppVTab = tab->vTableBase();
    } catch (const std::runtime_error& ex) {
        return SQLITE_ERROR;
    }
    return SQLITE_OK;
}

int setab_destroy(sqlite3_vtab *pVTab) {
    Setab* table = reinterpret_cast<Setab*>(pVTab);
    delete table;
    return SQLITE_OK;
}

int setab_bestindex(sqlite3_vtab* pVTab, sqlite3_index_info* pIndexInfo) {
    Setab* table = reinterpret_cast<Setab*>(pVTab);
    table->bestIndex(pIndexInfo);
    return SQLITE_OK;
}

int setab_open(sqlite3_vtab* pVTab, sqlite3_vtab_cursor** ppCursor) {
    Setab* table = reinterpret_cast<Setab*>(pVTab);
    if (table->isWriteOnly()) {
        return SQLITE_ERROR;
    }
    SetabCursor* cursor = new SetabCursor(table);
    *ppCursor = cursor->vTableCursorBase();
    return SQLITE_OK;
}

int setab_close(sqlite3_vtab_cursor* pSetabCursor) {
    SetabCursor* cursor = reinterpret_cast<SetabCursor*>(pSetabCursor);
    std::cout << "Closing cursor\n";
    delete cursor;
    return SQLITE_OK;
}

int setab_eof(sqlite3_vtab_cursor* pSetabCursor) {
    SetabCursor* cursor = reinterpret_cast<SetabCursor*>(pSetabCursor);
    return cursor->isEOF();
}

int setab_filter(sqlite3_vtab_cursor* pSetabCursor, int idxNum, const char* idxStr, int argc, sqlite3_value** argv) {
    SetabCursor* cursor = reinterpret_cast<SetabCursor*>(pSetabCursor);
    return cursor->filter(idxNum, vector<sqlite3_value*>(argv, argv+argc));
}

int setab_next(sqlite3_vtab_cursor* pSetabCursor) {
    SetabCursor* cursor = reinterpret_cast<SetabCursor*>(pSetabCursor);
    cursor->nextRow();
    return SQLITE_OK;
}

int setab_column(sqlite3_vtab_cursor* pSetabCursor, sqlite3_context* pContext, int N) {
    SetabCursor* cursor = reinterpret_cast<SetabCursor*>(pSetabCursor);
    //std::cout << "xColumn(" << N << "): ";
    const auto& col = cursor->row().columns()[N];
    if (std::get<0>(col) == ColumnType::INTEGER) {
        //std::cout << std::get<2>(col) << "\n";
        sqlite3_result_int64(pContext, std::get<2>(col));
    } else if(std::get<0>(col) == ColumnType::TEXT) {
        //std::cout << std::get<1>(col) << "\n";
        sqlite3_result_text(pContext, std::get<1>(col).data(), std::get<1>(col).size(), SQLITE_TRANSIENT);
    } else {
        return SQLITE_ERROR;
    }
    return SQLITE_OK;
}

int setab_rowid(sqlite3_vtab_cursor* pSetabCursor, sqlite_int64* pRowid) {
    SetabCursor* cursor = reinterpret_cast<SetabCursor*>(pSetabCursor);
    *pRowid = cursor->rowId();
    return SQLITE_OK;
}

int setab_update(sqlite3_vtab* pVTab, int argc, sqlite3_value **argv, sqlite_int64* pRowid) {
    Setab* table = reinterpret_cast<Setab*>(pVTab);
    if (table->isReadOnly()) {
        return SQLITE_READONLY;
    }
    // You can only INSERT into setab tables. No update. No delete.
    // Those operations make no sense.
    if (!(argc > 1 && sqlite3_value_type(argv[0]) == SQLITE_NULL)) {
        return SQLITE_CONSTRAINT_VTAB;
    }
    return table->write(pRowid, vector<sqlite3_value*>(argv+2, argv+argc));
}

int setab_rename(sqlite3_vtab* pVTab, const char* zNew) {
    Setab* table = reinterpret_cast<Setab*>(pVTab);
    table->rename(zNew); 
    return SQLITE_OK;
}
}

sqlite3_module* Sqlite3SetabModule() {
    static sqlite3_module module {
        .iVersion = 1,
        .xCreate = setab_create,
        .xConnect = setab_create,
        .xBestIndex = setab_bestindex,
        .xDisconnect = setab_destroy,
        .xDestroy = setab_destroy,
        .xOpen = setab_open,
        .xClose = setab_close,
        .xFilter = setab_filter,
        .xNext = setab_next,
        .xEof = setab_eof,
        .xColumn = setab_column,
        .xRowid = setab_rowid,
        .xUpdate = setab_update,
        .xBegin = nullptr,
        .xSync = nullptr,
        .xCommit = nullptr,
        .xRollback = nullptr,
        .xFindFunction = nullptr,
        .xRename = setab_rename,
        .xSavepoint = nullptr,
        .xRelease = nullptr,
        .xRollbackTo = nullptr
    };
    return &module;
}
