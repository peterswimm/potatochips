// Shared helpers for hosting the PotatoChips emulators on the disting NT.
// Copyright 2020 Christian Kauten
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//

#ifndef NT_POTATOCHIPS_CHIP_HPP_
#define NT_POTATOCHIPS_CHIP_HPP_

#include "compat.hpp"
#include <distingnt/api.h>
#include "dsp/blip_buffer.hpp"
#include "dsp/math.hpp"

/// @brief Glue between the PotatoChips DSP layer and the disting NT API.
namespace NTPotatoChips {

/// the CPU clock rate the Blargg-style chip emulators are driven at
static constexpr uint32_t CLOCK_RATE = 768000;

/// the number of frames between control-rate parameter updates
/// @details
/// The Rack build updates frequency and level every sample but the remaining
/// chip registers only every 16th sample. This port keeps that division.
static constexpr unsigned CV_DIVISION = 16;

// ---------------------------------------------------------------------------
// MARK: Busses
// ---------------------------------------------------------------------------

/// @brief Return the frames of an input bus, or NULL when unrouted.
///
/// @param busFrames the frame buffer passed to step()
/// @param numFrames the number of frames in the current step
/// @param bus the value of a bus parameter, where 0 means "None"
/// @returns a pointer to the bus' frames, or NULL if bus is 0
///
static inline const float* inputBus(
    const float* busFrames,
    unsigned numFrames,
    int16_t bus
) {
    return bus ? busFrames + (bus - 1) * numFrames : NULL;
}

/// @brief Return the frames of an output bus, or NULL when unrouted.
///
/// @param busFrames the frame buffer passed to step()
/// @param numFrames the number of frames in the current step
/// @param bus the value of a bus parameter, where 0 means "None"
/// @returns a pointer to the bus' frames, or NULL if bus is 0
///
static inline float* outputBus(
    float* busFrames,
    unsigned numFrames,
    int16_t bus
) {
    return bus ? busFrames + (bus - 1) * numFrames : NULL;
}

/// @brief Return the voltage on a bus, or a normalled value when unrouted.
///
/// @param bus the bus returned by inputBus(), which may be NULL
/// @param frame the index of the frame to read
/// @param normal the voltage to return when the bus is unrouted
/// @returns the voltage to treat the input as carrying
///
static inline float voltage(const float* bus, unsigned frame, float normal) {
    return bus ? bus[frame] : normal;
}

/// @brief Write a sample to an output bus, honouring the bus' output mode.
///
/// @param bus the bus returned by outputBus(), which may be NULL
/// @param frame the index of the frame to write
/// @param value the value to write, in volts
/// @param replace true to overwrite the bus, false to add to it
///
static inline void write(float* bus, unsigned frame, float value, bool replace) {
    if (!bus) return;
    if (replace)
        bus[frame] = value;
    else
        bus[frame] += value;
}

/// @brief Linearly map a value from one range onto another.
///
/// @param x the value to map
/// @param xMin the lower bound of the input range
/// @param xMax the upper bound of the input range
/// @param yMin the lower bound of the output range
/// @param yMax the upper bound of the output range
/// @returns x mapped from [xMin, xMax] onto [yMin, yMax], without clipping
/// @details
/// Stands in for rack::math::rescale, which the Rack build uses to condition
/// trigger inputs and attenuverters.
///
static inline float rescale(
    float x,
    float xMin,
    float xMax,
    float yMin,
    float yMax
) {
    return yMin + (x - xMin) / (xMax - xMin) * (yMax - yMin);
}

/// @brief Condition a trigger input the way the Rack build does.
///
/// @param cv the voltage on the trigger input
/// @returns the signal to pass to a Trigger::Threshold
/// @details
/// Clamps the input to [0, 10]V so that bipolar signals read as unipolar, then
/// scales it such that the trigger fires at 2V and rearms below 0.01V.
///
static inline float triggerSignal(float cv) {
    return rescale(Math::clip(cv, 0.f, 10.f), 0.01f, 2.f, 0.f, 1.f);
}

// ---------------------------------------------------------------------------
// MARK: Pitch
// ---------------------------------------------------------------------------

/// @brief Return the pitch in octaves described by a coarse / fine pair.
///
/// @param coarse an offset in semitones
/// @param fine an offset in cents
/// @returns the combined offset in octaves, i.e., the Eurorack V/Oct scale
///
static inline float pitch(int16_t coarse, int16_t fine) {
    return (coarse * 100 + fine) * (1.f / 1200.f);
}

/// @brief Return the frequency in Hz for a pitch relative to C4.
///
/// @param octaves the pitch in octaves relative to C4
/// @returns the frequency in Hz, clipped to the audible range
///
static inline float frequency(float octaves) {
    return Math::clip(
        rack::dsp::FREQ_C4 * powf(2.f, octaves), 0.f, 20000.f
    );
}

/// @brief Return the frequency in Hz for a pitch relative to 1Hz.
///
/// @param octaves the pitch in octaves relative to 1Hz
/// @returns the frequency in Hz, clipped to the audible range
/// @details
/// Used by the low-frequency generators, e.g., the AY-3-8910's envelope.
///
static inline float frequencyFrom1Hz(float octaves) {
    return Math::clip(powf(2.f, octaves), 0.f, 20000.f);
}

// ---------------------------------------------------------------------------
// MARK: Emulator hosting
// ---------------------------------------------------------------------------

/// @brief A divider that gates control-rate updates within step().
struct ControlRateDivider {
 private:
    /// the number of frames until the next control-rate update
    unsigned counter = 0;

 public:
    /// @brief Return true when this frame should run a control-rate update.
    /// @details
    /// Returns true on the first call and every CV_DIVISION calls thereafter.
    ///
    inline bool process() {
        if (counter) {
            counter--;
            return false;
        }
        counter = CV_DIVISION - 1;
        return true;
    }
};

/// @brief A chip emulator rendering through one BLIPBuffer per oscillator.
/// @tparam ChipEmulator the class of the chip emulator to host
/// @details
/// Stands in for the Rack build's ChipModule base class. The disting NT has no
/// polyphony to serve, so a single emulator instance replaces the Rack build's
/// array of one emulator per polyphonic channel.
///
template<typename ChipEmulator>
struct ChipVoice {
    /// the BLIP buffers to render audio samples from
    BLIPBuffer buffers[ChipEmulator::OSC_COUNT];
    /// the chip emulator to synthesize sound with
    ChipEmulator apu;

 private:
    /// the divider that gates the control-rate register updates
    ControlRateDivider cvDivider;

 public:
    /// @brief Initialize a new chip voice.
    ///
    /// @param volume the volume level to set the emulator to
    /// @param args arguments to forward to the emulator's constructor
    /// @details
    /// Most of the emulators are default constructible. The POKEY takes a
    /// pointer to the polynomial tables it shares between its oscillators,
    /// which the algorithm allocates from its own memory rather than letting
    /// the emulator reach for the heap.
    ///
    template<typename... Args>
    explicit ChipVoice(float volume, Args&&... args)
        // forwarded rather than copied: the POKEY's tables are 17KB
        : apu(static_cast<Args&&>(args)...) {
        for (unsigned osc = 0; osc < ChipEmulator::OSC_COUNT; osc++) {
            apu.set_output(osc, &buffers[osc]);
            buffers[osc].set_sample_rate(NT_globals.sampleRate, CLOCK_RATE);
        }
        apu.set_volume(volume);
        apu.reset();
    }

    /// @brief Return the number of chip cycles that elapse per output sample.
    static inline int32_t cyclesPerSample() {
        return CLOCK_RATE / NT_globals.sampleRate;
    }

    /// @brief Return true when this frame should run a control-rate update.
    inline bool isControlRate() { return cvDivider.process(); }

    /// @brief Advance the emulator by one output sample.
    ///
    /// @param samples an array of ChipEmulator::OSC_COUNT samples to fill with
    /// the output of each oscillator, in the normalized range [-1, 1]
    ///
    inline void advance(float* samples) {
        apu.end_frame(cyclesPerSample());
        for (unsigned osc = 0; osc < ChipEmulator::OSC_COUNT; osc++)
            samples[osc] = buffers[osc].read_sample();
    }
};

/// @brief One oscillator's output routing for the current step.
struct Output {
    /// the frames of the assigned bus, or NULL when the output is unrouted
    float* bus;
    /// true to overwrite the bus, false to add to it
    bool replace;
};

/// @brief Write one frame of oscillator samples to their output busses.
///
/// @param samples the per-oscillator samples filled in by ChipVoice::advance()
/// @param outputs the per-oscillator output routing
/// @param count the number of oscillators
/// @param frame the index of the frame to write
/// @param normal true to sum unrouted oscillators into the next routed output
/// @param hardClip true to clip each output to the range [-1, 1]
/// @details
/// `normal` mirrors the Rack build's output normalling, where an oscillator
/// whose output port is unpatched is mixed into the next patched port instead
/// of being discarded.
///
static inline void writeFrame(
    const float* samples,
    const Output* outputs,
    unsigned count,
    unsigned frame,
    bool normal = false,
    bool hardClip = true
) {
    float carry = 0.f;
    for (unsigned osc = 0; osc < count; osc++) {
        float value = samples[osc] + carry;
        if (!outputs[osc].bus) {  // unrouted: carry forward or discard
            carry = normal ? value : 0.f;
            continue;
        }
        carry = 0.f;
        if (hardClip) value = Math::clip(value, -1.f, 1.f);
        write(
            outputs[osc].bus,
            frame,
            Math::Eurorack::toAC(value),
            outputs[osc].replace
        );
    }
}

}  // namespace NTPotatoChips

#endif  // NT_POTATOCHIPS_CHIP_HPP_
