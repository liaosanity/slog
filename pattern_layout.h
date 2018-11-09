#ifndef PATTERN_LAYOUT_H
#define PATTERN_LAYOUT_H

#include <log4cplus/layout.h>

namespace slog 
{

namespace pattern 
{
    class PatternConverter;
}

class PatternLayout : public log4cplus::Layout 
{
public:
    PatternLayout(const log4cplus::tstring& pattern);
    PatternLayout(const log4cplus::helpers::Properties& properties);
    virtual ~PatternLayout();

    virtual void formatAndAppend(log4cplus::tostream& output, 
        const log4cplus::spi::InternalLoggingEvent& event);

protected:
    void init(const log4cplus::tstring& pattern, unsigned ndcMaxDepth = 0);

    log4cplus::tstring pattern;
    std::vector<pattern::PatternConverter*> parsedPattern;

private: 
    PatternLayout(const PatternLayout&);
    PatternLayout& operator=(const PatternLayout&);
};

} // namespace slog

#endif
