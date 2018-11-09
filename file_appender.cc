#include "file_appender.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <log4cplus/layout.h>
#include <log4cplus/streams.h>
#include <log4cplus/helpers/loglog.h>
#include <log4cplus/helpers/stringhelper.h>
#include <log4cplus/helpers/timehelper.h>
#include <log4cplus/helpers/property.h>
#include <log4cplus/helpers/fileinfo.h>
#include <log4cplus/spi/loggingevent.h>
#include <log4cplus/spi/factory.h>
#include <log4cplus/thread/syncprims-pub-impl.h>
#include <log4cplus/internal/internal.h>
#include <fcntl.h>
#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <stdio.h>
#include <cerrno>

#ifdef LOG4CPLUS_HAVE_ERRNO_H
#include <errno.h>
#endif

using namespace log4cplus;
using namespace log4cplus::helpers;

namespace slog
{

static const std::string empty_str;

using helpers::Properties;
using helpers::Time;

const long DEFAULT_ROLLING_LOG_SIZE = 10 * 1024 * 1024L;
const long MINIMUM_ROLLING_LOG_SIZE = 200 * 1024L;

const size_t kMinimumBufferSize = 512;
const size_t kDefaultBufferSize = 4 << 10;
const size_t kMinimumCompressFlushSize = 4 << 10;
const size_t kDefaultCompressFlushSize = 512 << 10;
const float kDefaultDynamicBufferFactor = 1.2;

namespace
{

long const LOG4CPLUS_FILE_NOT_FOUND = ENOENT;

static long file_rename(tstring const &src, tstring const &target) 
{
    if (0 == std::rename(LOG4CPLUS_TSTRING_TO_STRING(src).c_str(), 
        LOG4CPLUS_TSTRING_TO_STRING(target).c_str()))
    {
        return 0;
    }
    else
    {
        return errno;
    }
}

static long file_remove(tstring const &src) 
{
    if (0 == std::remove(LOG4CPLUS_TSTRING_TO_STRING(src).c_str()))
    {
        return 0;
    }
    else
    {
        return errno;
    }
}

static void loglog_renaming_result(helpers::LogLog &loglog, 
    tstring const &src, tstring const &target, long ret) 
{
    if (0 == ret) 
    {
        loglog.debug(LOG4CPLUS_TEXT("Renamed file ") + src 
            + LOG4CPLUS_TEXT(" to ") + target);
    } 
    else if (ret != LOG4CPLUS_FILE_NOT_FOUND) 
    {
        tostringstream oss;
        oss << LOG4CPLUS_TEXT("Failed to rename file from ")
            << src << LOG4CPLUS_TEXT(" to ") << target
            << LOG4CPLUS_TEXT("; error ") << ret;
        loglog.error(oss.str());
    }
}

static void rolloverFiles(const tstring& filename, 
    unsigned int maxBackupIndex, const log4cplus::tstring& tail) 
{
    helpers::LogLog *loglog = helpers::LogLog::getLogLog();

    tostringstream buffer;
    buffer << filename << LOG4CPLUS_TEXT(".") << maxBackupIndex << tail;
    long ret = file_remove(buffer.str());

    tostringstream source_oss;
    tostringstream target_oss;

    for (int i = maxBackupIndex - 1; i >= 1; --i)
    {
        source_oss.str(LOG4CPLUS_TEXT(""));
        target_oss.str(LOG4CPLUS_TEXT(""));

        source_oss << filename << LOG4CPLUS_TEXT(".") << i << tail;
        target_oss << filename << LOG4CPLUS_TEXT(".") << (i+1) << tail;

        tstring const source(source_oss.str());
        tstring const target(target_oss.str());

        ret = file_rename(source, target);
        loglog_renaming_result(*loglog, source, target, ret);
    }
}

log4cplus::tstring& ltrim(log4cplus::tstring& ss)
{
    tstring::iterator p = find_if(ss.begin(),
        ss.end(), std::not1(std::ptr_fun(::isspace)));
    ss.erase(ss.begin(), p);

    return ss;
}

log4cplus::tstring& rtrim(log4cplus::tstring& ss)
{
    tstring::reverse_iterator p = find_if(ss.rbegin(), 
        ss.rend(), std::not1(std::ptr_fun(::isspace)));
    ss.erase(p.base(), ss.end());

    return ss;
}

log4cplus::tstring& trim(log4cplus::tstring& ss)
{
    return ltrim(rtrim(ss));
} 

} // namespace

DynamicBuffer::DynamicBuffer(size_t initial, double factor)
    : size_(initial), factor_(factor)
{
    buf_ = (char *)malloc(size_);
    if (factor_ <= 1) 
    {
        factor_ = kDefaultDynamicBufferFactor;
    }
}

void DynamicBuffer::Extend() 
{
    size_ = (size_t)(size_ * factor_);
    buf_ = (char*)realloc(buf_, size_);
}

bool LogBuffer::ShouldFlush() const 
{
    size_t used = stream_.rdbuf()->pubseekoff(0, std::ios_base::cur, 
        std::ios_base::out);

    return used >= max_;
}

int LogBuffer::Flush(bool) 
{
    if (!file_) 
    {
        return -1;
    }

    std::string buf = stream_.str();
    size_t used = stream_.rdbuf()->pubseekoff(0, std::ios_base::cur, 
        std::ios_base::out);
    int ret = write(file_->fd(), buf.data(), used);
    Clear();

    return ret;
}

void LogBuffer::Clear() 
{
    stream_.rdbuf()->pubseekpos(0, std::ios_base::out);
    logs_ = 0; 
    if (file_) 
    {
        file_.reset();
    }
}

GzLogBuffer::GzLogBuffer(size_t max, size_t real_flush)
    : LogBuffer(max)
    , real_flush_(real_flush)
    , input_size_(0)
    , buffer_(max, 1.5)
    , offset_(0)
{
    memset(&strm_, 0, sizeof(strm_));
    int ret = ::deflateInit2(&strm_, Z_DEFAULT_COMPRESSION, 
        Z_DEFLATED, 15|16, 8, Z_DEFAULT_STRATEGY);
    (void)ret;
}

GzLogBuffer::~GzLogBuffer() 
{
    ::deflateEnd(&strm_);
}

int GzLogBuffer::Flush(bool force) 
{
    if (!file_) 
    {
        return -1;
    }

    std::string buf = stream_.str();
    const char* input = buf.data();
    int inlen = stream_.rdbuf()->pubseekoff(0, std::ios_base::cur, 
        std::ios_base::out);
    input_size_ += inlen;

    strm_.next_in = (Bytef*)input;
    strm_.avail_in = inlen;

    while (buffer_.size() - offset_ <= 128) 
    {
        buffer_.Extend();
    }
    size_t out_avail = buffer_.size() - offset_;
    strm_.next_out = (Bytef*)buffer_.ptr() + offset_;
    strm_.avail_out = out_avail;

    int ret = 0;
    if ((input_size_ < real_flush_) && !force) 
    {
        while (1) 
        {
            ret = ::deflate(&strm_, Z_NO_FLUSH);
            if (ret == Z_OK) 
            {
                offset_ += out_avail - strm_.avail_out;
                if (strm_.avail_in == 0) 
                {
                    stream_.rdbuf()->pubseekpos(0, std::ios_base::out);

                    break;
                }

                while (buffer_.size() - offset_ <= 128) 
                {
                    buffer_.Extend();
                }
                out_avail = buffer_.size() - offset_;
                strm_.next_out = (Bytef *)buffer_.ptr() + offset_;
                strm_.avail_out = out_avail;
            } 
            else 
            {
                Clear();

                break;
            }
        }
    } 
    else 
    {
        while (1) 
        {
            ret = ::deflate(&strm_, Z_FINISH);
            if (ret == Z_OK) 
            {
                offset_ += out_avail - strm_.avail_out;

                while (buffer_.size() - offset_ <= 128) 
                {
                    buffer_.Extend();
                }
                out_avail = buffer_.size() - offset_;
                strm_.next_out = (Bytef *)buffer_.ptr() + offset_;
                strm_.avail_out = out_avail;
            } 
            else if (ret == Z_STREAM_END) 
            {
                offset_ += out_avail - strm_.avail_out;

                break;
            } 
            else 
            {
                Clear();

                break;
            }
        }

        if (ret >= 0) 
        {
            ret = write(file_->fd(), buffer_.ptr(), offset_);
            Clear();
        }
    }

    return ret;
}

void GzLogBuffer::Clear() 
{
    LogBuffer::Clear();
    input_size_ = 0;
    offset_ = 0;
    ::deflateReset(&strm_);
}

FileAppender::FileAppender(const tstring& filename_, bool immediateFlush_)
    : immediateFlush(immediateFlush_)
    , reopenDelay(1)
    , bufferSize(kDefaultBufferSize)
    , compressFlushSize(kDefaultCompressFlushSize)
    , appendMode(true)
    , compressType(kNoCompress)
    , closeOnExec(false)
{
    init(filename_, empty_str);
}

FileAppender::FileAppender(const Properties& props)
    : Appender(props)
    , immediateFlush(false)
    , reopenDelay(1)
    , bufferSize(kDefaultBufferSize)
    , compressFlushSize(kDefaultCompressFlushSize)
    , appendMode(true)
    , compressType(kNoCompress)
    , closeOnExec(false)
{
    bool append = false;
    tstring const & fn = props.getProperty(LOG4CPLUS_TEXT("File"));
    if (fn.empty())
    {
        getErrorHandler()->error(LOG4CPLUS_TEXT("Invalid filename"));

        return;
    }

    props.getBool(closeOnExec, LOG4CPLUS_TEXT("CloseOnExec"));
    props.getBool(immediateFlush, LOG4CPLUS_TEXT("ImmediateFlush"));
    props.getBool(append, LOG4CPLUS_TEXT("Append"));
    props.getInt(reopenDelay, LOG4CPLUS_TEXT("ReopenDelay"));
    props.getULong(bufferSize, LOG4CPLUS_TEXT("BufferSize"));
    if (bufferSize < kMinimumBufferSize) 
    {
        bufferSize = kMinimumBufferSize;
    }

    tstring lockFileName = props.getProperty(LOG4CPLUS_TEXT("LockFile"));
    if (useLockFile && lockFileName.empty()) 
    {
        lockFileName = fn;
        lockFileName += LOG4CPLUS_TEXT(".lock");
    }

    log4cplus::tstring compress = props.getProperty(LOG4CPLUS_TEXT("Compress"));
    compress = toLower(compress);

    if (compress == LOG4CPLUS_TEXT("gz")) 
    {
        compressType = kGzCompress;
        fileNamePostfix = LOG4CPLUS_TEXT(".gz");
    }

    if (compressType != kNoCompress) 
    {
        props.getULong(compressFlushSize, "CompressFlushSize");
        unsigned long minimum = std::min(bufferSize, kMinimumCompressFlushSize);
        if (compressFlushSize < minimum) 
        {
            compressFlushSize = minimum;
        }
        immediateFlush = false;
    }

    init(fn, lockFileName);
}

void FileAppender::init(const tstring& filenames, 
    const log4cplus::tstring& lockFileName_)
{
    tokenize(filenames, ',', std::back_insert_iterator<std::vector<tstring> >(fileNames));
    for (std::vector<tstring>::size_type i = 0 ; i < fileNames.size() ; ++i) 
    {
         trim(fileNames[i]);
    }

    helpers::LockFileGuard guard;

    if (useLockFile && ! lockFile.get())
    {
        try
        {
            lockFile.reset(new helpers::LockFile(lockFileName_));
            guard.attach_and_lock(*lockFile);
        }
        catch (std::runtime_error const &)
        {
            return;
        }
    }
}

FileAppender::~FileAppender()
{
    destructorImpl();
}

void FileAppender::close()
{
    thread::MutexGuard guard(access_mutex);
    size_t total_logs = 0;
    std::list<boost::shared_ptr<LogBuffer> >::iterator it = buffers.begin();
    for ( ; it != buffers.end(); ) 
    {
        size_t logs = (*it)->GetLogCount();
        if (logs != 0) 
        {
            total_logs += logs;
            ++it;
        } 
        else 
        {
            buffers.erase(it++);
        }
    }

    if (total_logs) 
    {
        for (it = buffers.begin(); it != buffers.end(); ++it) 
        {
            if (!(*it)->file()) 
            {
                if (!openFile((*it)->index())) 
                {
                    std::stringstream errmsg;
                    errmsg << "Dropped " << (*it)->GetLogCount() << " logs. "
                        << "Open " << currentFileNames[(*it)->index()] << ": "
                        << strerror(errno);
                    getLogLog().error(errmsg.str());

                    continue;
                }
            }

            const boost::shared_ptr<LogBuffer>& buffer = (*it);
            FlushBuffer(buffer, true, false);
        }
    }

    closeFiles();
    closed = true;
}

void FileAppender::closeFiles() 
{
    for (size_t i = 0; i < logFiles.size(); i++) 
    {
        closeFile(i);
    }
}

int FileAppender::doOpenFile(const std::string& fname, 
    bool append, bool cloexec) 
{
    int flags = O_CREAT | O_WRONLY;
    if (append) 
    {
        flags |= O_APPEND;
    } 
    else 
    {
        flags |= O_TRUNC;
    }

    int fd = open(fname.c_str(), flags, 0644);
    if (fd >= 0 && cloexec) 
    {
        ::fcntl(fd, F_SETFD, FD_CLOEXEC);
    }

    return fd;
}

bool FileAppender::openFiles() 
{
    currentFileNames = getFileNames();
    for (size_t i = 0; i < currentFileNames.size(); i++) 
    {
        int fd = doOpenFile(currentFileNames[i], appendMode, closeOnExec);
        if (fd < 0) 
        {
            std::stringstream errmsg;
            errmsg <<  "Open " << currentFileNames[i] << ": " << strerror(errno);
            getLogLog().error(errmsg.str());

            return false;
        } 
        else 
        {
            LogFilePtr logfile(new LogFile(fd));
            if (logFiles.size() > i) 
            {
                logFiles[i] = logfile;
            } 
            else 
            {
                logFiles.push_back(logfile);
            }
        }
    }

    return true;
}

bool FileAppender::openFile(size_t index) 
{
    assert(!logFiles[index]);

    currentFileNames[index] = getFileName(index);
    int fd = doOpenFile(currentFileNames[index], appendMode, closeOnExec);
    if (fd < 0) 
    {
        return false;
    } 
    else 
    {
        logFiles[index].reset(new LogFile(fd));

        return true;
    }
}

void FileAppender::closeFile(size_t index) 
{
    if (logFiles[index]) 
    {
        logFiles[index].reset();
    }
}

bool FileAppender::FlushBuffer(const boost::shared_ptr<LogBuffer>& buffer,
    bool force, bool unlock) 
{
    bool ret = true;

    if (unlock) 
    {
        access_mutex.unlock();
    }

    int r = buffer->Flush(force);
    if (r < 0) 
    {
        ret = false;
        std::stringstream errmsg;
        errmsg << "Dropped " << buffer->GetLogCount() << " logs. ";
        if (r == -1) 
        {
            errmsg << "Write " << currentFileNames[buffer->index()] 
                << ": " << strerror(errno);
            closeFile(buffer->index());
        } 
        else 
        {
            errmsg << "Compression: " << zError(r);
        }

        getLogLog().error(errmsg.str());
    }

    if (unlock) 
    {
        access_mutex.lock();
    }

    return ret;
}

bool FileAppender::checkAndRollover(size_t index) 
{
    if (!logFiles[index]) 
    {
        if (!openFile(index)) 
        {
            std::stringstream errmsg;
            errmsg << "Dropped logs. Open "
                << currentFileNames[index] << ": " << strerror(errno);
            getLogLog().error(errmsg.str());

            return false;
        }
    } 

    return true;
}

void FileAppender::append(const spi::InternalLoggingEvent& event)
{
    if (logFiles.empty()) 
    {
        if (!openFiles()) 
        {
            return;
        }
    }

    size_t index = 0;

    if (buffers.empty()) 
    {
        boost::shared_ptr<LogBuffer> buffer;
        if (compressType == kNoCompress) 
        {
            buffer.reset(new LogBuffer(bufferSize));
        } 
        else 
        {
            buffer.reset(new GzLogBuffer(bufferSize, compressFlushSize));
        }

        index = (index + 1) % fileNames.size();
        if (index >= fileNames.size()) 
        {
            return;
        }

        buffer->setfile(index, logFiles[index]);
        buffers.push_back(buffer);
    }

    if (!checkAndRollover(index)) 
    {
        return;
    }

    boost::shared_ptr<LogBuffer> buffer = buffers.front();
    bool isleavebuffers = false;
    if (buffer->file()) 
    {
        if (buffer->file()->fd() != logFiles[buffer->index()]->fd()) 
        {
            buffers.pop_front();
            isleavebuffers = true;
            FlushBuffer(buffer, true, true);
            index = (buffer->index() + 1) % fileNames.size();
            buffer->setfile(index, logFiles[index]);
        }
    } 
    else 
    {
        index = (buffer->index() + 1) % fileNames.size();
        buffer->setfile(index, logFiles[index]);
    }

    assert((buffer->file() && buffer->file()->fd() == logFiles[buffer->index()]->fd()) 
        || buffer->GetLogCount() == 0);
    layout->formatAndAppend(buffer->stream(), event);
    buffer->AddLogCount();

    if (buffer->ShouldFlush() || immediateFlush) 
    {
        assert(buffer->file());
        if (!isleavebuffers) 
        {
            buffers.pop_front();
            isleavebuffers = true;
        }

        FlushBuffer(buffer, false, true);

        if (compressType == kGzCompress && buffer->file() 
            && buffer->file()->fd() != logFiles[buffer->index()]->fd()) 
        {
            FlushBuffer(buffer, true, true);
        }

        assert((buffer->file() && buffer->file()->fd() == logFiles[buffer->index()]->fd())
            || buffer->GetLogCount() == 0);
    }

    if (isleavebuffers) 
    {
        buffers.push_back(buffer);
    }
}

RollingFileAppender::RollingFileAppender(const tstring& filename_,
    long maxFileSize_, int maxBackupIndex_, bool immediateFlush_)
    : FileAppender(filename_, immediateFlush_)
{
    init(maxFileSize_, maxBackupIndex_);
}

RollingFileAppender::RollingFileAppender(const Properties& properties)
    : FileAppender(properties)
{
    long tmpMaxFileSize = DEFAULT_ROLLING_LOG_SIZE;
    int tmpMaxBackupIndex = 1;
    tstring tmp(helpers::toUpper(properties.getProperty(LOG4CPLUS_TEXT("MaxFileSize"))));
    if (!tmp.empty()) 
    {
        tmpMaxFileSize = std::atoi(LOG4CPLUS_TSTRING_TO_STRING(tmp).c_str());
        if (tmpMaxFileSize != 0)
        {
            tstring::size_type const len = tmp.length();
            if (len > 2 && tmp.compare(len - 2, 2, LOG4CPLUS_TEXT("MB")) == 0)
            {
                tmpMaxFileSize *= (1024 * 1024);
            }
            else if (len > 2 && tmp.compare(len - 2, 2, LOG4CPLUS_TEXT("KB")) == 0)
            {
                tmpMaxFileSize *= 1024;
            }
        }
        tmpMaxFileSize = (std::max)(tmpMaxFileSize, MINIMUM_ROLLING_LOG_SIZE);
    }

    properties.getInt(tmpMaxBackupIndex, LOG4CPLUS_TEXT("MaxBackupIndex"));

    init(tmpMaxFileSize, tmpMaxBackupIndex);
}

void RollingFileAppender::init(long maxFileSize_, int maxBackupIndex_)
{
    if (maxFileSize_ < MINIMUM_ROLLING_LOG_SIZE) 
    {
        tostringstream oss;
        oss << LOG4CPLUS_TEXT("RollingFileAppender: MaxFileSize property")
            LOG4CPLUS_TEXT(" value is too small. Resetting to ")
            << MINIMUM_ROLLING_LOG_SIZE << ".";
        helpers::getLogLog().warn(oss.str());
        maxFileSize_ = MINIMUM_ROLLING_LOG_SIZE;
    }

    maxFileSize = maxFileSize_;
    maxBackupIndex = (std::max)(maxBackupIndex_, 1);
}

RollingFileAppender::~RollingFileAppender()
{
    destructorImpl();
}

bool RollingFileAppender::checkAndRollover(size_t index) 
{
    if (!logFiles[index]) 
    {
    	if (!openFile(index)) 
        {
    	    std::stringstream errmsg;
            errmsg << "Dropped logs. Open "
                << currentFileNames[index] << ": " << strerror(errno);
            getLogLog().error(errmsg.str());

    	    return false;
    	}
    }

    struct stat st = {};
    int r = fstat(logFiles[index]->fd(), &st);
    if (r < 0) 
    {
        std::stringstream errmsg;
        errmsg << "stat " << currentFileNames[index] << ": " << strerror(errno);
        getLogLog().error(errmsg.str());
        closeFile(index);

        return false;
    }

    if (st.st_size >= maxFileSize) 
    {
        helpers::FileInfo fi;
        if (getFileInfo(&fi, currentFileNames[index]) == -1
            || fi.size < maxFileSize)
        {
            int fd = doOpenFile(currentFileNames[index], true, closeOnExec);
            if (fd >= 0) 
            {
                logFiles[index].reset(new LogFile(fd));
            }

            return true;
        }

        if (maxBackupIndex > 0) 
        {
            rolloverFiles(fileNames[index], maxBackupIndex, fileNamePostfix);
            tstring target = fileNames[index] + LOG4CPLUS_TEXT(".1") + fileNamePostfix;

            getLogLog().debug(LOG4CPLUS_TEXT("Renaming file ") 
                + currentFileNames[index]  + fileNamePostfix + LOG4CPLUS_TEXT(" to ")
                + target);
            long ret = file_rename(fileNames[index] + fileNamePostfix, target);
            loglog_renaming_result(getLogLog(), fileNames[index] + fileNamePostfix, target, ret);
        }
    }

    return true;
}

DailyRollingFileAppender::DailyRollingFileAppender(const tstring& filename_,
    DailyRollingFileSchedule schedule_, bool immediateFlush_)
    : FileAppender(filename_, immediateFlush_)
    , filename_ori(filename_)
    , multiple(1)
{
    init(schedule_);
}

DailyRollingFileAppender::DailyRollingFileAppender(const Properties& properties)
    : FileAppender(properties)
    , multiple(1)
{
    DailyRollingFileSchedule theSchedule = DAILY;
    tstring scheduleStr(helpers::toUpper(properties.getProperty(LOG4CPLUS_TEXT("Schedule"))));

    if (scheduleStr == LOG4CPLUS_TEXT("MONTHLY"))
    {
        theSchedule = MONTHLY;
    }
    else if (scheduleStr == LOG4CPLUS_TEXT("WEEKLY"))
    {
        theSchedule = WEEKLY;
    }
    else if (scheduleStr == LOG4CPLUS_TEXT("DAILY"))
    {
        theSchedule = DAILY;
    }
    else if (scheduleStr == LOG4CPLUS_TEXT("TWICE_DAILY"))
    {
        theSchedule = TWICE_DAILY;
    }
    else if (scheduleStr == LOG4CPLUS_TEXT("HOURLY"))
    {
        theSchedule = HOURLY;
    }
    else if (scheduleStr == LOG4CPLUS_TEXT("MINUTELY")) 
    {
        theSchedule = MINUTELY;
        if (properties.exists(LOG4CPLUS_TEXT("Multiple")))
        {
            tstring tmp = properties.getProperty(LOG4CPLUS_TEXT("Multiple"));
            multiple = atoi(LOG4CPLUS_TSTRING_TO_STRING(tmp).c_str());
            if (multiple < 1)
            {
                multiple = 1;
            }

            if (multiple > 59)
            {
                theSchedule = HOURLY;
            }
 
            if (multiple > 30)
            {
                multiple = 30;
            }
        }
    } 
    else 
    {
        helpers::getLogLog().warn(
            LOG4CPLUS_TEXT("DailyRollingFileAppender::ctor()")
            LOG4CPLUS_TEXT("- \"Schedule\" not valid: ")
            + properties.getProperty(LOG4CPLUS_TEXT("Schedule")));
        theSchedule = DAILY;
    }
    
    init(theSchedule);
}

void DailyRollingFileAppender::init(DailyRollingFileSchedule sch)
{
    this->schedule = sch;

    Time now = Time::gettimeofday();
    nextRolloverTime = calculateNextRolloverTime(now);
}

DailyRollingFileAppender::~DailyRollingFileAppender()
{
    destructorImpl();
}

bool DailyRollingFileAppender::checkAndRollover(size_t index) 
{
    Time now = Time::gettimeofday();
    if (now >= nextRolloverTime) 
    {
        nextRolloverTime = calculateNextRolloverTime(now);

        closeFiles();
        assert(!logFiles[index]);

        if (!openFiles()) 
        {
            std::stringstream errmsg;
            errmsg << "Dropped logs. Open files "
                << currentFileNames[index] << ": " << strerror(errno);
            getLogLog().error(errmsg.str());	

            return false;
        }

        std::list<boost::shared_ptr<LogBuffer> > tmp_buffers;
        std::list<boost::shared_ptr<LogBuffer> >::iterator it = buffers.begin();
        for ( ; it != buffers.end(); ) 
        {
            if ((*it)->file()) 
            {
                tmp_buffers.push_back(*it);
                buffers.erase(it++);
            } 
            else 
            {
                it++;
            }
        }
        it = tmp_buffers.begin();
        for ( ; it != tmp_buffers.end(); ) 
        {
            assert((*it)->file());
            FlushBuffer(*it, true, true);
            buffers.push_back(*it);
            tmp_buffers.erase(it++);
        }
    } 
    else 
    {
        if (!logFiles[index]) 
        {
            if (!openFile(index)) 
            {
                std::stringstream errmsg;
                errmsg << "Dropped logs. Open "
                    << currentFileNames[index] << ": " << strerror(errno);
                getLogLog().error(errmsg.str());	

                return false;
            }
        }
    }

    return true;
}

Time DailyRollingFileAppender::calculateNextRolloverTime(const Time& t) const
{
    Time ret = t;
    ret.usec(0);
    struct tm time;
    t.localtime(&time);
    time.tm_sec = 0;

    switch (schedule)
    {
    case MONTHLY: 
    {
        time.tm_mday = 1;
        time.tm_hour = 0;
        time.tm_min = 0;
        time.tm_mon += 1;
        time.tm_isdst = 0;

        if (ret.setTime(&time) == -1) 
        {
            helpers::getLogLog().error(
                "DailyRollingFileAppender::calculateNextRolloverTime()-"
                " setTime() returned error");
            ret = (t + Time(2678400));
        }

        return ret;
    }

    case WEEKLY:
        time.tm_mday -= (time.tm_wday % 7);
        time.tm_hour = 0;
        time.tm_min = 0;
        ret.setTime(&time);
        return (ret + Time(7 * 24 * 60 * 60));

    default:
        helpers::getLogLog().error(
            "DailyRollingFileAppender::calculateNextRolloverTime()-"
            " invalid schedule value");

    case DAILY:
        time.tm_hour = 0;
        time.tm_min = 0;
        ret.setTime(&time);
        return (ret + Time(24 * 60 * 60));

    case TWICE_DAILY:
        if (time.tm_hour >= 12) 
        {
            time.tm_hour = 12;
        } 
        else 
        {
            time.tm_hour = 0;
        }
        time.tm_min = 0;
        ret.setTime(&time);
        return (ret + Time(12 * 60 * 60));

    case HOURLY:
        time.tm_min = 0;
        ret.setTime(&time);
        return (ret + Time(60 * 60));

    case MINUTELY:
        {
            ret += Time(60 * multiple);
            ret.localtime(&time);
            time.tm_sec = 0;
            time.tm_min = (time.tm_min / multiple) * multiple;
            ret.setTime(&time);
            return ret;
        }
    };
}

std::vector<log4cplus::tstring> DailyRollingFileAppender::getFileNames() const
{
    std::vector<log4cplus::tstring> tmp;
    for (size_t i = 0; i < fileNames.size(); i++) 
    {
        tmp.push_back(getFileName(i));
    }

    return tmp;
}

tstring DailyRollingFileAppender::getFileName(size_t index) const
{
    Time now = Time::gettimeofday();
    tchar const * pattern = 0;
    switch (schedule)
    {
    case MONTHLY:
        pattern = LOG4CPLUS_TEXT("%Y-%m");
        break;

    case WEEKLY:
        pattern = LOG4CPLUS_TEXT("%Y-%W");
        break;

    default:
        helpers::getLogLog().error(
            LOG4CPLUS_TEXT("DailyRollingFileAppender::getFilename()-")
            LOG4CPLUS_TEXT(" invalid schedule value"));

    case DAILY:
        pattern = LOG4CPLUS_TEXT("%Y-%m-%d");
        break;

    case TWICE_DAILY:
        pattern = LOG4CPLUS_TEXT("%Y-%m-%d-%p");
        break;

    case HOURLY:
        pattern = LOG4CPLUS_TEXT("%Y-%m-%d-%H");
        break;

    case MINUTELY:
        pattern = LOG4CPLUS_TEXT("%Y-%m-%d-%H-%M");
        struct tm time;
        now.localtime(&time);
        time.tm_sec = 0;
        time.tm_min = (time.tm_min / multiple) * multiple;
        now.setTime(&time);
        break;
    };

    tstring result (fileNames[index]);
    result += LOG4CPLUS_TEXT(".");
    result += now.getFormattedTime(pattern, false);

    return result + fileNamePostfix;
}

} // namespace slog

