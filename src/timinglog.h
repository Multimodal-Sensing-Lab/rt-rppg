#ifndef TIMINGLOG_H
#define TIMINGLOG_H

/*
 * Optional timing log, for the evaluation of the complete system (tools/live_eval.py).
 *
 * With RT_RPPG_TIMING=<folder>, the camera thread writes camera.csv (one row per captured frame) and the processing
 * loop writes frames.csv (one row per processed frame) in that folder. The times are microseconds of the monotonic
 * clock (CLOCK_MONOTONIC, also used for the V4L2 buffer timestamps), so they can be compared with each other and
 * with the times recorded by other programs on the same computer. Without the variable, nothing is written.
 */

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>
#ifdef __linux__
#include <pthread.h>
#endif

namespace timinglog {

// Microseconds of the monotonic clock
inline long long nowUs()
{
    using namespace std::chrono;
    return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}

// Opens <RT_RPPG_TIMING>/<name> and writes the header line. Returns nullptr if the log is disabled or cannot be written.
inline std::FILE *open(const char *name, const char *header)
{
    const char *folder = std::getenv("RT_RPPG_TIMING");
    if (folder == nullptr || *folder == '\0')
        return nullptr;
    ::mkdir(folder, 0755);
    std::string path = std::string(folder) + "/" + name;
    std::FILE *f = std::fopen(path.c_str(), "w");
    if (f == nullptr) {
        std::fprintf(stderr, "timing log: cannot write %s\n", path.c_str());
        return nullptr;
    }
    std::setvbuf(f, nullptr, _IOFBF, 1 << 16);
    std::fprintf(f, "%s\n", header);
    return f;
}

// One decimal with a decimal point whatever the locale (QApplication sets the system locale, see network.cpp)
inline std::string decimal(double value)
{
    if (!std::isfinite(value))
        return "nan";
    long long tenths = std::llround(value * 10);
    return std::string(tenths < 0 ? "-" : "") + std::to_string(std::llabs(tenths) / 10) + "." +
           std::to_string(std::llabs(tenths) % 10);
}

// Names the calling thread (at most 15 characters), so that the CPU time of each thread can be read from /proc
inline void nameThread(const char *name)
{
#ifdef __linux__
    pthread_setname_np(pthread_self(), name);
#else
    (void)name;
#endif
}

} // namespace timinglog

#endif // TIMINGLOG_H
