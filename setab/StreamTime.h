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

#include <folly/stats/TimeseriesHistogram.h>
#include <folly/stats/TimeseriesHistogram-defs.h>

template<class Time=milliseconds>
class StreamTime {
    using ClockType = folly::LegacyStatsClock<Time>;
    using TimeSeries = folly::TimeseriesHistogram<int64_t, ClockType>;

    const Time maxDelta_;
    const double pct_;

    const Time intervals_[2]  {
        Time(60 * Time::period::den),
        Time(600 * Time::period::den)
        //Time(60 * 1000),
        //Time(600 * 1000),
    };

    Time referenceNow_;
    TimeSeries history_;

    Time sysNow() const {
        return duration_cast<Time>(system_clock::now().time_since_epoch());
    }

public:

    enum TimeWindow {
        FastView,
        SlowView
    };

    explicit StreamTime(Time maxDelta, Time referenceNow, double pct=95.0) 
        : maxDelta_{maxDelta},
          pct_{pct},
          referenceNow_{referenceNow},
          history_{(maxDelta_.count() * 2) / 100,
                   -maxDelta.count(),
                   maxDelta.count(),
                   folly::MultiLevelTimeSeries<int64_t, ClockType>(100, 2, intervals_)} {}

    explicit StreamTime(Time maxDelta) : StreamTime(maxDelta, sysNow()) {}

    Time streamNow(TimeWindow tw=FastView) const {
        return referenceNow_ + Time(history_.getPercentileEstimate(pct_, tw));
    }
    Time currentDelta(TimeWindow tw=FastView) const {
        return Time(history_.getPercentileEstimate(pct_, tw));
    }

    void addObservation(Time ts) {
        Time t = sysNow();
        referenceNow_ = t;
        history_.addValue(t, (ts - t).count());
        history_.update(t);
    }
};
