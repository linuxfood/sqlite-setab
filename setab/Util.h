#pragma once

#include "ZmqMsg.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <queue>
#include <random>
#include <string>
#include <thread>
#include <unordered_map>
#include <tuple>
#include <vector>

#include <folly/Range.h>
#include <sqlite3/sqlite3.h>
#include <zeromq/zmq.h>

using std::swap;
using std::move;
using std::string;
using std::to_string;
using std::vector;
using std::unordered_map;

using namespace std::chrono;

constexpr char ColSep = '\036';

milliseconds nowMs();

string trimString(string inString);
string trimQuotes(string inString);

string lcString(string inString);

string joinVector(const vector<string>& c, string delim=",");

template<class T>
T randomValue(T lowerBound, T upperBound) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(lowerBound, upperBound);
    return dis(gen);
}

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

class Sqlite3Db {
    sqlite3* db_{nullptr};
public:
    Sqlite3Db() noexcept {}

    void open(const string& path) {
        if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
            throw Sqlite3Exception(sqlite3_errmsg(db_));
        }
    }

    void close() {
        sqlite3_close(db_);
        db_ = nullptr;
    }

    void exec(const string& sql) {
        char* errorMsg = nullptr;
        sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errorMsg);
        if (errorMsg) {
            throw Sqlite3Exception(errorMsg);
        }
    }

    sqlite3* raw() { return db_; }
    const char* errmsg() const { return sqlite3_errmsg(db_); }

    ~Sqlite3Db() {
        close();
    }
};

