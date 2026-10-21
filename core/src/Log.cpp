#include "Log.h"

namespace allnes
{
Log& Log::get()
{
    static Log logger;
    return logger;
}

Log& Log::setLevel(Level level)
{
    m_level = level;
    return *this;
}

Level Log::getLevel()
{
    return m_level;
}

void Log::setLogStream(std::ostream& stream)
{
    m_output = &stream;
}

void Log::setCpuTraceStream(std::ostream& stream)
{
    m_cpuTrace = &stream;
}

std::ostream& Log::getStream()
{
    return *m_output;
}

std::ostream& Log::getCpuTraceStream()
{
    return *m_cpuTrace;
}

} // namespace allnes
