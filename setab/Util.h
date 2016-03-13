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

#include "zeromq/zmq.h"
#include "sqlite3.h"
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
