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

#include "setab/Row.h"
#include "setab/Util.h"

#include <array>
#include <atomic>
#include <shared_mutex>

#include <folly/SharedMutex.h>

template <class RBT> class RowCursorImpl;

// A block of rows. The rows are stored in order that they arrived in the
// stream, but the block does track the min and max times in the block so that
// we can efficiently filter through blocks. A row block also maintains a pointer
// to the next block in the chain.
template<size_t BlockSz, class LockT = folly::SharedMutex>
class RowBlockImpl : public std::enable_shared_from_this<RowBlockImpl<BlockSz, LockT>> {
public:
    using Lock = LockT;
    using SharedHolder = std::shared_lock<Lock>;
    using ExclusiveHolder = std::unique_lock<Lock>;

    static constexpr size_t BlockSize = BlockSz;

    static std::shared_ptr<RowBlockImpl<BlockSz, Lock>> create() {
        return std::make_shared<RowBlockImpl<BlockSz, Lock>>();
    }

    bool appendRow(Row& row) {
        auto guard(lockExclusive());
        if (blockUsed == rows.max_size()) {
            return false;
        }
        if (blockUsed>0) {
            minTime = std::min(minTime, row.ts());
            maxTime = std::max(maxTime, row.ts());
        } else {
            minTime = maxTime = row.ts();
        }
        rows[blockUsed] = move(row);
        blockUsed++;
        blockSize += row.size();
        return true;
    }

    std::shared_ptr<RowBlockImpl<BlockSz, Lock>> next() const {
        auto guard(lockShared());
        return nextBlock;
    }

    void setNextBlock(std::shared_ptr<RowBlockImpl<BlockSz, Lock>> next) {
        auto guard(lockExclusive());
        nextBlock = next;
    }

    size_t size() const {
        auto guard(lockShared());
        return blockUsed;
    }

    size_t byteSize() const {
        auto guard(lockShared());
        return blockSize;
    }

    std::pair<milliseconds, milliseconds> minMaxTime() const {
        auto guard(lockShared());
        return {minTime, maxTime};
    }

    const Row& at(size_t offset) const {
        auto guard(lockShared());
        return rows[offset];
    }

    const Row& front() const {
        auto guard(lockShared());
        return rows.front();
    }

    const Row& back() const {
        auto guard(lockShared());
        return rows[offset()];
    }

    std::shared_lock<Lock> lockShared() const {
        return move(std::shared_lock<Lock>(blockLock));
    }

    std::unique_lock<Lock> lockExclusive() const {
        return move(std::unique_lock<Lock>(blockLock));
    }

#if defined(SETAB_ROWBLOCK_DEBUG)
    ~RowBlockImpl() {
        std::cout << "Destroying RowBlock for ids="
                  << rows.front().rowId() << ":" << rows[offset()].rowId()
                  << " ts=" << minTime.count() << ":" << maxTime.count() << "\n";
    }
#else
    ~RowBlockImpl() = default;
#endif

private:
    size_t offset() const {
        return blockUsed == 0 ? 0 : blockUsed-1;
    }
    mutable Lock blockLock{};
    milliseconds minTime{0};
    milliseconds maxTime{0};
    size_t blockSize{0};
    size_t blockUsed{0};
    std::array<Row, BlockSz> rows{};
    std::shared_ptr<RowBlockImpl<BlockSz, Lock>> nextBlock;

    friend class RowCursorImpl<RowBlockImpl<BlockSz, Lock>>;
};

template<class RowBlockType>
class RowCursorImpl {
public:
    RowCursorImpl(std::shared_ptr<RowBlockType> block)
            : block_{block}, offset_{0} {
    }

    RowCursorImpl() = delete;
    RowCursorImpl(const RowCursorImpl<RowBlockType>&) = default;
    RowCursorImpl(RowCursorImpl<RowBlockType>&&) = default;

    RowCursorImpl<RowBlockType>& operator=(const RowCursorImpl<RowBlockType>&) = default;
    RowCursorImpl<RowBlockType>& operator=(RowCursorImpl<RowBlockType>&&) = default;

    ~RowCursorImpl() = default;

    const Row& get() const {
        return block_->at(offset_);
    }

    // Move the cursor forward one row. If it is unable, because there is
    // no more data left in the buffer that this cursor is iterating,
    // then next() returns false.
    bool next() {
        auto guard(block_->lockShared());

        // Uses internal data because otherwise locking doesn't work.
        if ((offset_+1) > block_->offset()) {
            if (block_->size() < block_->rows.max_size() || !block_->nextBlock) {
                return false;
            }
            offset_ = 0;
            {
                // The ordering of oldBlock and guard2 is important.
                // This is done so that if the ref-count of block_ is zero,
                // after we move it to nextBlock, it can be safely destructed.
                // If the ordering was different, then when we tried to destruct
                // block_, a shared lock would still be held, and
                // folly::SharedMutex correctly asserts in this situation.
                std::shared_ptr<RowBlockType> oldBlock = block_;
                auto guard2 = move(guard);
                block_ = block_->nextBlock;
            }
            return true;
        } else {
            offset_++;
            return true;
        }
    }

    // Moves this cursor as close to the requested minTime as possible.
    // If seek returns true, then the cursor is positioned at the row that
    // satisfies row.ts() > minTime, if false, you must check the cursor
    // to see where it landed.
    bool seek(milliseconds minTime) {
        auto minMax = block_->minMaxTime();
        while (minMax.first < minTime && minMax.second < minTime) {
            if (!block_->nextBlock) {
                return false;
            }
            block_ = block_->nextBlock;
            minMax = block_->minMaxTime();
        }
        while (get().valid() && get().ts() < minTime) {
            if (!next()) { return false; }
        }
        return true;
    }

private:
    std::shared_ptr<RowBlockType> block_;
    size_t offset_;

    friend RowBlockType;
};



template<class RowBlockCls, class RowCursorCls=RowCursorImpl<RowBlockCls>>
class RowBufferImpl {
public:
    explicit RowBufferImpl(size_t maxRows, size_t maxBytes, milliseconds maxAge)
        : maxRows_{maxRows},
          maxBytes_{maxBytes},
          maxAge_{maxAge},
          totalRows_{0},
          totalBytes_{0},
          totalBlocks_{1},
          headBlock_{RowBlockCls::create()},
          tailBlock_{headBlock_},
          blockWritesLock_{},
          writesBlockedCondition_{} {
    }

    RowBufferImpl(const RowBufferImpl<RowBlockCls, RowCursorCls>&) = delete;

    RowBufferImpl<RowBlockCls, RowCursorCls>&
        operator=(const RowBufferImpl<RowBlockCls, RowCursorCls>&) = delete;

    // This always succeeds, unless the process is out of memory.
    // It's recommended to make that not happen.
    bool appendRow(Row& row) {
        adviseGC();
        size_t bytes = row.size();
        bool appended = tailBlock_->appendRow(row);

        while(!appended) {
            auto nextBlock = RowBlockCls::create();
            tailBlock_->setNextBlock(nextBlock);
            tailBlock_ = nextBlock;
            appended = tailBlock_->appendRow(row);
            totalBlocks_.fetch_add(1);
        }

        totalRows_.fetch_add(1);
        totalBytes_.fetch_add(bytes);
        {
            std::unique_lock<std::mutex> guard(blockWritesLock_);
            rowSeq_++;
        }
        writesBlockedCondition_.notify_all();
        return true;
    }

    bool appendRow(const Row& row) {
        Row rowCopy(row);
        return appendRow(rowCopy);
    }

    // Frees some memory, if it makes sense to do so.
    // This can only free blocks of rows. If the rows in a block violate
    // any of maxAge, maxBytes, or maxRows, then the container won't respect
    // those constraints. A configuration with more granular blocks is
    // recommended in these cases.
    void adviseGC() {
        while (totalRows_ >= maxRows_ ||
               totalBytes_ > maxBytes_ ||
               headBlock_->minMaxTime().first < (tailBlock_->minMaxTime().second - maxAge_)) {
            auto nextBlock = headBlock_->next();
            if (nextBlock == tailBlock_) {
                break;
            }
            totalRows_.fetch_sub(headBlock_->size());
            totalBytes_.fetch_sub(headBlock_->byteSize());
            totalBlocks_.fetch_sub(1);
            headBlock_ = nextBlock;
        }
    }

    bool waitForWrite(milliseconds maxWait=0ms) {
        size_t curSeq = rowSeq_.load();
        auto checkSequence = [curSeq, this]() {
            std::cout << "rowSeq_:" << rowSeq_.load() << " curSeq:" << curSeq << "\n";
            return rowSeq_.load() != curSeq;
        };

        std::unique_lock<std::mutex> guard(blockWritesLock_);
        if (maxWait > 0ms) {
            return writesBlockedCondition_.wait_for(guard, maxWait, checkSequence);
        }
        writesBlockedCondition_.wait(guard, checkSequence);
        return true;
    }

    RowCursorCls getCursor() const {
        return RowCursorCls(headBlock_);
    }

    struct RowBufferStats {
        size_t totalRows;
        size_t totalBytes;
        size_t totalBlocks;
    };

    RowBufferStats stats() const {
        return RowBufferStats{totalRows_.load(), totalBytes_.load(), totalBlocks_.load()};
    };

    size_t maxRows() const { return maxRows_; }
    size_t maxBytes() const { return maxBytes_; }
    milliseconds maxAge() const { return maxAge_; }

private:

    const size_t maxRows_;
    const size_t maxBytes_;
    const milliseconds maxAge_;

    std::atomic_size_t rowSeq_;
    std::atomic_size_t totalRows_;
    std::atomic_size_t totalBytes_;
    std::atomic_size_t totalBlocks_;

    std::shared_ptr<RowBlockCls> headBlock_;
    std::shared_ptr<RowBlockCls> tailBlock_;

    std::mutex blockWritesLock_;
    std::condition_variable writesBlockedCondition_;
};

// The default RowBuffer types.
using RowBlock = RowBlockImpl<1000, folly::SharedMutex>;
using RowCursor = RowCursorImpl<RowBlock>;
using RowBuffer = RowBufferImpl<RowBlock, RowCursor>;
