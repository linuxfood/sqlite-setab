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

#include <sqlite3.h>

#include <folly/Range.h>

#include <cassert>
#include <memory>
#include <string>

template<class T>
struct Sqlite3FreeDeleter {
    void operator()(T* ptr) { sqlite3_free(ptr); }
};

template<class T>
using Sqlite3Ptr = typename std::unique_ptr<T, Sqlite3FreeDeleter<T>>;

class Sqlite3Exception : public std::runtime_error {
public:
    Sqlite3Exception(const char* what) : std::runtime_error(what) {}
};

using Sqlite3ValueRange = folly::Range<sqlite3_value*>;

class Sqlite3Stmt {
private:
    struct StmtFinalizer {
        void operator()(sqlite3_stmt* stmt) {
            int errc = sqlite3_finalize(stmt);
            assert(errc == SQLITE_OK);
        }
    };
public:
    explicit Sqlite3Stmt(sqlite3_stmt* stmt) noexcept :
        state_(SQLITE_DONE),
        stmt_(stmt, StmtFinalizer()),
        stmtMetadata_(extractKVPairs(extractComment(sql()))) {
        assert(stmt != nullptr);
    }

    Sqlite3Stmt(const Sqlite3Stmt&) = default;
    Sqlite3Stmt& operator=(const Sqlite3Stmt&) = default;

    Sqlite3Stmt(Sqlite3Stmt&&) = default;
    Sqlite3Stmt& operator=(Sqlite3Stmt&&) = default;

    int parameterCount() const {
        return sqlite3_bind_parameter_count(raw());
    }

    std::vector<folly::StringPiece> parameterNames() const {
        std::vector<folly::StringPiece> out;
        for (int i=1; i<=parameterCount(); i++) {
            out.push_back(sqlite3_bind_parameter_name(stmt_.get(), i));
        }
        return out;
    }

    const std::unordered_map<std::string, std::string>& metadata() const {
        return stmtMetadata_;
    }

    std::vector<std::string> columnNames() const {
        std::vector<std::string> names;
        for (int i=0; i<sqlite3_column_count(raw()); i++) {
            names.push_back(sqlite3_column_name(raw(), i));
        }
        return names;
    }

    sqlite3_value* columnValue(int i) const { return sqlite3_column_value(raw(), i); }

    void bindValue(int i, sqlite3_value* v) {
        if (sqlite3_bind_value(raw(), i, v) != SQLITE_OK) {
            throw Sqlite3Exception(errmsg());
        }
    }

    bool readonly() const {
        return sqlite3_stmt_readonly(raw());
    }

    int step() {
        state_ = sqlite3_step(raw());
        return state_;
    }

    int state() const { return state_; }

    int reset() {
        return sqlite3_reset(raw());
    }

    folly::StringPiece sql() const {
        return sqlite3_sql(raw());
    }

    sqlite3_stmt* raw() const { return stmt_.get(); }


    // Return the database error message. Statements don't have error messages.
    const char* errmsg() const {
        return sqlite3_errmsg(sqlite3_db_handle(raw()));
    }

    bool operator==(const Sqlite3Stmt& o) const {
        return raw() == o.raw();
    }
private:
    int state_;
    std::shared_ptr<sqlite3_stmt> stmt_;
    std::unordered_map<std::string, std::string> stmtMetadata_;
};

class Sqlite3Db {
public:
    Sqlite3Db() noexcept {}
    ~Sqlite3Db() { if (db_ != nullptr) { close(); } }

    void open(const std::string& path) {
        if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
            throw Sqlite3Exception(sqlite3_errmsg(db_));
        }
    }

    // Called automatically on destruction.
    void close() {
        sqlite3_close(db_);
        db_ = nullptr;
    }

    class TransactionProxy {
    public:
        TransactionProxy(Sqlite3Db* db) : db_(db) {
            db_->exec("BEGIN");
        }
        ~TransactionProxy() {
            if (!finished) {
                rollback();
            }
        }

        void commit() {
            assert(finished != true);
            db_->exec("COMMIT");
            finished = true;
        }
        void rollback() {
            assert(finished != true);
            db_->exec("ROLLBACK");
            finished = true;
        }
    private:
        Sqlite3Db* db_{nullptr};
        bool finished{false};
    };

    // Start an exception safe transaction.
    // You need only hold a reference to the TransactionProxy
    // and call `commit()` on it when you're done.
    TransactionProxy begin() {
        return TransactionProxy(this);
    }

    void exec(const std::string& sql) {
        char* errorMsg = nullptr;
        sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errorMsg);
        if (errorMsg) {
            throw Sqlite3Exception(errorMsg);
        }
    }

    template<class ... Args>
    void exec(const std::string& sql, Args... args) {
    }

    template<class T>
    void create_module(const std::string& moduleName, sqlite3_module* module, T* moduleParam) {
        if (sqlite3_create_module_v2(db_, moduleName.c_str(), module, moduleParam, nullptr)) {
            throw Sqlite3Exception(errmsg());
        }
    }

    sqlite3* raw() { return db_; }
    const char* errmsg() const { return sqlite3_errmsg(db_); }

private:
    sqlite3* db_{nullptr};
};

