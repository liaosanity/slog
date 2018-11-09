#include <iostream>
#include <fstream>
#include <cstdio>
#include <cstdarg>
#include <memory>
#include <vector>
#include <log4cplus/logger.h>
#include <log4cplus/consoleappender.h>
#include <log4cplus/helpers/loglog.h>
#include <log4cplus/fstreams.h>
#include <log4cplus/socketappender.h>
#include <log4cplus/hierarchy.h>
#include <log4cplus/ndc.h>

#include "slog.h"
#include "configurator.h"

using namespace std;
using namespace log4cplus;
using namespace log4cplus::spi;
using namespace log4cplus::helpers;

namespace slog 
{

typedef std::pair<std::string, std::string> KeyValue;
typedef std::vector<KeyValue> ConfigList;

void initializeLog();
static void sLogConfig(ConfigList& clist);
static bool isAppenderInit = false;

static const tstring LevelString(LogLevel ll)
{
    return getLogLevelManager().toString(ll);
}

static bool reConfig(const Properties& props)
{
    Properties checks = props.getPropertySubset("slog.appender.");
    if (0 == checks.size()) 
    {
        LogLog::getLogLog()->error("Invalid format");

        return false;
    }

    log4cplus::Logger::getRoot().getDefaultHierarchy().resetConfiguration();
    slog::PropertyConfigurator pc(props);
    pc.configure();
    isAppenderInit = true;

    return true;
}

static void defaultConsole(Level level) 
{
    ConfigList defaultSet;
    std::string ll = LevelString(level);

    if (level < SLOG_WARN) 
    {
        ll += ", ROOT_INFO";

        defaultSet.push_back(KeyValue("slog.appender.ROOT_INFO", 
            "ConsoleAppender"));
        defaultSet.push_back(KeyValue("slog.appender.ROOT_INFO.ImmediateFlush", 
            "true"));
        defaultSet.push_back(KeyValue("slog.appender.ROOT_INFO.layout", 
            "PatternLayout"));
        defaultSet.push_back(KeyValue("slog.appender.ROOT_INFO.layout.ConversionPattern",
            "%D [%p] [%c] %m%n"));
        defaultSet.push_back(KeyValue("slog.appender.ROOT_INFO.filters.1",
            "LogLevelRangeFilter"));
        defaultSet.push_back(KeyValue("slog.appender.ROOT_INFO.filters.1.LogLevelMin", 
            "ALL"));
        defaultSet.push_back(KeyValue("slog.appender.ROOT_INFO.filters.1.LogLevelMax", 
            "WARN"));
        defaultSet.push_back(KeyValue("slog.appender.ROOT_INFO.filters.1.AcceptOnMatch", 
            "true"));
    }

    if (level < SLOG_OFF) 
    {
        ll += ", ROOT_ERROR";

        defaultSet.push_back(KeyValue("slog.appender.ROOT_ERROR", 
            "ConsoleAppender"));
        defaultSet.push_back(KeyValue("slog.appender.ROOT_ERROR.ImmediateFlush", 
            "true"));
        defaultSet.push_back(KeyValue("slog.appender.ROOT_ERROR.logToStdErr", 
            "true"));
        defaultSet.push_back(KeyValue("slog.appender.ROOT_ERROR.layout", 
            "PatternLayout"));
        defaultSet.push_back(KeyValue("slog.appender.ROOT_ERROR.layout.ConversionPattern",
            "%D [%p] [%c] [%l] %m%n"));
        defaultSet.push_back(KeyValue("slog.appender.ROOT_ERROR.Threshold", 
            "WARN"));
    }

    if (level >= SLOG_OFF)
    {
        ll += ", ROOT_NULL";

        defaultSet.push_back(KeyValue("slog.appender.ROOT_NULL", "NullAppender"));
    }

    defaultSet.push_back(KeyValue("slog.rootLogger", ll));
    sLogConfig(defaultSet);
}

static void init(void)
{
    if (isAppenderInit)
    {
        return;
    }

    slog::initializeLog();
    defaultConsole(SLOG_INFO);
}

void sLogConfig(const std::string& file)
{
    if (0 == file.length()) 
    {
        LogLog::getLogLog()->error("configured file doesn't exist.");

        return;
    }

    tifstream tfile;
    tfile.open(LOG4CPLUS_TSTRING_TO_STRING(file).c_str());
    if (!tfile) 
    {
        LogLog::getLogLog()->error("Unable to open file: " + file);

        return;
    }

    Properties props(tfile);
    reConfig(props);
}

static void sLogConfig(ConfigList& clist)
{
    Properties props;
    for (ConfigList::iterator it = clist.begin(); it != clist.end(); ++it)
    {
        props.setProperty(it->first, it->second);
    }

    reConfig(props);
}

static bool needLog(const std::string& name, int level)
{
    init();

    log4cplus::Logger logger = (name.empty()) ?
        log4cplus::Logger::getRoot() : log4cplus::Logger::getInstance(name);

    return logger.isEnabledFor(level);
}

static void LogAllStream(const std::string& name, int level, 
    const std::string& message, const char* file, int line)
{
    log4cplus::Logger logger = (name.empty()) ?
        log4cplus::Logger::getRoot() : log4cplus::Logger::getInstance(name);
    logger.forcedLog(level, message, file, line);
}

logstream::logstream(const std::string& name, int level, 
    const char* file, int line)
    : m_name(name)
    , m_level(level)
    , m_file(file)
    , m_line(line) 
{
}

logstream::~logstream()
{
    if (needLog(m_name, m_level)) 
    {
        logstream& ls = *this;
        LogAllStream(m_name, m_level, ls.str(), m_file, m_line);
    }
}

logstream& logstream::stream() 
{
    return *this; 
}

} // namespace slog

