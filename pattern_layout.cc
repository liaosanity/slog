#include <log4cplus/helpers/loglog.h>
#include <log4cplus/helpers/timehelper.h>
#include <log4cplus/helpers/stringhelper.h>
#include <log4cplus/helpers/socket.h>
#include <log4cplus/helpers/property.h>
#include <log4cplus/spi/loggingevent.h>
#include <log4cplus/internal/internal.h>
#include <log4cplus/internal/env.h>
#include <cstdlib>
#include <iomanip>

#include "pattern_layout.h"

using namespace log4cplus;
using namespace log4cplus::helpers;

namespace
{

static log4cplus::tstring get_basename(const log4cplus::tstring& filename)
{
    log4cplus::tchar const dir_sep(LOG4CPLUS_TEXT('/'));
    log4cplus::tstring::size_type pos = filename.rfind(dir_sep);
    if (pos != log4cplus::tstring::npos)
    {
        return filename.substr(pos + 1);
    }
    else
    {
        return filename;
    }
}

} // namespace

namespace slog
{

static tchar const ESCAPE_CHAR = LOG4CPLUS_TEXT('%');

void formatRelativeTimestamp(log4cplus::tostream &output,
    log4cplus::spi::InternalLoggingEvent const &event)
{
    helpers::Time const rel_time = event.getTimestamp() - getTTCCLayoutTimeBase();
    tchar const old_fill = output.fill();
    helpers::time_t const sec = rel_time.sec();
 
    if (sec != 0)
    {
        output << sec << std::setfill(LOG4CPLUS_TEXT('0')) << std::setw(3);
    }
 
    output << rel_time.usec() / 1000;
    output.fill(old_fill);
}

namespace pattern 
{

struct FormattingInfo 
{
    int minLen;
    std::size_t maxLen;
    bool leftAlign;
    FormattingInfo() { reset(); }

    void reset();
    void dump(helpers::LogLog&);
};

class PatternConverter
{
public:
    explicit PatternConverter(const FormattingInfo& info);
    virtual ~PatternConverter() {}
    void formatAndAppend(tostream& output, 
        const spi::InternalLoggingEvent& event);

    virtual void convert(tstring & result,
        const spi::InternalLoggingEvent& event) = 0;

private:
    int minLen;
    std::size_t maxLen;
    bool leftAlign;
};

typedef std::vector<pattern::PatternConverter*> PatternConverterList;

class LiteralPatternConverter : public PatternConverter
{
public:
    LiteralPatternConverter(const tstring& str);
    virtual void convert(tstring & result,
        const spi::InternalLoggingEvent&)
    {
        result = str;
    }

private:
    tstring str;
};

class BasicPatternConverter : public PatternConverter
{
public:
    enum Type 
    {
        THREAD_CONVERTER,
        THREAD2_CONVERTER,
        PROCESS_CONVERTER,
        LOGLEVEL_CONVERTER,
        NDC_CONVERTER,
        MESSAGE_CONVERTER,
        NEWLINE_CONVERTER,
        BASENAME_CONVERTER,
        FILE_CONVERTER,
        LINE_CONVERTER,
        FULL_LOCATION_CONVERTER,
        FUNCTION_CONVERTER 
    };

    BasicPatternConverter(const FormattingInfo& info, Type type);
    virtual void convert(tstring& result, const spi::InternalLoggingEvent& event);

private:
    BasicPatternConverter(const BasicPatternConverter&);
    BasicPatternConverter& operator=(BasicPatternConverter&);
    
    LogLevelManager& llmCache;
    Type type;
};

class LoggerPatternConverter : public PatternConverter 
{
public:
    LoggerPatternConverter(const FormattingInfo& info, int precision);
    virtual void convert(tstring& result, const spi::InternalLoggingEvent& event);

private:
    int precision;
};

class DatePatternConverter : public PatternConverter 
{
public:
    DatePatternConverter(const FormattingInfo& info, 
        const tstring& pattern, bool use_gmtime);
    virtual void convert(tstring& result, const spi::InternalLoggingEvent& event);

private:
    bool use_gmtime;
    tstring format;
};

class EnvPatternConverter : public PatternConverter 
{
public:
    EnvPatternConverter(const FormattingInfo& info, const log4cplus::tstring& env);
    virtual void convert(tstring& result, const spi::InternalLoggingEvent& event);

private:
    log4cplus::tstring envKey;
};

class RelativeTimestampConverter: public PatternConverter 
{
public:
    RelativeTimestampConverter(const FormattingInfo& info);
    virtual void convert(tstring &result, const spi::InternalLoggingEvent& event);
};

class HostnamePatternConverter : public PatternConverter 
{
public:
    HostnamePatternConverter(const FormattingInfo& info, bool hn);
    virtual void convert(tstring &result, const spi::InternalLoggingEvent& event);

private:
    tstring hostname_;
};

class MDCPatternConverter : public PatternConverter
{
public:
    MDCPatternConverter(const FormattingInfo& info, tstring const &k);
    virtual void convert(tstring &result, const spi::InternalLoggingEvent& event);

private:
    tstring key;
};

class NDCPatternConverter : public PatternConverter 
{
public:
    NDCPatternConverter(const FormattingInfo& info, int precision);
    virtual void convert(tstring &result, const spi::InternalLoggingEvent& event);

private:
    int precision;
};

class PatternParser
{
public:
    PatternParser(const tstring& pattern, unsigned ndcMaxDepth);
    std::vector<PatternConverter*> parse();

private:
    enum ParserState 
    {
        LITERAL_STATE, 
        CONVERTER_STATE,
        DOT_STATE,
        MIN_STATE,
        MAX_STATE
    };

    tstring extractOption();
    int extractPrecisionOption();
    void finalizeConverter(tchar c);

    tstring pattern;
    FormattingInfo formattingInfo;
    std::vector<PatternConverter*> list;
    ParserState state;
    tstring::size_type pos;
    tstring currentLiteral;
    unsigned ndcMaxDepth;
};

void FormattingInfo::reset() 
{
    minLen = -1;
    maxLen = 0x7FFFFFFF;
    leftAlign = false;
}

void FormattingInfo::dump(helpers::LogLog& loglog) 
{
    tostringstream buf;
    buf << LOG4CPLUS_TEXT("min=") << minLen << LOG4CPLUS_TEXT(", max=") << maxLen
        << LOG4CPLUS_TEXT(", leftAlign=") << std::boolalpha << leftAlign;
    loglog.debug(buf.str());
}

PatternConverter::PatternConverter(const FormattingInfo& i)
{
    minLen = i.minLen;
    maxLen = i.maxLen;
    leftAlign = i.leftAlign;
}

void PatternConverter::formatAndAppend(tostream& output, 
    const spi::InternalLoggingEvent& event)
{
    tstring s;
    convert(s, event);
    std::size_t len = s.length();

    if (len > maxLen)
    {
        output << s.substr(len - maxLen);
    }
    else if (static_cast<int>(len) < minLen)
    {
        std::ios_base::fmtflags const original_flags = output.flags();
        tchar const fill = output.fill(LOG4CPLUS_TEXT(' '));
        output.setf(leftAlign ? std::ios_base::left : std::ios_base::right,
            std::ios_base::adjustfield);
        output.width(minLen);
        output << s;
        output.fill(fill);
        output.flags(original_flags);
    }
    else
    {
        output << s;
    }
}

LiteralPatternConverter::LiteralPatternConverter(const tstring& str_)
    : PatternConverter(FormattingInfo())
    , str(str_)
{
}

BasicPatternConverter::BasicPatternConverter(const FormattingInfo& info, Type type_)
    : PatternConverter(info)
    , llmCache(getLogLevelManager())
    , type(type_)
{
}

void BasicPatternConverter::convert(tstring &result,
    const spi::InternalLoggingEvent& event)
{
    switch (type)
    {
    case LOGLEVEL_CONVERTER:
        result = llmCache.toString(event.getLogLevel());
        return;

    case BASENAME_CONVERTER:
        result = get_basename(event.getFile());
        return;

    case PROCESS_CONVERTER:
        helpers::convertIntegerToString(result, internal::get_process_id()); 
        return;

    case NDC_CONVERTER:
        result = event.getNDC();
        return;

    case MESSAGE_CONVERTER:
        result = event.getMessage();
        return;

    case NEWLINE_CONVERTER:
        result = LOG4CPLUS_TEXT("\n");
        return; 

    case FILE_CONVERTER:
        result = event.getFile();
        return;

    case THREAD_CONVERTER:
        result = event.getThread();
        return;

    case THREAD2_CONVERTER:
        result = event.getThread2();
        return;

    case LINE_CONVERTER:
        {
            if (event.getLine() != -1)
            {
                helpers::convertIntegerToString(result, event.getLine());
            }
            else
            {
                result.clear();
            }

            return;
        }

    case FULL_LOCATION_CONVERTER:
        {
            tstring const & file = event.getFile();
            if (!file.empty())
            {
                result = file;
                result += LOG4CPLUS_TEXT(":");
                result += helpers::convertIntegerToString(event.getLine());
            }
            else
            {
                result = LOG4CPLUS_TEXT(":");
            }

            return;
        }
        
    case FUNCTION_CONVERTER:
        result = event.getFunction();
        return;
    }

    result = LOG4CPLUS_TEXT("INTERNAL LOG4CPLUS ERROR");
}

LoggerPatternConverter::LoggerPatternConverter(const FormattingInfo& info, int prec)
    : PatternConverter(info)
    , precision(prec)
{
}

void LoggerPatternConverter::convert(tstring &result,
    const spi::InternalLoggingEvent& event)
{
    const tstring& name = event.getLoggerName();
    if (precision <= 0) 
    {
        result = name;
    }
    else 
    {
        std::size_t len = name.length();
        tstring::size_type end = len - 1;
        for (int i = precision; i > 0; --i)
        {
            end = name.rfind(LOG4CPLUS_TEXT('.'), end - 1);
            if (end == tstring::npos) 
            {
                result = name;

                return;
            }
        }

        result = name.substr(end + 1);
    }
}

DatePatternConverter::DatePatternConverter(const FormattingInfo& info, 
    const tstring& pattern, bool use_gmtime_)
    : PatternConverter(info)
    , use_gmtime(use_gmtime_)
    , format(pattern)
{
}

void DatePatternConverter::convert(tstring &result,
    const spi::InternalLoggingEvent& event)
{
    result = event.getTimestamp().getFormattedTime(format, use_gmtime);
}

EnvPatternConverter::EnvPatternConverter(const FormattingInfo& info, 
    const tstring& env)
    : PatternConverter(info)
    , envKey(env)
{
}

void EnvPatternConverter::convert(tstring &result,
    const spi::InternalLoggingEvent&)
{
    char const *val = std::getenv(envKey.c_str());
    if (val) 
    {
        result = val;
    } 
    else 
    {
        result.clear();
    }
}

RelativeTimestampConverter::RelativeTimestampConverter(FormattingInfo const & info)
    : PatternConverter(info)
{
}

void RelativeTimestampConverter::convert(tstring &result,
    spi::InternalLoggingEvent const &event)
{
    tostringstream oss;
    detail::clear_tostringstream(oss);
    formatRelativeTimestamp(oss, event);
    oss.str().swap(result);
}

HostnamePatternConverter::HostnamePatternConverter(const FormattingInfo& info, bool hn)
    : PatternConverter(info)
    , hostname_(helpers::getHostname(hn))
{
}

void HostnamePatternConverter::convert(tstring &result, 
    const spi::InternalLoggingEvent&)
{
    result = hostname_;
}

pattern::MDCPatternConverter::MDCPatternConverter(const FormattingInfo& info, tstring const &k)
    : PatternConverter(info)
    , key(k)
{
}

void pattern::MDCPatternConverter::convert(tstring &result,
    const spi::InternalLoggingEvent& event)
{
    if (!key.empty())
    {
        result = event.getMDC(key);
    }
    else
    {
        result.clear();

        MappedDiagnosticContextMap const &mdcMap = event.getMDCCopy();
        for (MappedDiagnosticContextMap::const_iterator it = mdcMap.begin(); 
             it != mdcMap.end(); ++it)
        {           
            tstring const &name(it->first);
            tstring const &value(it->second);

            result += LOG4CPLUS_TEXT("{");
            result += name;
            result += LOG4CPLUS_TEXT(", ");
            result += value;
            result += LOG4CPLUS_TEXT("}"); 
        }
    }
}

pattern::NDCPatternConverter::NDCPatternConverter(const FormattingInfo& info, int precision_)
    : PatternConverter(info)
    , precision(precision_)
{
}

void pattern::NDCPatternConverter::convert(tstring &result,
    const spi::InternalLoggingEvent& event)
{
    const log4cplus::tstring& text = event.getNDC();
    if (precision <= 0)
    {
        result = text;
    }
    else
    {
        tstring::size_type p = text.find(LOG4CPLUS_TEXT(' '));
        for (int i = 1; i < precision && p != tstring::npos; ++i)
        {
            p = text.find(LOG4CPLUS_TEXT(' '), p + 1);
        }

        result = text.substr(0, p);
    }
}

PatternParser::PatternParser(const tstring& pattern_, unsigned ndcMaxDepth_)
    : pattern(pattern_)
    , state(LITERAL_STATE)
    , pos(0)
    , ndcMaxDepth(ndcMaxDepth_)
{
}

tstring PatternParser::extractOption() 
{
    if ((pos < pattern.length()) && (pattern[pos] == LOG4CPLUS_TEXT('{'))) 
    {
        tstring::size_type end = pattern.find_first_of(LOG4CPLUS_TEXT('}'), pos);
        if (end != tstring::npos) 
        {
            tstring r = pattern.substr(pos + 1, end - pos - 1);
            pos = end + 1;

            return r;
        }
        else 
        {
            log4cplus::tostringstream buf;
            buf << LOG4CPLUS_TEXT("No matching '}' found in conversion pattern string \"")
                << pattern << LOG4CPLUS_TEXT("\"");
            helpers::getLogLog().error(buf.str());
            pos = pattern.length();
        }
    }

    return LOG4CPLUS_TEXT("");
}

int PatternParser::extractPrecisionOption() 
{
    tstring opt = extractOption();
    int r = 0;
    if (!opt.empty())
    {
        r = std::atoi(LOG4CPLUS_TSTRING_TO_STRING(opt).c_str());
    }

    return r;
}

PatternConverterList PatternParser::parse() 
{
    tchar c;
    pos = 0;
    while (pos < pattern.length()) 
    {
        c = pattern[pos++];
        switch (state) 
        {
        case LITERAL_STATE:
            if (pos == pattern.length()) 
            {
                currentLiteral += c;

                continue;
            }

            if (c == ESCAPE_CHAR) 
            {
                switch (pattern[pos]) 
                {
                case ESCAPE_CHAR:
                    currentLiteral += c;
                    pos++;
                    break;

                default:
                    if (!currentLiteral.empty()) 
                    {
                        list.push_back(new LiteralPatternConverter(currentLiteral));
                    }
                    currentLiteral.resize(0);
                    currentLiteral += c;
                    state = CONVERTER_STATE;
                    formattingInfo.reset();
                }
            }
            else 
            {
                currentLiteral += c;
            }
            break;

        case CONVERTER_STATE:
            currentLiteral += c;
            switch (c) 
            {
            case LOG4CPLUS_TEXT('-'):
                formattingInfo.leftAlign = true;
                break;
            case LOG4CPLUS_TEXT('.'):
                state = DOT_STATE;
                break;
            default:
                if (c >= LOG4CPLUS_TEXT('0') && c <= LOG4CPLUS_TEXT('9')) 
                {
                    formattingInfo.minLen = c - LOG4CPLUS_TEXT('0');
                    state = MIN_STATE;
                }
                else 
                {
                    finalizeConverter(c);
                }
            }
            break;

        case MIN_STATE:
            currentLiteral += c;
            if (c >= LOG4CPLUS_TEXT('0') && c <= LOG4CPLUS_TEXT('9')) 
            {
                formattingInfo.minLen = formattingInfo.minLen * 10 + (c - LOG4CPLUS_TEXT('0'));
            }
            else if (c == LOG4CPLUS_TEXT('.')) 
            {
                state = DOT_STATE;
            }
            else 
            {
                finalizeConverter(c);
            }
            break;

        case DOT_STATE:
            currentLiteral += c;
            if (c >= LOG4CPLUS_TEXT('0') && c <= LOG4CPLUS_TEXT('9')) 
            {
                formattingInfo.maxLen = c - LOG4CPLUS_TEXT('0');
                state = MAX_STATE;
            }
            else 
            {
                tostringstream buf;
                buf << LOG4CPLUS_TEXT("Error occured in position ") << pos
                    << LOG4CPLUS_TEXT(".\n Was expecting digit, instead got char \"")
                    << c << LOG4CPLUS_TEXT("\".");
                helpers::getLogLog().error(buf.str());
                state = LITERAL_STATE;
            }
            break;

         case MAX_STATE:
            currentLiteral += c;
            if (c >= LOG4CPLUS_TEXT('0') && c <= LOG4CPLUS_TEXT('9'))
            {
                formattingInfo.maxLen = formattingInfo.maxLen * 10 + (c - LOG4CPLUS_TEXT('0'));
            }
            else 
            {
                finalizeConverter(c);
                state = LITERAL_STATE;
            }
            break;
        }
    }

    if (!currentLiteral.empty()) 
    {
        list.push_back(new LiteralPatternConverter(currentLiteral));
    }

    return list;
}

void PatternParser::finalizeConverter(tchar c) 
{
    PatternConverter* pc = 0;
    switch (c) 
    {
    case LOG4CPLUS_TEXT('b'):
        pc = new BasicPatternConverter(formattingInfo, 
            BasicPatternConverter::BASENAME_CONVERTER);
        break;
        
    case LOG4CPLUS_TEXT('c'):
        pc = new LoggerPatternConverter(formattingInfo, 
            extractPrecisionOption());
        break;

    case LOG4CPLUS_TEXT('d'):
    case LOG4CPLUS_TEXT('D'):
        {
            tstring dOpt = extractOption();
            if (dOpt.empty()) 
            {
                dOpt = LOG4CPLUS_TEXT("%Y-%m-%d %H:%M:%S");
            }
            bool use_gmtime = c == LOG4CPLUS_TEXT('d');
            pc = new DatePatternConverter(formattingInfo, dOpt, use_gmtime);
        }
        break;

    case LOG4CPLUS_TEXT('E'):
        pc = new EnvPatternConverter(formattingInfo, extractOption());
        break;

    case LOG4CPLUS_TEXT('F'):
        pc = new BasicPatternConverter(formattingInfo, 
            BasicPatternConverter::FILE_CONVERTER);
        break;

    case LOG4CPLUS_TEXT('h'):
    case LOG4CPLUS_TEXT('H'):
        {
            bool hn = (c == LOG4CPLUS_TEXT('H'));
            pc = new HostnamePatternConverter(formattingInfo, hn);
        }
        break;

    case LOG4CPLUS_TEXT('i'):
        pc = new BasicPatternConverter(formattingInfo, 
            BasicPatternConverter::PROCESS_CONVERTER);
        break;

    case LOG4CPLUS_TEXT('l'):
        pc = new BasicPatternConverter(formattingInfo, 
            BasicPatternConverter::FULL_LOCATION_CONVERTER);
        break;

    case LOG4CPLUS_TEXT('P'):
    	pc = new BasicPatternConverter(formattingInfo,
             BasicPatternConverter::PROCESS_CONVERTER);
    	break;
    
    case LOG4CPLUS_TEXT('L'):
        pc = new BasicPatternConverter(formattingInfo, 
            BasicPatternConverter::LINE_CONVERTER);
        break;

    case LOG4CPLUS_TEXT('m'):
        pc = new BasicPatternConverter(formattingInfo, 
            BasicPatternConverter::MESSAGE_CONVERTER);
        break;

    case LOG4CPLUS_TEXT('M'):
        pc = new BasicPatternConverter(formattingInfo, 
            BasicPatternConverter::FUNCTION_CONVERTER);
        break;

    case LOG4CPLUS_TEXT('n'):
        pc = new BasicPatternConverter(formattingInfo, 
            BasicPatternConverter::NEWLINE_CONVERTER);
        break;

    case LOG4CPLUS_TEXT('p'):
        pc = new BasicPatternConverter(formattingInfo, 
            BasicPatternConverter::LOGLEVEL_CONVERTER);
        break;

    case LOG4CPLUS_TEXT('r'):
        pc = new RelativeTimestampConverter(formattingInfo);
        break;

    case LOG4CPLUS_TEXT('t'):
        pc = new BasicPatternConverter(formattingInfo, 
            BasicPatternConverter::THREAD_CONVERTER);
        break;

    case LOG4CPLUS_TEXT('T'):
        pc = new BasicPatternConverter(formattingInfo,
            BasicPatternConverter::THREAD2_CONVERTER);
        break;

    case LOG4CPLUS_TEXT('x'):
        pc = new NDCPatternConverter(formattingInfo, ndcMaxDepth);
        break;

    case LOG4CPLUS_TEXT('X'):
        pc = new MDCPatternConverter(formattingInfo, extractOption());
        break;

    default:
        tostringstream buf;
        buf << LOG4CPLUS_TEXT("Unexpected char [") << c
            << LOG4CPLUS_TEXT("] at position ") << pos
            << LOG4CPLUS_TEXT(" in conversion patterrn.");
        helpers::getLogLog().error(buf.str());
        pc = new LiteralPatternConverter(currentLiteral);
    }

    list.push_back(pc);
    currentLiteral.resize(0);
    state = LITERAL_STATE;
    formattingInfo.reset();
}

} // namespace pattern

typedef pattern::PatternConverterList PatternConverterList;

PatternLayout::PatternLayout(const tstring& pattern_)
{
    init(pattern_, 0);
}

PatternLayout::PatternLayout(const helpers::Properties& properties)
{
    unsigned ndcMaxDepth = 0;
    properties.getUInt(ndcMaxDepth, LOG4CPLUS_TEXT("NDCMaxDepth"));

    bool hasPattern = properties.exists(LOG4CPLUS_TEXT("Pattern"));
    bool hasConversionPattern = properties.exists(LOG4CPLUS_TEXT("ConversionPattern"));
    
    if (hasPattern) 
    {
        helpers::getLogLog().warn(
            LOG4CPLUS_TEXT("PatternLayout- the \"Pattern\" property has been")
            LOG4CPLUS_TEXT(" deprecated.  Use \"ConversionPattern\" instead."));
    }
    
    if (hasConversionPattern) 
    {
        init(properties.getProperty(LOG4CPLUS_TEXT("ConversionPattern")),
            ndcMaxDepth);
    }
    else if (hasPattern) 
    {
        init(properties.getProperty(LOG4CPLUS_TEXT("Pattern")), ndcMaxDepth);
    }
    else 
    {
        helpers::getLogLog().error(
            LOG4CPLUS_TEXT("ConversionPattern not specified in properties"), true);
    }
}

void PatternLayout::init(const tstring& pattern_, unsigned ndcMaxDepth)
{
    pattern = pattern_;
    parsedPattern = pattern::PatternParser(pattern, ndcMaxDepth).parse();
    PatternConverterList::iterator it = parsedPattern.begin();
    for (; it != parsedPattern.end(); ++it)
    {
        if (0 == (*it)) 
        {
            helpers::getLogLog().error(
                LOG4CPLUS_TEXT("Parsed Pattern created a NULL PatternConverter"));
            (*it) = new pattern::LiteralPatternConverter(LOG4CPLUS_TEXT(""));
        }
    }

    if (parsedPattern.empty()) 
    {
        helpers::getLogLog().warn(
            LOG4CPLUS_TEXT("PatternLayout pattern is empty.  Using default..."));
        parsedPattern.push_back(
            new pattern::BasicPatternConverter(pattern::FormattingInfo(), 
            pattern::BasicPatternConverter::MESSAGE_CONVERTER));
    }
}

PatternLayout::~PatternLayout()
{
    PatternConverterList::iterator it = parsedPattern.begin();
    for (; it != parsedPattern.end(); ++it)
    {
        delete(*it);
    }
}

void PatternLayout::formatAndAppend(tostream& output, 
    const spi::InternalLoggingEvent& event)
{
    PatternConverterList::iterator it = parsedPattern.begin();
    for (; it != parsedPattern.end(); ++it)
    {
        (*it)->formatAndAppend(output, event);
    }
}

} // namespace log4cplus

