#ifndef LOGGER_FACTORY_H
#define LOGGER_FACTORY_H

#include <log4cplus/spi/loggerfactory.h>
#include <log4cplus/hierarchy.h>

#include "logger.h"
#include "logger_impl.h"

namespace slog 
{

class LoggerFactory : public log4cplus::spi::LoggerFactory 
{
public:
    log4cplus::Logger makeNewLoggerInstance(const log4cplus::tstring& name, 
        log4cplus::Hierarchy& h) 
    {
        return static_cast<log4cplus::Logger>(slog::Logger(
            new slog::LoggerImpl(name, h)));
    }
}; 

} // namespace slog

#endif
