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
#include "Util.h"

enum class ColumnType {
    INTEGER = SQLITE_INTEGER,
    TEXT = SQLITE_TEXT,
};

using ColumnValue = std::tuple<ColumnType, string, int64_t>;

/**
 * Rows are in the following format:
 * %ld[\036[%s][%ld]]+
 * And \036 is the byte for 'record separator'.
 * Where %ld is a 64bit signed integer in utf-8 base-10.
 * And %s is arbitrary bytes other than \036.
 */
class Row {
    int64_t rowId_;
    bool consumed_;
    vector<ColumnValue> columns_;

public:

    explicit Row(int64_t rowId)
        : rowId_{rowId}, consumed_{false}, columns_{} {}

    Row(int64_t rowId, vector<ColumnValue> columns)
        : rowId_{rowId}, consumed_{false}, columns_{columns.begin(), columns.end()} {}

    int64_t rowId() const { return rowId_; }

    milliseconds ts() const {
        return milliseconds(std::get<2>(columns_[0]));
    }

    const vector<ColumnValue>& columns() const { return columns_; }

    bool valid() const {
        return !columns_.empty();
    }

    bool consumed() const { return consumed_; }

    void setConsumed() { consumed_ = true; }

};

std::ostream& operator<<(std::ostream& o, const Row& r) {
    o << "Row:ts=" << r.ts().count()
      << ":size=" << r.columns().size();
    int i = 0;
    for(const auto& col : r.columns()) {
        o << ":col[" << i << "]=";
        auto colType = std::get<0>(col);
        if (colType == ColumnType::INTEGER) {
            o << std::get<2>(col);
        } else if (colType == ColumnType::TEXT) {
            o << "'" << std::get<1>(col) << "'";
        }
        i++;
    }
    return o;
}


class VTable {
    sqlite3_vtab vTableBase_; /* Must come first */
    sqlite3* db_;
    string tableName_;
    vector<string> tableSchema_;
    vector<ColumnType> columnTypes_;
    vector<string> rawTableArgs_;

    void* zctx_;
    void* readSock_;
    void* writeSock_;

    int listenPort_;
    string nextHopService_;

    int lingerMs_; /* int for compat with zmq */
    int batchSize_;
    
    int64_t batchStart_;
    int64_t currentRowId_;

    milliseconds windowSizeMs_;

    std::queue<std::unique_ptr<Row>> queuedRows_;

public:
    VTable(sqlite3* db, string tableName, vector<string> rawTableArgs)
        : vTableBase_{},
          db_{db},
          tableName_{tableName},
          tableSchema_{{"ts INTEGER"}},
          columnTypes_{{ColumnType::INTEGER}},
          rawTableArgs_{rawTableArgs},
          zctx_{nullptr},
          readSock_{nullptr},
          writeSock_{nullptr},
          listenPort_{0},
          nextHopService_{},
          lingerMs_{1000},
          batchSize_{10000},
          batchStart_{0},
          currentRowId_{0},
          windowSizeMs_{100*1000} {

        std::cout << "Create debug..\n";
        for (size_t i=0; i<rawTableArgs_.size(); i++) {
            std::cout << "arg:" << i << " value:'" << rawTableArgs_[i] << "'\n";
        }

        // Parse table arguments.
        // The basic schema is that args that have '=' have in them are arguments
        // to the engine. Anything else is passed straight through to SQLite as
        // the schema of the stream. The schema always has an integer 'ts' column first.
        // Example:
        // CREATE VIRTUAL TABLE web_reqs USING stream_engine (
        //     listen_port = 8000,
        //     batch_size = 1000,
        //     window_size_ms = 30000,
        //
        //     host VARCHAR(30),
        //     tag VARCHAR(80),
        //     latency_ms INTEGER,
        //
        // );
        //
        // Table schema will be:
        // CREATE TABLE x(
        //     ts INTEGER,
        //     host TEXT,
        //     tag TEXT,
        //     latency_ms INTEGER,
        // );

        // Parse table arguments.
        for (auto& arg : rawTableArgs_) {
            auto eqPos = arg.find('=');
            if (eqPos == string::npos) {
                tableSchema_.push_back(arg);
                auto spcPos = arg.find(' ');
                if (spcPos == string::npos) {
                    throw std::invalid_argument("Invalid column specification");
                }
                auto columnType = lcString(trimString(arg.substr(spcPos+1)));
                if (columnType == "integer") {
                    columnTypes_.push_back(ColumnType::INTEGER);
                } else if (columnType == "text") {
                    columnTypes_.push_back(ColumnType::TEXT);
                } else {
                    throw std::invalid_argument("Invalid column type. Must be INTEGER or TEXT.");
                }
                continue;
            }
            auto key = trimString(arg.substr(0, eqPos));
            auto value = arg.substr(eqPos+1);
            std::cout << "key='" << key << "', value=" << value << "\n";
            if (key == "listen_port") {
                listenPort_ = std::stoi(value); // Allow exceptions to propagate to fail table creation.
            } else if (key == "next_hop_service") {

                nextHopService_ = trimQuotes(trimString(value));

            } else if (key == "batch_size") {
                batchSize_ = std::stoi(value);
            } else if (key == "window_size_ms") {
                windowSizeMs_ = milliseconds(std::stoi(value));
            }
        }

        // If the table doesn't listen, and doesn't connect, then what good is it?
        if (listenPort_ <= 0 && nextHopService_.empty()) {
            throw std::invalid_argument("Table does not listen and/or connect to anything.");
        }

        // Construct CREATE TABLE call declare_vtab
        string vtabSchema = "CREATE TABLE x(";
        vtabSchema.append(joinVector(tableSchema_, ", "));
        vtabSchema.append(");");

        std::cout << "table schema: " << vtabSchema << "\n";

        if (sqlite3_declare_vtab(db_, vtabSchema.c_str())) {
            throw std::runtime_error("failed to initialize vtab object");
        }

        if ((zctx_ = zmq_ctx_new()) == nullptr) {
            throw std::runtime_error(zmq_strerror(zmq_errno()));
        }


        // Wire up the down-stream service, if specified.
        if (!nextHopService_.empty()) {
            if ((writeSock_ = zmq_socket(zctx_, ZMQ_PUSH)) == nullptr) {
                throw std::runtime_error(zmq_strerror(zmq_errno()));
            }
            std::cout << "Going to connect to: " << nextHopService_ << "\n";
            if (zmq_connect(writeSock_, nextHopService_.c_str()) == -1) {
                throw std::runtime_error(zmq_strerror(zmq_errno()));
            }
            // Ignore failure of this for now..
            zmq_setsockopt(writeSock_, ZMQ_LINGER, &lingerMs_, sizeof(lingerMs_));
        }

        // Wire up this service, if specified.
        if (listenPort_ > 0) {
            if ((readSock_ = zmq_socket(zctx_, ZMQ_PULL)) == nullptr) {
                throw std::runtime_error(zmq_strerror(zmq_errno()));
            }

            auto connStr = Sqlite3Ptr<char>(sqlite3_mprintf("tcp://*:%d", listenPort_));
            std::cout << "Going to bind to: " << connStr.get() << "\n";
            if (zmq_bind(readSock_, connStr.get()) == -1) {
                throw std::runtime_error(zmq_strerror(zmq_errno()));
            }

            // Ignore failure of this for now..
            zmq_setsockopt(readSock_, ZMQ_LINGER, &lingerMs_, sizeof(lingerMs_));
        }
    }

    ~VTable() {
        zmq_ctx_term(zctx_);
    }

    sqlite3_vtab* vTableBase() { return &vTableBase_; }
    

    void rename(string tableName) { tableName_ = tableName; }

    bool parse(ZmqMsg&& m, vector<ColumnValue>& columns) {
        char* valuesBegin = static_cast<char*>(m.data());
        char* valuesEnd = valuesBegin;
        char* dataEnd = valuesBegin + m.size();

        int col = 0;
        ColumnValue v;
        while(valuesEnd <= dataEnd) {
            while(*valuesEnd != ColSep && valuesEnd != dataEnd) valuesEnd++;

            if (columnTypes_[col] == ColumnType::INTEGER) {
                char* x = nullptr;
                v = std::make_tuple(ColumnType::INTEGER, string{}, std::strtoll(valuesBegin, &x, 10));
                // Failed parse of integer.
                if (x == valuesBegin) {
                    std::cout << "failed ts parse\n";
                    return false;
                }
            } else if (columnTypes_[col] == ColumnType::TEXT) {
                v = std::make_tuple(ColumnType::TEXT, string(valuesBegin, valuesEnd), -1L);
            }
            columns.push_back(v);
            valuesBegin = ++valuesEnd;
            col++;
        }

        if (col < columnTypes_.size()) {
            std::cout << "Didn't read enough columns from message: Expected:"
                      << columnTypes_.size() << " Got:" << col << "\n";
            return false;
        }

        return true;
    }

    std::unique_ptr<Row> readRow(bool dontWait) {
        if (!queuedRows_.empty()) {
            auto row = move(queuedRows_.front());
            queuedRows_.pop();
            return row;
        }
        ZmqMsg m;
        vector<ColumnValue> columns;

        currentRowId_++;
        if (zmq_msg_recv((zmq_msg_t*)m, readSock_, dontWait ? ZMQ_DONTWAIT : 0) == -1) {
            std::cout << "ZMQ error(" << zmq_errno() << "): " << zmq_strerror(zmq_errno()) << "\n";
            return std::unique_ptr<Row>(new Row{currentRowId_});
        }

        if (!parse(move(m), columns)) {
            return std::unique_ptr<Row>(new Row{currentRowId_});
        }

        return std::unique_ptr<Row>(new Row{currentRowId_, move(columns)});
    }

    void requeue(std::unique_ptr<Row> row) {
        queuedRows_.emplace(move(row));
    }

    bool batchConsumed(int64_t rowId, int64_t batchStart, milliseconds cursorOpenedMs) {
        if ((nowMs() - cursorOpenedMs) >= windowSizeMs_) {
            return true;
        }
        else if ((rowId - batchStart) >= batchSize_) {
            return true;
        }
        return false;
    }

    static constexpr int TS_COLUMN = 0;

    /**
     * The idxNum set in the output section of sqlite3_index_info is a bitmap to describe usage.
     * 0 - ts column (gt constraint)
     * 1 - ts column (ge constraint)
     */
    void bestIndex(sqlite3_index_info* pIndexInfo) {
        using index_constraint = typename sqlite3_index_info::sqlite3_index_constraint;
        double estimatedCost = 1.0e3;
        int nArg = 0;
        int index = 0;
        int tsIndex = -1;
        const index_constraint* pConstraint = pIndexInfo->aConstraint;
        for (int i=0; i < pIndexInfo->nConstraint; i++, pConstraint++) {
            if (pConstraint->usable == 0) {
                continue;
            }
            if (pConstraint->iColumn == TS_COLUMN) {
                if (pConstraint->op == SQLITE_INDEX_CONSTRAINT_GT) {
                    tsIndex = i;
                    index |= 1;
                } else if (pConstraint->op == SQLITE_INDEX_CONSTRAINT_GE) {
                    tsIndex = i;
                    index |= 2;
                }
                estimatedCost -= 100.0; /* filtering on ts isn't actually cheaper, but what the hell? */
            }
        }
        if (tsIndex >= 0) {
            pIndexInfo->aConstraintUsage[tsIndex].argvIndex = ++nArg;
        }
        if (pIndexInfo->nOrderBy==1) {
            // Time naturally goes forwards, so tell the engine it doesn't need a sort here.
            // This could be smarter, though.
            if (!pIndexInfo->aOrderBy[0].desc) {
                pIndexInfo->orderByConsumed = 1;
            }
        }
        pIndexInfo->idxNum = index;
        pIndexInfo->estimatedCost = estimatedCost;
        pIndexInfo->estimatedRows = 10; /* TODO: Track cursor creation time, read rate, and last row time to guess at this. */
    }

    bool forWrite() const { return !nextHopService_.empty(); }
    bool forRead() const { return listenPort_ > 0; }

    bool isWriteOnly() const { return forWrite() && !forRead(); }
    bool isReadOnly() const { return  !forWrite() && forRead(); }


    int write(sqlite_int64* pRowid, vector<sqlite3_value*> values) {
        vector<string> strValues;
        for (auto v : values) {
            strValues.push_back(string((char*)sqlite3_value_blob(v), sqlite3_value_bytes(v)));
        }
        std::cout << "Performing 'insert' into " << tableName_ << "\n";
        int i=0;
        for (auto& v: strValues) {
            std::cout << "col[" << i << "]:" << v << "\n";
            i++;
        }
        ZmqMsg m(joinVector(strValues, string(1, ColSep)));
        if (zmq_msg_send((zmq_msg_t*)m, writeSock_, 0) == -1) {
            std::cout << "er, send failed: " << zmq_strerror(zmq_errno()) << "\n";
            return SQLITE_FULL; // I guess?
        }
        return SQLITE_OK;
    }
};

class Cursor {
    sqlite3_vtab_cursor vTableCursorBase_;
    VTable* parent_;
    int64_t batchStart_;
    milliseconds cursorOpened_;

    std::unique_ptr<Row> row_;
    
public:
    Cursor(VTable* parent)
        : vTableCursorBase_{},
          parent_{parent},
          batchStart_{-1},
          cursorOpened_{nowMs()},
          row_{nullptr} {
    }

    sqlite3_vtab_cursor* vTableCursorBase() { return &vTableCursorBase_; }

    bool isEOF() {
        bool batchDone = parent_->batchConsumed(rowId(), batchStart_, cursorOpened_);
        if (batchDone && !row_->consumed()) {
            parent_->requeue(move(row_));
        }
        return batchDone;
    }

    int64_t rowId() const {
        return row_ ? row_->rowId() : -1;
    }

    int64_t seekUntilTime(milliseconds epoch, int seekType) {
        while (true) {
            int64_t batchStart = nextRow();
            if (seekType == SQLITE_INDEX_CONSTRAINT_GE) {
                if (row_->ts() >= epoch) {
                    return batchStart;
                }
            } else if (seekType == SQLITE_INDEX_CONSTRAINT_GT) {
                if (row_->ts() > epoch) {
                    return batchStart;
                }
            }
            std::cout << "Row too old: " << *row_ << "\n";
        }
    }
    
    int64_t nextRow() {
        auto newRow = parent_->readRow(false);
        while (!newRow->valid()) { newRow = move(parent_->readRow(false)); }
        row_ = move(newRow);
        //std::cout << "nextRow(): " << *row_ << "\n";
        return row_->rowId();
    }

    int filter(int idxNum, std::vector<sqlite3_value*> values) {
        cursorOpened_ = nowMs();
        //std::cout << "filter() argv size:" << values.size() << "\n";
        if (idxNum == 0) {
            batchStart_ = nextRow();
            return SQLITE_OK;
        }
        milliseconds startTime = milliseconds(sqlite3_value_int64(values[0]));
        int seekType = -1;
        if (idxNum & 1) {
            seekType = SQLITE_INDEX_CONSTRAINT_GT;
            std::cout << "Filtering on tsGT:" << startTime.count() << "\n";
        } else if (idxNum & 2) {
            seekType = SQLITE_INDEX_CONSTRAINT_GE;
            std::cout << "Filtering on tsGE:" << startTime.count() << "\n";
        }
        batchStart_ = seekUntilTime(startTime, seekType);
        return SQLITE_OK;
    }

    const Row* row() const { row_->setConsumed(); return row_.get(); }
};

int setab_create(sqlite3* db, void* pAux, int argc, const char* const* argv, sqlite3_vtab** ppVTab, char** pzErr) {
    try {
        auto* tab = new VTable(db, argv[2], vector<string>(argv+3, argv+argc));
        *ppVTab = tab->vTableBase();
    } catch (const std::runtime_error& ex) {
        return SQLITE_ERROR;
    }
    return SQLITE_OK;
}

int setab_destroy(sqlite3_vtab *pVTab) {
    VTable* table = reinterpret_cast<VTable*>(pVTab);
    delete table;
    return SQLITE_OK;
}

int setab_bestindex(sqlite3_vtab* pVTab, sqlite3_index_info* pIndexInfo) {
    VTable* table = reinterpret_cast<VTable*>(pVTab);
    table->bestIndex(pIndexInfo);
    return SQLITE_OK;
}

int setab_open(sqlite3_vtab* pVTab, sqlite3_vtab_cursor** ppCursor) {
    VTable* table = reinterpret_cast<VTable*>(pVTab);
    if (table->isWriteOnly()) {
        return SQLITE_ERROR;
    }
    Cursor* cursor = new Cursor(table);
    *ppCursor = cursor->vTableCursorBase();
    return SQLITE_OK;
}

int setab_close(sqlite3_vtab_cursor* pCursor) {
    Cursor* cursor = reinterpret_cast<Cursor*>(pCursor);
    std::cout << "Closing cursor\n";
    delete cursor;
    return SQLITE_OK;
}

int setab_eof(sqlite3_vtab_cursor* pCursor) {
    Cursor* cursor = reinterpret_cast<Cursor*>(pCursor);
    return cursor->isEOF();
}

int setab_filter(sqlite3_vtab_cursor* pCursor, int idxNum, const char* idxStr, int argc, sqlite3_value** argv) {
    Cursor* cursor = reinterpret_cast<Cursor*>(pCursor);
    return cursor->filter(idxNum, vector<sqlite3_value*>(argv, argv+argc));
}

int setab_next(sqlite3_vtab_cursor* pCursor) {
    Cursor* cursor = reinterpret_cast<Cursor*>(pCursor);
    cursor->nextRow();
    return SQLITE_OK;
}

int setab_column(sqlite3_vtab_cursor* pCursor, sqlite3_context* pContext, int N) {
    Cursor* cursor = reinterpret_cast<Cursor*>(pCursor);
    //std::cout << "xColumn(" << N << "): ";
    const auto& col = cursor->row()->columns()[N];
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

int setab_rowid(sqlite3_vtab_cursor* pCursor, sqlite_int64* pRowid) {
    Cursor* cursor = reinterpret_cast<Cursor*>(pCursor);
    *pRowid = cursor->rowId();
    return SQLITE_OK;
}

int setab_update(sqlite3_vtab* pVTab, int argc, sqlite3_value **argv, sqlite_int64* pRowid) {
    VTable* table = reinterpret_cast<VTable*>(pVTab);
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
    VTable* table = reinterpret_cast<VTable*>(pVTab);
    table->rename(zNew); 
    return SQLITE_OK;
}

sqlite3_module setab_module {
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

int main(int argc, char** argv) {
    const char* dbName = "test.db";
    bool secondary = false;
    if (argc > 1) {
        dbName = "test1.db";
        secondary = true;
    }
    sqlite3* db = nullptr;
    int rc = sqlite3_open(dbName, &db);
    if (rc) {
        std::cout << "Couldn't open db: " << sqlite3_errmsg(db) << "\n";
        sqlite3_close(db);
        return 1;
    }
    if (sqlite3_create_module_v2(db, "setab", &setab_module, nullptr, nullptr)) {
        std::cout << "Couldn't make module: " << sqlite3_errmsg(db) << "\n";
        sqlite3_close(db);
        return 1;
    }
    std::cout << "Initialized setab module..\n";

    sqlite3_stmt* runTbl = nullptr, *insertTbl = nullptr;

    const char* createSQL = R"SQL(
CREATE VIRTUAL TABLE test_table1 USING setab (
    tag TEXT,
    latency INTEGER,

    listen_port=6000,
    batch_size=10,
    window_size_ms=60000
);
)SQL";

    const char* create2SQL = R"SQL(
CREATE VIRTUAL TABLE test_table2 USING setab (
    tag_group TEXT,
    latency_sum INTEGER,
    latency_count INTEGER,
    
    next_hop_service='tcp://localhost:6001',
);
)SQL";

    const char* create3SQL = R"SQL(
CREATE VIRTUAL TABLE test_table3 USING setab (
    tag_group TEXT,
    latency_sum INTEGER,
    latency_count INTEGER,

    listen_port=6001,
    batch_size=10,
    window_size_ms=60000
);
)SQL";

    const char* runSQL = R"SQL(
SELECT ts, sum(latency), count(latency), group_concat(tag) FROM test_table1;
)SQL";

    const char* insertSQL = R"SQL(
INSERT INTO test_table2 (ts,tag_group,latency_sum,latency_count) VALUES(?,?,?,?);
)SQL";

    const char* run2SQL = "SELECT ts, latency_sum, latency_count, tag_group FROM test_table3;";

    const char* table1SQL = nullptr;
    const char* table2SQL = nullptr;
    const char* query1SQL = nullptr;
    const char* query2SQL = nullptr;

    if (secondary) {
        table1SQL = create3SQL;
        query1SQL = run2SQL;
    } else {
        table1SQL = createSQL;
        table2SQL = create2SQL;
        query1SQL = runSQL;
        query2SQL = insertSQL;
    }

    char* errorMsg = nullptr;
    if (table1SQL) {
        sqlite3_exec(db, table1SQL, nullptr, nullptr, &errorMsg);
        if (errorMsg) {
            std::cout << "Unable to create vtable: " << errorMsg;
            sqlite3_free(errorMsg);
            sqlite3_close(db);
            return 1;
        }
        std::cout << "Created test_table1..\n";
    }

    if (table2SQL) {
        sqlite3_exec(db, table2SQL, nullptr, nullptr, &errorMsg);
        if (errorMsg) {
            std::cout << "Unable to create vtable: " << errorMsg;
            sqlite3_free(errorMsg);
            sqlite3_close(db);
            return 1;
        }
        std::cout << "Created test_table2..\n";
    }

    if (query1SQL) {
        if (sqlite3_prepare_v2(db, query1SQL, -1, &runTbl, nullptr)) {
            std::cout << "Unable to compile runTbl query: " << sqlite3_errmsg(db) << "\n";
            sqlite3_close(db);
            return 1;
        }
        std::cout << "Compiled query: " << query1SQL << "\n";
    }

    if (query2SQL) {
        if (sqlite3_prepare_v2(db, insertSQL, -1, &insertTbl, nullptr)) {
            std::cout << "Unable to compile insertTbl query: " << sqlite3_errmsg(db) << "\n";
            sqlite3_close(db);
            return 1;
        }
        std::cout << "Compiled query: " << query2SQL << "\n";
    }

    auto ts = nowMs();
    while (true) {
        while ((rc = sqlite3_step(runTbl)) == SQLITE_ROW) {
            int64_t rts = sqlite3_column_int64(runTbl, 0);
            int64_t latencySum = sqlite3_column_int64(runTbl, 1);
            int64_t latencyCount = sqlite3_column_int64(runTbl, 2);
            const unsigned char* value = sqlite3_column_text(runTbl, 3);
            std::cout << "ts: " << rts
                      << " sum:" << latencySum
                      << " cnt:" << latencyCount
                      << " value: '" << value << "'\n";
            if (insertTbl) {
                sqlite3_bind_int64(insertTbl, 1, rts);
                sqlite3_bind_text(insertTbl, 2, (char*)value, -1, SQLITE_TRANSIENT);
                sqlite3_bind_int64(insertTbl, 3, latencySum);
                sqlite3_bind_int64(insertTbl, 4, latencyCount);

                if (sqlite3_step(insertTbl) != SQLITE_DONE) {
                    std::cout << "Failed to write to test_table2:" << sqlite3_errmsg(db) << "\n";
                }
                sqlite3_reset(insertTbl);
            }
        }
        std::cout << "Query finished.\n";
        if (rc != SQLITE_DONE) {
            std::cout << "query exited with error: " << sqlite3_errmsg(db) << "\n";
            sqlite3_finalize(runTbl);
            sqlite3_close(db);
            return 1;
        }
        sqlite3_reset(runTbl);
    }
    return 0;
}
