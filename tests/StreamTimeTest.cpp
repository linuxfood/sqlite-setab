
#include "setab/StreamTime.h"

#include <gtest/gtest.h>

using namespace std::chrono_literals;

TEST(StreamTime, OneSample) {
    auto now = nowMs();
    StreamTime<milliseconds> t(360000ms, now);
    t.addObservation(now);
    EXPECT_EQ(now, t.streamNow()) << "single observation produces same result";
}

TEST(StreamTime, SomeSamples) {
    auto now = nowMs();
    StreamTime<milliseconds> t(360000ms, now);
    for (milliseconds i=0ms; i<100ms; i++) {
        t.addObservation(now+i);
    }
}
