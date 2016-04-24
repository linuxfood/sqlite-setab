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

// Enable debugging output in the test harness always.
#define SETAB_ROWBLOCK_DEBUG 1
#include "setab/RowBuffer.h"


#include <folly/futures/Future.h>
#include <gtest/gtest.h>

using namespace std::chrono_literals;
using SmallRowBlock = RowBlockImpl<10>;
using SmallRowCursor = RowCursorImpl<SmallRowBlock>;
using SmallRowBuffer = RowBufferImpl<SmallRowBlock>;

namespace {
    ColumnValue makeColumn(std::string value) {
        return ColumnValue(ColumnType::TEXT, value, -1);
    }
    ColumnValue makeColumn(int64_t value) {
        return ColumnValue(ColumnType::INTEGER, "", value);
    }
    Row makeRow(int64_t id, milliseconds ts, vector<ColumnValue> extraColumns = {}) {
        vector<ColumnValue> cols = { makeColumn(ts.count()) };
        cols.insert(cols.cend(), extraColumns.begin(), extraColumns.end());
        return Row(id, cols);
    }
}

TEST(RowBlock, OneInsert) {
    auto buffer = SmallRowBlock::create();
    Row r = makeRow(4, 10ms, { makeColumn("hello") });
    EXPECT_EQ(true, r.valid());

    EXPECT_EQ(0, buffer->size());

    buffer->appendRow(r);
    EXPECT_EQ(false, r.valid());

    EXPECT_EQ(1, buffer->size());
}

TEST(RowBlock, MaxInsert) {
    auto buffer = SmallRowBlock::create();
    auto minTs = 0ms;
    int i=0;
    bool appended = false;
    Row _;
    do {
        i++;
        Row r = makeRow(i, minTs);
        appended = buffer->appendRow(r);
        minTs = minTs + 2ms;
    } while(appended);
    EXPECT_EQ(10, buffer->size());
    EXPECT_EQ((0ms).count(), buffer->minMaxTime().first.count());
    EXPECT_EQ((18ms).count(), buffer->minMaxTime().second.count());
    EXPECT_EQ(1, buffer->front().rowId());
    EXPECT_EQ(10, buffer->back().rowId());
}

TEST(RowBuffer, AppendExtend) {
    SmallRowBuffer buffer(100, 6000, 9600ms);
    auto minTs = 0ms;
    for (int i=0; i < 15; ++i) {
        buffer.appendRow(makeRow(i, minTs));
        minTs = minTs + 1ms;
    }
    auto stats = buffer.stats();
    EXPECT_EQ(15, stats.totalRows);
    // 15 * 64 bytes
    EXPECT_EQ(960, stats.totalBytes);
    EXPECT_EQ(2, stats.totalBlocks);
}

TEST(RowBuffer, CursorLiveBlocks) {
    SmallRowBuffer buffer(30, 6000, 9600ms);
    auto minTs = 0ms;
    SmallRowCursor c = buffer.getCursor();
    EXPECT_EQ(false, c.next()) << "advanced cursor on empty buffer";
    for (int i=0; i < 40; ++i) {
        buffer.appendRow(makeRow(i, minTs));
        minTs = minTs + 1ms;
    }
    EXPECT_EQ(3, buffer.stats().totalBlocks);
    EXPECT_EQ(30, buffer.stats().totalRows);

    for(int j=0; j<10; j++) {
        EXPECT_EQ(j, c.get().rowId());
        EXPECT_EQ(true, c.next());
    }

    SmallRowCursor c2 = buffer.getCursor();
    EXPECT_EQ(10, c2.get().rowId());
}

TEST(RowBuffer, CursorSeek) {
    SmallRowBuffer buffer(20, 3000, 9600ms);
    auto minTs = 0ms;
    SmallRowCursor c = buffer.getCursor();
    for (int i=0; i < 20; ++i) {
        buffer.appendRow(makeRow(i, minTs));
        minTs = minTs + 1ms;
    }

    EXPECT_EQ(true, c.seek(11ms));
    EXPECT_EQ(11, c.get().rowId());
}

TEST(RowBuffer, ThreadUse) {
    SmallRowBuffer buffer(20, 3000, 9600ms);
    std::thread reader([&buffer]() {
            SmallRowCursor c = buffer.getCursor();
            // Wait indefinitely for a write.
            EXPECT_EQ(true, buffer.waitForWrite());
            ASSERT_EQ(true, c.get().valid());
            EXPECT_EQ(30ms, c.get().ts());
            });
    // TODO: Fix this synchronization issue.
    buffer.appendRow(makeRow(1, 30ms));
    buffer.appendRow(makeRow(2, 31ms));
    std::this_thread::sleep_for(20ms);
    buffer.appendRow(makeRow(3, 32ms));
    buffer.appendRow(makeRow(4, 33ms));
    buffer.appendRow(makeRow(5, 34ms));
    reader.join();
}
