# Build and run the host-side harness against every ported algorithm.
#
#     make -C distingNT/test
#
# Each algorithm is linked into its own executable together with harness.cpp
# and nt_stub.cpp, because every plug-in defines pluginEntry(). The harness
# stands in for the module: it honours the memory each algorithm declares,
# feeds its parameters and busses, renders, and checks the result. See the
# comment at the top of harness.cpp for what it does and does not cover.

NT_API_PATH ?= ../../dep/distingNT_API

# the PotatoChips DSP layer, included as e.g. "dsp/blip_buffer.hpp"
SRC_PATH := ../../src

CXX ?= g++

INCLUDES := -I../include -I$(SRC_PATH) -I$(NT_API_PATH)/include -I.

# The host build turns on the sanitizers, which is the point of running here:
# the module has no way to tell us about an out-of-bounds write.
CXXFLAGS := \
	-std=c++11 \
	-O1 -g \
	-MMD -MP \
	-fsanitize=address,undefined \
	-fno-sanitize-recover=undefined \
	-fno-omit-frame-pointer \
	-Wall -Wno-class-memaccess -Wno-unused-variable

LDFLAGS := -fsanitize=address,undefined -fno-sanitize-recover=undefined

BUILD_DIR := build

SOURCES := $(wildcard ../plugins/*.cpp)
OBJECTS := $(patsubst ../plugins/%.cpp,$(BUILD_DIR)/%.o,$(SOURCES))
BINARIES := $(patsubst ../plugins/%.cpp,$(BUILD_DIR)/%,$(SOURCES))
STAMPS := $(patsubst ../plugins/%.cpp,$(BUILD_DIR)/%.passed,$(SOURCES))

SHARED := $(BUILD_DIR)/harness.o $(BUILD_DIR)/nt_stub.o

.PHONY: all build clean

all: build $(STAMPS)
	@echo "every algorithm passed"

build: $(BINARIES)

$(BUILD_DIR)/harness.o: harness.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

$(BUILD_DIR)/nt_stub.o: nt_stub.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

$(BUILD_DIR)/%.o: ../plugins/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

$(BUILD_DIR)/%: $(BUILD_DIR)/%.o $(SHARED)
	$(CXX) $(LDFLAGS) -o $@ $^

$(BUILD_DIR)/%.passed: $(BUILD_DIR)/%
	@./$< && touch $@

# rebuild when any header the algorithms pull in changes
-include $(wildcard $(BUILD_DIR)/*.d)

clean:
	rm -rf $(BUILD_DIR)
