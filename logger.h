#ifndef LOGGER_H
#define LOGGER_H

#include <log4cplus/logger.h>

#include "logger_impl.h"

namespace slog 
{

class Logger : public log4cplus::Logger 
{
public:
    Logger(slog::LoggerImpl *ptr) 
    {
        value = ptr;
        if (value) 
        {
            value->addReference();
        }
    }
};

} // namespace slog

#endif

