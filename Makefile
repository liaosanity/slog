###
PREFIX=/usr/local
LOG4CPLUS=$(HOME)/opt/log4cplus-1.2.1
BOOST=$(HOME)/opt/boost-1.50.0

CXXFLAGS := -g3 -O2 -Wall -fno-strict-aliasing -DINSIDE_LOG4CPLUS \
    -Wno-error=unused-but-set-variable -Wno-error=unused-but-set-parameter \
    -fPIC \
    -I . \
    -I $(LOG4CPLUS)/include \
    -I $(BOOST)/include

LDFLAGS := -pthread \
	-L. \
	-L $(LOG4CPLUS)/lib

RTFLAGS := \
	-Wl,-rpath=.

LIBS := -lz \
        -ldl \
        -llog4cplus \
	$(BOOST)/lib/libboost_thread.a

SRC := $(wildcard *.cc)
OBJ := $(patsubst %.cc, %.o, $(SRC))
DEP := $(patsubst %.o, %.d, $(OBJ))

VERSION=1.0.0
TARGET := libslog.so

ifeq ($(USE_DEP),1)
-include $(DEP) 
endif

all:
	$(MAKE) USE_DEP=1 target

$(TARGET).$(VERSION): $(OBJ)
	$(CXX) $^ -o $@ $(RTFLAGS) $(LDFLAGS) $(LIBS) -shared

target: $(TARGET).$(VERSION)

%.o : %.cc
	$(CXX) -c $(CXXFLAGS) $< -o $@

%.d : %.cc
	@$(CXX) -MM $< $(CXXFLAGS) | sed 's/$(notdir $*)\.o/$(subst /,\/,$*).o $(subst /,\/,$*).d/g' > $@

clean:
	-rm -rf $(OBJ) $(TARGET).$(VERSION) *.pid *.log *.core $(DEP) *.so

install:
	if ( test ! -d $(PREFIX)/include ) ; then mkdir -p $(PREFIX)/include ; fi
	if ( test ! -d $(PREFIX)/lib ) ; then mkdir -p $(PREFIX)/lib ; fi
	cp -f slog.h $(PREFIX)/include
	chmod a+r $(PREFIX)/include/*.h
	cp -f $(TARGET).$(VERSION) $(PREFIX)/lib
	chmod a+r $(PREFIX)/lib/$(TARGET).$(VERSION)
	cd $(PREFIX)/lib/ && ln -s -f $(TARGET).$(VERSION) $(TARGET)

.PHONY: all target clean
