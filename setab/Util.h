#pragma once

#include "setab/ZmqMsg.h"

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

using std::swap;
using std::move;
using std::string;
using std::to_string;
using std::vector;
using std::unordered_map;

using namespace std::chrono;
using namespace std::chrono_literals;

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

// Extracts content of C-Style comments from text.
// /* some comment text */ = " some comment text "
// /**/ = ""
folly::StringPiece extractComment(folly::StringPiece query);

// Extracts key=value pairs from some text.
std::unordered_map<std::string, std::string> extractKVPairs(folly::StringPiece text);
