#include "Util.h"

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
