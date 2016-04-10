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

#include "Setab.h"

#include <folly/dynamic.h>
#include <folly/json.h>

int main(int argc, char** argv) {
    const char* dbName;
    folly::dynamic queryConfig = folly::dynamic::object;
    if (argc < 3) {
        std::cout << "Usage: " << argv[0] << " DB QUERY_CONFIG\n";
        return 1;
    } else {
        dbName = argv[1];
        string configContent;
        folly::readFile(argv[2], configContent);
        queryConfig = folly::parseJson(configContent);
    }

    SetabRegistry tableRegistry;
    Sqlite3Db db;
    try {
        db.open(dbName);
    } catch (const Sqlite3Exception& ex) {
        std::cout << "Couldn't open db: " << ex.what();
        return 1;
    }

    if (sqlite3_create_module_v2(db.raw(), "setab", Sqlite3SetabModule(), &tableRegistry, nullptr)) {
        std::cout << "Couldn't make module: " << db.errmsg() << "\n";
        return 1;
    }
    std::cout << "Initialized setab module..\n";

    if (!queryConfig.count("tables") ||
        !queryConfig.count("selections") ||
        !queryConfig.count("insertions")) {
        std::cout << "Configuration seems to be missing 'tables', 'selections', or 'insertions'\n";
        return 1;
    }

    std::vector<sqlite3_stmt*> selections;
    std::vector<sqlite3_stmt*> insertions;

    for (const auto& createSQL : queryConfig["tables"]) {
        try {
            db.exec(createSQL.c_str());
        } catch (const Sqlite3Exception& ex) {
            std::cout << "Unable to create table: " << ex.what();
            return 1;
        }
    }

    for (const auto& querySQL : queryConfig["selections"]) {
        selections.push_back(nullptr);
        if (sqlite3_prepare_v2(db.raw(), querySQL.c_str(), -1, &selections.back(), nullptr)) {
            std::cout << "Unable to compile selection query: " << db.errmsg() << "\n";
            return 1;
        }
        std::cout << "Compiled query: " << querySQL << "\n";
    }

    for (const auto& insertData : queryConfig["insertions"]) {
        if (!insertData.count("query")) {
            std::cout << "No query associated for insertion. Need key: `query`.\n";
            return 1;
        }
        if (!insertData.count("selections")) {
            std::cout << "No data to insert for insertion. Need key: `selections`.\n";
        }
        const char* insertSQL = insertData["query"].c_str();
        insertions.push_back(nullptr);
        if (sqlite3_prepare_v2(db.raw(), insertSQL, -1, &insertions.back(), nullptr)) {
            std::cout << "Unable to compile insertion query: " << db.errmsg() << "\n";
            return 1;
        }
        std::cout << "Compiled query: " << insertSQL << "\n";
    }

    std::unordered_set<size_t> fucks;
    fucks.reserve(selections.size());
    while (true) {
        int i=0;
        // Advance all selections one step.
        // Reset them if they finish, and abort if there's an error.
        for (auto stmt : selections) {
            int rc = sqlite3_step(stmt);
            switch (rc) {
                case SQLITE_ROW:
                    std::cout << "Got row from: " << i << "\n";
                    fucks.insert(i);
                    break;
                case SQLITE_DONE:
                    std::cout << "Completed: " << i << "\n";
                    sqlite3_reset(stmt);
                    break;
                default:
                    std::cout << "Query `" << sqlite3_sql(stmt) << "` experienced an error:" << db.errmsg();
                    std::cout << "Aborting.\n";
                    return 1;
            }
            i++;
        }

        int j=0;
        // Perform all the requested insertions. Failed writes also abort the engine.
        for (auto insertStmt : insertions) {
            int c=1;
            bool canInsert = true;
            for (auto& selectData : queryConfig["insertions"][j]["selections"].items()) {
                size_t selectIndex = selectData.first.asInt();
                if (!fucks.count(selectIndex)) {
                    canInsert = false;
                    continue;
                }
                sqlite3_stmt* selectStmt = selections[selectIndex];
                for (auto& columnIndex : selectData.second) {
                    //const char* colName = sqlite3_column_name(selectStmt, columnIndex.getInt());
                    sqlite3_value* value = sqlite3_column_value(selectStmt, columnIndex.getInt());
                    sqlite3_bind_value(insertStmt, c, value);
                    c++;
                }
            }
            if (canInsert && sqlite3_step(insertStmt) != SQLITE_DONE) {
                std::cout << "Failed to write to table " << j << "\n";
                std::cout << "Aborting.\n";
                return 1;
            }
            sqlite3_reset(insertStmt);
            j++;
        }
        fucks.clear();
    }
    return 0;
}
