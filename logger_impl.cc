#include "logger_impl.h"

#include <algorithm>
#include <log4cplus/spi/loggingevent.h>
#include <log4cplus/helpers/loglog.h>

using namespace log4cplus;
using namespace log4cplus::helpers;

namespace slog 
{

void LoggerImpl::callAppenders(const log4cplus::spi::InternalLoggingEvent& event)
{
    int writes = 0;
    const LoggerImpl* c = this;

    for (; NULL != c; c = (LoggerImpl *)c->parent.get()) 
    {
        log4cplus::spi::LoggerImpl* p = (log4cplus::spi::LoggerImpl *)c;
        if ("root" == p->getName()) 
        {
            writes += p->appendLoopOnAppenders(event);
        }
        else 
        {
            writes += c->appendLoopOnAppenders(event);
        }

        if (!c->additive) 
        {
            break;
        }   
    } 
}

void LoggerImpl::addAppender(log4cplus::SharedAppenderPtr newAppender)
{
    if (NULL == newAppender) 
    {
        getLogLog().warn(LOG4CPLUS_TEXT("Try to add NULL appender"));

        return;
    }

    pthread_rwlock_wrlock(&rwlock_);

    ListType::iterator it = std::find(appenderList.begin(), 
        appenderList.end(), newAppender);
    if (it == appenderList.end()) 
    {
        appenderList.push_back(newAppender);
    }

    pthread_rwlock_unlock(&rwlock_);
}

AppenderAttachableImpl::ListType LoggerImpl::getAllAppenders()
{
    pthread_rwlock_rdlock(&rwlock_);

    AppenderAttachableImpl::ListType l = appenderList;

    pthread_rwlock_unlock(&rwlock_);

    return l;
}

log4cplus::SharedAppenderPtr LoggerImpl::getAppender(
    const log4cplus::tstring& name)
{
    pthread_rwlock_rdlock(&rwlock_);

    log4cplus::SharedAppenderPtr p(NULL);

    ListType::iterator it = appenderList.begin();
    for (; it != appenderList.end(); ++it)
    {
        if ((*it)->getName() == name) 
        {
            p = *it;
        }
    }

    pthread_rwlock_unlock(&rwlock_);

    return p;
}

void LoggerImpl::removeAllAppenders()
{
    pthread_rwlock_wrlock(&rwlock_);

    appenderList.erase(appenderList.begin(), appenderList.end());

    pthread_rwlock_unlock(&rwlock_);
}

void LoggerImpl::removeAppender(log4cplus::SharedAppenderPtr appender)
{
    if (NULL == appender) 
    {
        getLogLog().warn(LOG4CPLUS_TEXT("Try to remove NULL appender"));

        return;
    }

    pthread_rwlock_wrlock(&rwlock_);

    ListType::iterator it = std::find(appenderList.begin(), 
        appenderList.end(), appender);
    if (it != appenderList.end()) 
    {
        appenderList.erase(it);
    }

    pthread_rwlock_unlock(&rwlock_);
}

int LoggerImpl::appendLoopOnAppenders(
    const log4cplus::spi::InternalLoggingEvent& event) const
{
    int count = 0;

    pthread_rwlock_rdlock(const_cast<pthread_rwlock_t *>(&rwlock_));

    ListType::const_iterator it = appenderList.begin();
    for (; it != appenderList.end(); ++it)
    {
        ++count;
        (*it)->doAppend(event);
    }

    pthread_rwlock_unlock(const_cast<pthread_rwlock_t *>(&rwlock_));

    return count;
}

} // namespace slog
