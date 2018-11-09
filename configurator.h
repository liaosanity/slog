#ifndef CONFIGURATOR_H
#define CONFIGURATOR_H

#include <log4cplus/configurator.h>

using namespace log4cplus;

namespace slog 
{

class PropertyConfigurator : public log4cplus::PropertyConfigurator 
{
public:
    PropertyConfigurator(const log4cplus::tstring& propertyFile,
        Hierarchy& h = Logger::getDefaultHierarchy(), unsigned flags = 0);
    PropertyConfigurator(const log4cplus::helpers::Properties& props,
        Hierarchy& h = Logger::getDefaultHierarchy(), unsigned flags = 0);
    PropertyConfigurator(log4cplus::tistream& propertyStream,
        Hierarchy& h = Logger::getDefaultHierarchy(), unsigned flags = 0);

    ~PropertyConfigurator() {}

    virtual void configure();

protected:
    void init();
    void reconfigure();

private:
    log4cplus::helpers::Properties origin_properties;
};

class ConfigurationWatchDogThread;
class ConfigureAndWatchThread 
{
public:
    ConfigureAndWatchThread(const log4cplus::tstring& propertyFile,
        unsigned int millis = 60 * 1000);
    virtual ~ConfigureAndWatchThread();

private:
    ConfigureAndWatchThread(const ConfigureAndWatchThread&);
    ConfigureAndWatchThread& operator=(const ConfigureAndWatchThread&);

    ConfigurationWatchDogThread *watchDogThread;
};

} // namespace slog

#endif
