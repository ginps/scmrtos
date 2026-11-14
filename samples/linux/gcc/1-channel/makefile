# scmRTOS sample for the Linux port: 1-channel
#   make                      - build with defaults (scheme 0, real-time ticks)
#   make SCHEME=1 ORDER=1     - context switch by software interrupt, descending priorities
#   make run
SCMRTOS ?= ../../../..
SCHEME  ?= 0
ORDER   ?= 0
CXX     ?= g++

CXXFLAGS += -std=c++11 -O2 -g -Wall -Wextra -Wno-unused-parameter \
            -DscmRTOS_CONTEXT_SWITCH_SCHEME=$(SCHEME) -DscmRTOS_PRIORITY_ORDER=$(ORDER) \
            -Isrc -I$(SCMRTOS)/core -I$(SCMRTOS)/port/linux/gcc

SRC = src/main.cpp \
      $(SCMRTOS)/core/os_kernel.cpp \
      $(SCMRTOS)/core/os_services.cpp \
      $(wildcard $(SCMRTOS)/core/usrlib.cpp) \
      $(SCMRTOS)/port/linux/gcc/os_target.cpp

1-channel: $(SRC) src/*.h $(SCMRTOS)/core/*.h $(SCMRTOS)/port/linux/gcc/os_target.h
	$(CXX) $(CXXFLAGS) -o $@ $(SRC)

run: 1-channel
	./1-channel

clean:
	rm -f 1-channel

.PHONY: run clean
