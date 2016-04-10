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

#pragma once

#include "setab/Util.h"
#include "setab/Registry.h"
#include "setab/Row.h"

#include <folly/Conv.h>
#include <folly/FileUtil.h>
#include <folly/String.h>
#include <folly/Synchronized.h>

class Setab {
    sqlite3_vtab vTableBase_; /* Must come first */
    sqlite3* db_;
    SetabRegistry* registry_;

    string tableName_;
    vector<Column> columns_;
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
    Setab(sqlite3* db, SetabRegistry* registry, string tableName, vector<string> rawTableArgs)
        : vTableBase_{},
          db_{db},
          registry_{registry},
          tableName_{tableName},
          columns_{{"ts", ColumnType::INTEGER}},
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
                vector<string> segments;
                folly::split(' ', arg, segments, true);
                if (segments.size() != 2) {
                    throw std::invalid_argument("Invalid column description. Expected `name`' '`type`");
                }
                Column newCol;
                newCol.name = segments[0];

                auto columnType = lcString(trimString(segments[1]));
                if (columnType == "integer") {
                    newCol.type = ColumnType::INTEGER;
                } else if (columnType == "text") {
                    newCol.type = ColumnType::TEXT;
                } else {
                    throw std::invalid_argument("Invalid column type. Must be INTEGER or TEXT.");
                }
                columns_.push_back(newCol);
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
        string vtabSchema = tableSchema();
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
        registry_->addTable(tableName_, this);
    }

    ~Setab() {
        zmq_ctx_term(zctx_);
        registry_->removeTable(tableName_);
    }

    sqlite3_vtab* vTableBase() { return &vTableBase_; }

    const vector<Column>& tableColumns() const { return columns_; }

    string tableSchema() const {
        vector<string> tmp;
        for (auto& col : columns_) {
            string colType = col.type == ColumnType::INTEGER ? "INTEGER" : "TEXT";
            tmp.push_back(col.name + " " + colType);
        }
        return "CREATE TABLE x(" + folly::join(", ", tmp) + ");";
    }
    

    void rename(string tableName) {
        registry_->renameTable(tableName_, tableName);
        tableName_ = tableName;
    }

    bool parse(ZmqMsg&& m, vector<ColumnValue>& columns) {
        folly::StringPiece rowData{static_cast<const char*>(m.data()), m.size()};
        std::cout << "Raw message:`" << folly::cEscape<string>(rowData) << "`\n";

        vector<folly::StringPiece> rawColumns;
        folly::split(ColSep, rowData, rawColumns);

        if (rawColumns.size() != columns_.size()) {
            std::cout << "Message has wrong column count. "
                      << rawColumns.size() << " != " << columns_.size() << "\n";
            return false;
        }

        for (size_t i=0; i < columns_.size(); i++) {
            ColumnValue v;
            switch (columns_[i].type) {
                case ColumnType::INTEGER:
                    try {
                        v = std::make_tuple(ColumnType::INTEGER, string{}, folly::to<int64_t>(rawColumns[i]));
                    } catch (const std::range_error& ex) {
                        std::cout << "Invalid message. Expected INTEGER, got TEXT.\n";
                        return false;
                    }
                    break;
                case ColumnType::TEXT:
                    v = std::make_tuple(ColumnType::TEXT, rawColumns[i].str(), -1);
                    break;
                default:
                    std::cout << "UNKNOWN COLUMN TYPE: " << static_cast<int>(columns_[i].type) << "\n";
                    return false;
            }

            columns.push_back(v);
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

    const string& tableName() const { return tableName_; }
    SetabRegistry* registry() { return registry_; }
};

class SetabCursor {
    sqlite3_vtab_cursor vTableCursorBase_;
    Setab* parent_;
    int64_t batchStart_;
    milliseconds cursorOpened_;

    std::unique_ptr<Row> row_;
    
public:
    SetabCursor(Setab* parent)
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

sqlite3_module* Sqlite3SetabModule();
