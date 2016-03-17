#pragma once

#include <algorithm>
#include <cstdlib>
#include <cctype>
#include <chrono>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <tuple>
#include <thread>
#include <queue>
#include <vector>

#include <zeromq/zmq.h>
#include <sqlite3/sqlite3.h>

#include "ZmqMsg.h"


using std::swap;
using std::move;
using std::string;
using std::to_string;
using std::vector;
using namespace std::chrono;

constexpr char ColSep = '\036';

milliseconds nowMs() {
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch());
}

string trimString(string inString) {
    auto b = inString.begin();
    auto e = inString.end();

    while(std::isspace(*b)) { b++; }
    while(std::isspace(*e)) { e--; }

    return string(b, e);
}

string trimQuotes(string inString) {
    auto b = inString.begin();
    auto e = inString.begin();

    while (*b == '\'') { b++; }
    e = b;
    while (*e != '\'' && e != inString.end()) { e++; }

    return string(b, e);
}

string lcString(string inString) {
    for (auto& c : inString) {
        c = std::tolower(c);
    }
    return inString;
}

string joinVector(const vector<string>& c, string delim=",") {
    string result;
    auto i = std::begin(c);
    result.append(*i);
    i++;
    while (i != std::end(c)) {
        result.append(delim).append(*i);
        i++;
    }
    return result;
}

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

