#pragma once

#include <chrono>
#include <iostream>
#include <ostream>

namespace allnes
{
// Log levels are ordered: a message is emitted when its level is at or below
// the configured threshold.
enum Level
{
    None = 0,
    Error,
    Info,
    InfoVerbose,
    ApuTrace,
    CpuTrace,
};

class Log
{
public:
    static Log& get();

    Log&          setLevel(Level level);
    Level         getLevel();

    void          setLogStream(std::ostream& stream);
    void          setCpuTraceStream(std::ostream& stream);

    std::ostream& getStream();
    std::ostream& getCpuTraceStream();

private:
    Log() = default;

    Level         m_level      = Error;
    std::ostream* m_output     = &std::cerr;
    std::ostream* m_cpuTrace   = &std::cerr;
};

inline std::ostream& log_timestamp(std::ostream& out)
{
    using namespace std::chrono;
    const auto now = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
    return out << '[' << now << ']';
}

// A for-loop keeps the macro a single statement, so it can be used after an
// if/else without dangling-else surprises.
#define LOG(level)                                                                                                     \
    for (bool allnes_log_once = ((level) <= allnes::Log::get().getLevel()); allnes_log_once; allnes_log_once = false)  \
        allnes::Log::get().getStream() << allnes::log_timestamp << '[' << __FILE__ << ':' << __LINE__ << "] "

#define LOG_CPU                                                                                                        \
    for (bool allnes_log_once = (allnes::CpuTrace == allnes::Log::get().getLevel()); allnes_log_once;                  \
         allnes_log_once      = false)                                                                                 \
        allnes::Log::get().getCpuTraceStream()

#define VAR_PRINT(x) " \033[0;31m" << #x << "\033[0m=" << x

} // namespace allnes
