#!/bin/sh

LOG4CPLUS=${HOME}/opt/log4cplus-1.2.1
SLOG=${HOME}/opt/slog-1.0.0
export LD_LIBRARY_PATH=${LOG4CPLUS}/lib:${SLOG}/lib:$LD_LIBRARY_PATH

./test
