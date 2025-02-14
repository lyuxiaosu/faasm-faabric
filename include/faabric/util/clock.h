#pragma once

#include <chrono>

namespace faabric::util {
typedef std::chrono::steady_clock::time_point TimePoint;

class Clock
{
  public:
    Clock();

    const TimePoint now();

    const long epochMillis();

    const long epochMicros();

    const long timeDiff(const TimePoint& t1, const TimePoint& t2);

    const long timeDiffNano(const TimePoint& t1, const TimePoint& t2);

    const long timeDiffMicro(const TimePoint& t1, const TimePoint& t2);
};

Clock& getGlobalClock();
}
