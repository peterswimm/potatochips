# Build the PotatoChips algorithms as disting NT plug-ins.
#
# Each .cpp in plugins/ becomes one .o to copy to the module's SD card, under
# /programs/plug-ins/.
#
# Requires the GNU Arm Embedded Toolchain (arm-none-eabi-c++) and the disting
# NT API headers. The API ships as a submodule of this repository:
#
#     git submodule update --init dep/distingNT_API
#     make -C distingNT
#
# Point NT_API_PATH at your own checkout to build against a different version.

NT_API_PATH ?= ../dep/distingNT_API

# the PotatoChips DSP layer, included as e.g. "dsp/blip_buffer.hpp"
SRC_PATH := ../src

CXX := arm-none-eabi-c++

INCLUDES := -Iinclude -I$(SRC_PATH) -I$(NT_API_PATH)/include

# Flags for the disting NT's Cortex-M7. Taken from the API's own examples,
# less the warnings raised by the (unmodified) PotatoChips DSP headers:
#   -Wno-class-memaccess   the emulators memset() their oscillator structs
#   -Wno-unused-variable   dsp/wavetable4bit.hpp defines unused wavetables
#
# -fno-threadsafe-statics is added on top of the examples' flags: the module is
# single-threaded, and without it the function-local statics in the emulators'
# lookup tables pull in __cxa_guard_acquire/release.
CXXFLAGS := \
	-std=c++11 \
	-mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb \
	-fno-rtti -fno-exceptions -fno-threadsafe-statics \
	-Os -fPIC \
	-Wall -Wno-class-memaccess -Wno-unused-variable

BUILD_DIR := build

SOURCES := $(wildcard plugins/*.cpp)
OBJECTS := $(patsubst plugins/%.cpp,$(BUILD_DIR)/%.o,$(SOURCES))

.PHONY: all clean

all: $(OBJECTS)

$(BUILD_DIR)/%.o: plugins/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

clean:
	rm -rf $(BUILD_DIR)
