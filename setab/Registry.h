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

#include <folly/Synchronized.h>

class Setab;

/*
 * The registry provides a way to lookup existing tables for various purposes.
 * The primary usecase is the 'window()' operator which creates a buffer
 * internal to the table so that joins can operate over time ranges.
 * See the note in `Window.h` for details on that operator.
 */
class SetabRegistry {
    folly::Synchronized<unordered_map<string, Setab*>> liveTables_;
public:

    void addTable(string tableName, Setab* vtab) {
        liveTables_->insert({tableName, vtab});
    }

    Setab* getTable(const string& tableName) {
        return liveTables_->operator[](tableName);
    }

    void removeTable(const string& tableName) {
        liveTables_->erase(tableName);
    }

    void renameTable(const string& oldName, string newName) {
        SYNCHRONIZED(liveTables_) {
            Setab* table = liveTables_[oldName];
            liveTables_[newName] = table;
            liveTables_.erase(oldName);
        }
    }

};
