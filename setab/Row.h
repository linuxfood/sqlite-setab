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

#include "setab/Sqlite.h"
#include "setab/Util.h"


enum class ColumnType {
    INTEGER = SQLITE_INTEGER,
    TEXT = SQLITE_TEXT,
};

using ColumnValue = std::tuple<ColumnType, string, int64_t>;

struct Column {
    string name;
    ColumnType type;
};

/**
 * Rows are in the following format:
 * %ld[\036[%s][%ld]]+
 * And \036 is the byte for 'record separator'.
 * Where %ld is a 64bit signed integer in utf-8 base-10.
 * And %s is arbitrary bytes other than \036.
 */
class Row {
    int64_t rowId_;
    size_t cachedSize_;
    vector<ColumnValue> columns_;

    static size_t computeSize(const vector<ColumnValue>& cols) {
        size_t sz = sizeof(rowId_) + sizeof(cachedSize_) + sizeof(columns_);
        for (const auto& col : cols) {
            sz += sizeof(ColumnValue);
            if (std::get<0>(col) == ColumnType::TEXT) {
                sz += std::get<1>(col).size();
            }
        }
        return sz;
    }
public:

    Row() : rowId_{-1}, cachedSize_{computeSize({})}, columns_{} {}

    explicit Row(int64_t rowId)
        : rowId_{rowId}, cachedSize_{computeSize({})}, columns_{} {}

    explicit Row(int64_t rowId, vector<ColumnValue> columns)
        : rowId_{rowId}, cachedSize_{computeSize(columns)}, columns_{columns.begin(), columns.end()} {
    }

    int64_t rowId() const { return rowId_; }

    milliseconds ts() const {
        return milliseconds(std::get<2>(columns_[0]));
    }

    const vector<ColumnValue>& columns() const { return columns_; }

    bool valid() const {
        return !columns_.empty();
    }

    bool consumed() const { return false; }

    void setConsumed() {}

    size_t size() const {
        return cachedSize_;
    }
};

inline std::ostream& operator<<(std::ostream& o, const Row& r) {
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
