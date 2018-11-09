LOG4CPLUS=$(HOME)/opt/log4cplus-1.2.1
SLOG=$(HOME)/opt/slog-1.0.0
BOOST=$(HOME)/opt/boost-1.50.0

CXXFLAGS := -g3 -O2 -Wall -fno-strict-aliasing \
    -Wno-error=unused-but-set-variable -Wno-error=unused-but-set-parameter \
    -std=c++0x \
    -I . \
    -I $(LOG4CPLUS)/include \
    -I $(SLOG)/include \
    -I $(BOOST)/include

LDFLAGS := -pthread \
	-L. \
	-L $(LOG4CPLUS)/lib \
	-L $(SLOG)/lib

RTFLAGS := \
	-Wl,-rpath=.

LIBS := -llog4cplus \
	-lslog \
	$(BOOST)/lib/libboost_thread.a \
	$(BOOST)/lib/libboost_chrono.a \
	$(BOOST)/lib/libboost_system.a \
	$(BOOST)/lib/libboost_filesystem.a 

SRC := $(wildcard *.cc)

OBJ := $(patsubst %.cc, %.o, $(SRC))
DEP := $(patsubst %.o, %.d, $(OBJ))

TARGET := test 

ifeq ($(USE_DEP),1)
-include $(DEP) 
endif

all:
	$(MAKE) USE_DEP=1 target

test: main.o
	$(CXX) $^ -o $@ $(RTFLAGS) $(LDFLAGS) $(LIBS)

target: $(TARGET)

%.o : %.cc
	$(CXX) -c $(CXXFLAGS) $< -o $@

%.d : %.cc
	@$(CXX) -MM $< $(CXXFLAGS) | sed 's/$(notdir $*)\.o/$(subst /,\/,$*).o $(subst /,\/,$*).d/g' > $@

clean:
	-rm -rf $(OBJ) $(TARGET) *.pid *.log *.core $(DEP) *.so

.PHONY: all target clean
