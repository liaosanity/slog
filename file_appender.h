#ifndef FILE_APPENDER_H
#define FILE_APPENDER_H

#include <log4cplus/config.hxx>
#include <log4cplus/appender.h>
#include <log4cplus/fstreams.h>
#include <log4cplus/helpers/timehelper.h>
#include <log4cplus/helpers/lockfile.h>
#include <boost/thread.hpp>
#include <boost/noncopyable.hpp>
#include <fstream>
#include <memory>
#include <sstream>
#include <zlib.h>

namespace slog 
{

enum CompressType 
{
    kNoCompress = 0,
    kGzCompress = 1,
};

class LogFile : boost::noncopyable 
{
public:
    LogFile(int fd) 
        : fd_(fd) 
    {
    }

    ~LogFile() 
    { 
        if (fd_ >= 0) 
        {
            close(fd_); 
        }
    }

    int fd() const 
    { 
        return fd_; 
    }

private:
    int fd_;
};

typedef boost::shared_ptr<LogFile> LogFilePtr;

class DynamicBuffer : boost::noncopyable 
{
public:
    DynamicBuffer(size_t initial, double factor);

    ~DynamicBuffer() 
    { 
        free(buf_); 
    }

    char* ptr() const 
    { 
        return buf_; 
    }

    size_t size() const 
    { 
        return size_; 
    }

    void Extend();

private:
    char* buf_;
    size_t size_;
    double factor_;
};

class LogBuffer : boost::noncopyable 
{
public:
    LogBuffer(size_t max)
        : max_(max)
        , logs_(0)
        , index_(0) 
    {
    }

    virtual ~LogBuffer() 
    {
    }

    std::ostream& stream() 
    { 
        return stream_; 
    }

    void AddLogCount() 
    {
        logs_++; 
    }

    size_t GetLogCount() const 
    { 
        return logs_; 
    }

    void setfile(size_t log_index, const LogFilePtr& logfile) 
    { 
        index_ = log_index; 
        file_ = logfile; 
    }

    size_t index() 
    { 
        return index_; 
    }

    LogFilePtr& file() 
    { 
        return file_; 
    }

    bool ShouldFlush() const;
    virtual int Flush(bool force);

protected:
    virtual void Clear();

protected:
    std::stringstream stream_;
    size_t max_;
    size_t logs_;
    size_t index_;
    LogFilePtr file_;
};

class GzLogBuffer : public LogBuffer 
{
public:
    GzLogBuffer(size_t max, size_t real_flush);
    virtual ~GzLogBuffer();

    virtual int Flush(bool force);

protected:
    virtual void Clear();

protected:
    size_t real_flush_;
    size_t input_size_;
    DynamicBuffer buffer_;
    size_t offset_;
    z_stream strm_;
};

class LOG4CPLUS_EXPORT FileAppender : public log4cplus::Appender 
{
public:
    FileAppender(const log4cplus::tstring& filename, bool immediateFlush = true);
    FileAppender(const log4cplus::helpers::Properties& properties);
    virtual ~FileAppender();

    virtual void close();

protected:
    virtual void append(const log4cplus::spi::InternalLoggingEvent& event);
    virtual bool checkAndRollover(size_t index);

    virtual std::vector<log4cplus::tstring> getFileNames() const 
    {
        std::vector<log4cplus::tstring> tmp;
        for (size_t i = 0; i < fileNames.size(); i++) 
        {
            tmp.push_back(fileNames[i] + fileNamePostfix);
        }

        return tmp;
    }

    virtual log4cplus::tstring getFileName(size_t index) const 
    {
        return fileNames[index] + fileNamePostfix;
    }

    static int doOpenFile(const std::string& fname, bool append, bool cloexec);
    bool openFile(size_t index);
    bool openFiles();
    void closeFile(size_t index);
    void closeFiles();
    bool FlushBuffer(const boost::shared_ptr<LogBuffer>& buffer, 
        bool force, bool unlock);

private:
    void init(const log4cplus::tstring& filenames,
        const log4cplus::tstring& lockFileName);

    FileAppender(const FileAppender&);
    FileAppender& operator=(const FileAppender&);

protected:
    bool immediateFlush;
    int reopenDelay;
    unsigned long bufferSize;
    unsigned long compressFlushSize;
    std::vector<log4cplus::tstring> fileNames;
    log4cplus::tstring fileNamePostfix;
    std::vector<log4cplus::tstring> currentFileNames;
    log4cplus::helpers::Time reopenTime;
    bool appendMode;
    CompressType compressType;
    std::list<boost::shared_ptr<LogBuffer> > buffers;
    std::vector<LogFilePtr> logFiles;
    bool closeOnExec;
};

class LOG4CPLUS_EXPORT RollingFileAppender : public slog::FileAppender 
{
public:
    RollingFileAppender(const log4cplus::tstring& filename, 
        long maxFileSize = 10 * 1024 * 1024,
        int maxBackupIndex = 1, bool immediateFlush = true);
    RollingFileAppender(const log4cplus::helpers::Properties& properties);

    virtual ~RollingFileAppender();

protected:
    virtual bool checkAndRollover(size_t index);

private:
    LOG4CPLUS_PRIVATE void init(long maxFileSize, int maxBackupIndex);

protected:
    long maxFileSize;
    int maxBackupIndex;
};

enum DailyRollingFileSchedule 
{
    MONTHLY,
    WEEKLY, 
    DAILY,
    TWICE_DAILY, 
    HOURLY, 
    MINUTELY
};

class LOG4CPLUS_EXPORT DailyRollingFileAppender : public slog::FileAppender 
{
public:
    DailyRollingFileAppender(const log4cplus::tstring& filename,
        DailyRollingFileSchedule schedule = DAILY, bool immediateFlush = true);
    DailyRollingFileAppender(const log4cplus::helpers::Properties& properties);
    virtual ~DailyRollingFileAppender();

protected:
    virtual bool checkAndRollover(size_t index);
    virtual log4cplus::tstring getFileName(size_t index) const;
    virtual std::vector<log4cplus::tstring> getFileNames() const;

    log4cplus::helpers::Time calculateNextRolloverTime(const log4cplus::helpers::Time& t) const;
    log4cplus::tstring getFilename(const log4cplus::helpers::Time& t) const;

private:
    LOG4CPLUS_PRIVATE void init(DailyRollingFileSchedule schedule);

protected:
    DailyRollingFileSchedule schedule;
    log4cplus::tstring scheduledFilename;
    log4cplus::helpers::Time nextRolloverTime;
    log4cplus::tstring filename_ori;
    int multiple; 
};

} // namespace slog

#endif
