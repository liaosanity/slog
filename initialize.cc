#include <log4cplus/helpers/loglog.h>
#include <log4cplus/hierarchylocker.h>
#include <log4cplus/hierarchy.h>
#include <log4cplus/spi/factory.h>
#include <log4cplus/consoleappender.h>
#include <log4cplus/nullappender.h>
#include <log4cplus/syslogappender.h>
#include <log4cplus/asyncappender.h>
#include <log4cplus/log4judpappender.h>
#include <log4cplus/helpers/fileinfo.h>

#include "file_appender.h"
#include "pattern_layout.h"
#include "logger_factory.h"

namespace log4cplus 
{
    void initialize();
}

namespace slog 
{

using namespace log4cplus;

void InitLogFactoryRegistry() 
{
    std::auto_ptr<spi::LoggerFactory> f(new slog::LoggerFactory());
    log4cplus::Logger::getRoot().getDefaultHierarchy().setLoggerFactory(f);

    spi::AppenderFactoryRegistry& reg = spi::getAppenderFactoryRegistry();
    reg.put(std::auto_ptr<log4cplus::spi::AppenderFactory>(
        new log4cplus::spi::FactoryTempl<log4cplus::ConsoleAppender, 
        log4cplus::spi::AppenderFactory>(LOG4CPLUS_TEXT("ConsoleAppender"))));

    reg.put(std::auto_ptr<log4cplus::spi::AppenderFactory>(
        new log4cplus::spi::FactoryTempl<log4cplus::NullAppender,
        log4cplus::spi::AppenderFactory>(LOG4CPLUS_TEXT("NullAppender"))));

    reg.put(std::auto_ptr<log4cplus::spi::AppenderFactory>(
        new log4cplus::spi::FactoryTempl<slog::FileAppender,
        log4cplus::spi::AppenderFactory>(LOG4CPLUS_TEXT("FileAppender"))));

    reg.put(std::auto_ptr<log4cplus::spi::AppenderFactory>(
        new log4cplus::spi::FactoryTempl<slog::RollingFileAppender, 
        log4cplus::spi::AppenderFactory>(LOG4CPLUS_TEXT("RollingFileAppender"))));

    reg.put(std::auto_ptr<log4cplus::spi::AppenderFactory>(
        new log4cplus::spi::FactoryTempl<slog::DailyRollingFileAppender,
        log4cplus::spi::AppenderFactory>(LOG4CPLUS_TEXT("DailyRollingFileAppender"))));

    spi::LayoutFactoryRegistry& reg2 = spi::getLayoutFactoryRegistry();
    reg2.put(std::auto_ptr<log4cplus::spi::LayoutFactory>(
        new log4cplus::spi::FactoryTempl<log4cplus::SimpleLayout,
        log4cplus::spi::LayoutFactory>(LOG4CPLUS_TEXT("SimpleLayout"))));

    reg2.put(std::auto_ptr<log4cplus::spi::LayoutFactory>(
        new log4cplus::spi::FactoryTempl<log4cplus::TTCCLayout,
        log4cplus::spi::LayoutFactory>(LOG4CPLUS_TEXT("TTCCLayout"))));

    reg2.put(std::auto_ptr<log4cplus::spi::LayoutFactory>(
        new log4cplus::spi::FactoryTempl<slog::PatternLayout,
        log4cplus::spi::LayoutFactory>(LOG4CPLUS_TEXT("PatternLayout"))));

    spi::FilterFactoryRegistry& reg3 = spi::getFilterFactoryRegistry();
    reg3.put(std::auto_ptr<log4cplus::spi::FilterFactory>(
        new log4cplus::spi::FactoryTempl<log4cplus::spi::DenyAllFilter,
        log4cplus::spi::FilterFactory>(LOG4CPLUS_TEXT("DenyAllFilter"))));

    reg3.put(std::auto_ptr<log4cplus::spi::FilterFactory>(
        new log4cplus::spi::FactoryTempl<log4cplus::spi::LogLevelMatchFilter,
        log4cplus::spi::FilterFactory>(LOG4CPLUS_TEXT("LogLevelMatchFilter"))));

    reg3.put(std::auto_ptr<log4cplus::spi::FilterFactory>(
        new log4cplus::spi::FactoryTempl<log4cplus::spi::LogLevelRangeFilter,
        log4cplus::spi::FilterFactory>(LOG4CPLUS_TEXT("LogLevelRangeFilter"))));

    reg3.put(std::auto_ptr<log4cplus::spi::FilterFactory>(
        new log4cplus::spi::FactoryTempl<log4cplus::spi::StringMatchFilter,
        log4cplus::spi::FilterFactory>(LOG4CPLUS_TEXT("StringMatchFilter"))));
}

void initializeLog() 
{
    log4cplus::initialize();
    InitLogFactoryRegistry();
}

} // namespace slog
