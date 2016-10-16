#include "Util.h"

#include <folly/String.h>

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

string joinVector(const vector<string>& c, string delim) {
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

folly::StringPiece extractComment(folly::StringPiece query) {
    size_t startPos = 0;
    size_t endPos = 0;
    for (size_t i=0; i<query.size()-1; i++) {
        if (query[i] == '/' && query[i+1] == '*') {
            startPos = i+2;
            i++;
        }
        if (query[i] == '*' && query[i+1] == '/') {
            endPos = i;
            break;
        }
    }
    if (startPos > 0 && endPos > 0 && endPos > startPos) {
        return query.subpiece(startPos, endPos - startPos);
    }
    return folly::StringPiece();
}

std::unordered_map<std::string, std::string> extractKVPairs(folly::StringPiece text) {
    std::unordered_map<std::string, std::string> out;
    std::vector<folly::StringPiece> parts;

    folly::split(' ', text, parts, /* ignoreEmpty */ true);
    for (const auto part : parts) {
        folly::StringPiece keyPart, valuePart;
        folly::split('=', part, keyPart, valuePart);
        if (keyPart.size() > 0 && valuePart.size() > 0) {
            out.insert(std::make_pair(keyPart.str(), valuePart.str()));
        }
    }
    return out;
}
