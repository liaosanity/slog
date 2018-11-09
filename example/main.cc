#include <iostream>
#include <string>

#include "slog.h"

using namespace std;
using namespace slog;

#define LOGGING_INIT(file) sLogConfig(file)
#define LOG(level) sLog("Test", level)

const int DEBUG = slog::SLOG_DEBUG;
const int INFO  = slog::SLOG_INFO;
const int WARN  = slog::SLOG_WARN;
const int ERROR = slog::SLOG_ERROR;

int main(int argc, char** argv)
{
    LOGGING_INIT("./logging.conf"); 

    LOG(INFO) << "Hello, I'm INFO.";
    LOG(WARN) << "Hello, I'm WARN.";
    LOG(ERROR) << "Hello, I'm ERROR.";

    cout << "test finish." << endl;

    return 0;
}
