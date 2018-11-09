#ifndef S_LOG_H
#define S_LOG_H

#include <stdarg.h>
#include <cerrno>
#include <cstring>
#include <string>
#include <sstream>
#include <stdexcept>
#include <string>
#include <log4cplus/loglevel.h>

namespace slog 
{

enum Level 
{
    SLOG_ALL   = log4cplus::ALL_LOG_LEVEL,
    SLOG_TRACE = log4cplus::TRACE_LOG_LEVEL,
    SLOG_DEBUG = log4cplus::DEBUG_LOG_LEVEL,
    SLOG_INFO  = log4cplus::INFO_LOG_LEVEL,
    SLOG_WARN  = log4cplus::WARN_LOG_LEVEL,
    SLOG_ERROR = log4cplus::ERROR_LOG_LEVEL,
    SLOG_FATAL = log4cplus::FATAL_LOG_LEVEL,
    SLOG_OFF   = log4cplus::OFF_LOG_LEVEL,
};

void sLogConfig(const std::string& file);

class logstream : public std::ostringstream 
{
public:
    logstream(const std::string& name, int level, 
        const char* file, int line);
    ~logstream();

    logstream& stream();

private:
    logstream(const logstream&);
    logstream& operator=(const logstream&);

private:
    std::string m_name;
    int m_level;
    const char* m_file;
    int m_line;
};

#define sLog(name, level) \
    slog::logstream(name, level, __FILE__, __LINE__).stream()

} // namespace slog

#endif

