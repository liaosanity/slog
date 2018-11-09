#ifndef LOGGER_IMPL_H
#define LOGGER_IMPL_H

#include <log4cplus/helpers/pointer.h>
#include <log4cplus/spi/loggerimpl.h>
#include <pthread.h>

namespace slog 
{

class LoggerImpl : public log4cplus::spi::LoggerImpl 
{
public:
    LoggerImpl(const log4cplus::tstring& name, log4cplus::Hierarchy& h) 
        : log4cplus::spi::LoggerImpl(name, h) 
    {
        pthread_rwlock_init(&rwlock_, NULL);
    }

    virtual ~LoggerImpl() 
    { 
        pthread_rwlock_destroy(&rwlock_); 
    }

    virtual void callAppenders(const log4cplus::spi::InternalLoggingEvent& event);
    virtual void addAppender(log4cplus::SharedAppenderPtr newAppender);
    virtual AppenderAttachableImpl::ListType getAllAppenders();
    virtual log4cplus::SharedAppenderPtr getAppender(const log4cplus::tstring& name);
    virtual void removeAllAppenders();
    virtual void removeAppender(log4cplus::SharedAppenderPtr appender);
    int appendLoopOnAppenders(const log4cplus::spi::InternalLoggingEvent& event) const;

private:
    pthread_rwlock_t rwlock_;
};

} // namespace slog

#endif

