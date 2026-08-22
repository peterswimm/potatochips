// A digital oscillator that generates different waveforms.
// Copyright 2020 Christian Kauten
// Copyright 2015 Emilie Gillet (emilie.o.gillet@gmail.com)
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

#ifndef DSP_OSCILLATORS_MI_EDGES_WAVETABLE_HPP
#define DSP_OSCILLATORS_MI_EDGES_WAVETABLE_HPP

#include <algorithm>
#include <cstdint>
#include <limits>
#include "../math.hpp"
#include "wavetables.hpp"

/// @brief Structures for generating oscillating signals.
namespace Oscillator {

/// @brief Oscillator code from the Mutable Instruments _Edges_ module.
namespace MutableIntstrumentsEdges {

/// @brief The 8-bit pulse widths of the module's timer oscillators.
/// @details
/// These are the `pulse_widths` table from `timer_oscillator.cc`, less the
/// last entry, which the hardware ignores in favor of a CV. The panel labels
/// them _50%_, _66%_, _75%_, _87%_, and _95%_, whereas the value here is the
/// complementary duty cycle the timer produces; the two sound the same because
/// a pulse and its complement differ only in sign.
const uint8_t pulse_widths[] = {128, 85, 64, 32, 13};

/// the smallest CV controlled pulse width, i.e., `TimerOscillator::set_cv_pw()`
const uint8_t PULSE_WIDTH_MIN = 6;

/// the largest CV controlled pulse width, i.e., `TimerOscillator::set_cv_pw()`
const uint8_t PULSE_WIDTH_MAX = 250;

/// @brief Return the PolyBLEP correction for a step in a waveform.
///
/// @param phase the phase of the oscillator relative to the step in
/// \f$[0, 1)\f$
/// @param deltaPhase the amount the phase advances each sample
/// @returns the correction to add to a rising step of amplitude 2, i.e., the
/// amount to subtract from a falling one
/// @details
/// The pulse channels of the hardware come from a timer that toggles an output
/// pin, so their edges land between samples and carry no aliasing of their own.
/// Comparing a sampled phase against the pulse width would instead quantize
/// every edge onto a sample boundary, which aliases badly, so the samples
/// either side of an edge are corrected by the integral of a band-limited step.
///
inline float polyBlep(float phase, const float& deltaPhase) {
    if (phase < deltaPhase) {  // the sample after the step
        phase = phase / deltaPhase;
        return phase + phase - phase * phase - 1.f;
    } else if (phase > 1.f - deltaPhase) {  // the sample before the step
        phase = (phase - 1.f) / deltaPhase;
        return phase * phase + phase + phase + 1.f;
    }
    return 0.f;
}

/// @brief A 48kHz digital oscillator with different shapes
/// @details
/// The available shapes are:
/// 1. Sine,
/// 2. Triangle,
/// 3. Nintendo Entertainment System (NES) Triangle,
/// 4. Sample+Hold (S+H) Noise,
/// 5. Linear Feedback Shift Register (LFSR) Noise Short,
/// 6. Linear Feedback Shift Register (LFSR) Noise Long, and
/// 7. the pulse waves of the module's timer oscillators, with the five duty
///    cycles of the hardware and a width that a CV can control.
///
class DigitalOscillator {
 public:
    /// @brief The wave shapes for the oscillator
    enum class Shape {
        Sine = 0,
        Triangle,
        NES_Triangle,
        SampleHold,
        LFSR_Long,
        LFSR_Short,
        // The pulse shapes of the module's timer oscillators, i.e., channels
        // 1 through 3 of the hardware. They are appended to the list so that
        // the index of every shape above is unchanged, and patches that store
        // a shape index therefore still load.
        Pulse50,
        Pulse66,
        Pulse75,
        Pulse87,
        Pulse95,
        PulseCV,
        Count
    };

 private:
    /// the current shape of the wave produced by the oscillator
    Shape shape = Shape::Sine;

    /// the MIDI note that corresponds to the current pitch
    uint8_t note = 60;

    /// @brief Set the note, clamped to the range the wave-tables cover.
    ///
    /// @param note_ the note to set, which may be out of range or not finite
    /// @details
    /// The pitch can come from an unbounded control voltage, and a frequency
    /// of zero sends the logarithm to negative infinity, so the note is
    /// clamped rather than trusted.
    ///
    inline void setNote(const float& note_) {
        // the lowest note the band-limited wave-tables cover
        const float lowest = 12;
        // renderBandlimitedTriangle() selects a wave-table with the high
        // nibble of `note - lowest`, of which there are eight per family, so
        // notes above lowest + 0x7f would index past the end of the family
        const float highest = lowest + 0x7f;
        note = Math::clip(note_, lowest, highest);
    }
    /// the current frequency of the oscillator
    float freq = rack::dsp::FREQ_C4;
    /// the current phase of the oscillator
    float phase = 0.0;

    /// the current width of the pulse in [0, 1]
    uint8_t pulseWidth = 127;
    /// The auxiliary phase for the sine wave bit crusher
    uint16_t aux_phase = 0;

    /// The random number generator state for generating random noise
    uint16_t rng = 1;
    /// A sample from the sine wave to use for the random noise generators
    uint16_t sample = 0;
    /// The output value from the oscillator (12-bit in 16-bit container)
    uint16_t value = 0;

 public:
    /// whether the gate for the oscillator is open
    bool gateOpen = true;

    /// @brief Reset the oscillator to its default state
    void reset() {
        shape = Shape::Sine;
        note = 60;
        freq = rack::dsp::FREQ_C4;
        phase = 0.0;
        pulseWidth = 127;
        aux_phase = 0;
        rng = 1;
        sample = 0;
        value = 0;
        gateOpen = true;
    }

    /// @brief Set the pitch of the oscillator.
    ///
    /// @param pitch the pitch of the oscillator in units/octave
    ///
    inline void setPitch(const float& pitch) {
        freq = Math::clip(rack::dsp::FREQ_C4 * powf(2.f, pitch), 0.0f, 20000.0f);
        setNote(60 + 12 * pitch);
    }

    /// @brief Return the pitch of the oscillator.
    ///
    /// @returns the pitch of the oscillator in units/octave
    ///
    inline float getPitch() const { return std::log2(freq / rack::dsp::FREQ_C4); }

    /// @brief Set the frequency of the oscillator.
    ///
    /// @param frequency the frequency of the oscillator in Hertz
    ///
    inline void setFrequency(const float& frequency) {
        freq = frequency;
        setNote(60 + 12 * std::log2(frequency / rack::dsp::FREQ_C4));
    }

    /// @brief Return the frequency of the oscillator.
    ///
    /// @returns the frequency of the oscillator in Hertz
    ///
    inline float getFrequency() const { return freq; }

    /// @brief Set the shape of the oscillator.
    ///
    /// @param shape_ the new shape for the oscillator waveform
    ///
    inline void setShape(const Shape& shape_) { shape = shape_; }

    /// @brief Return the current shape of the oscillator.
    ///
    /// @returns the shape of the oscillator waveform
    ///
    inline Shape getShape() const { return shape; }

    /// @brief Return true if the shape takes its pulse width from a CV.
    ///
    /// @returns true if the current shape is the CV controlled pulse
    /// @details
    /// The hardware routes a channel's modulation CV to its pulse width, in
    /// place of its pitch, when the width of the channel is CV controlled.
    ///
    inline bool isPulseWidthCV() const { return shape == Shape::PulseCV; }

    /// @brief Cycle the shape of the oscillator.
    inline void cycleShape() {
        const auto length = static_cast<int>(Shape::Count);
        shape = static_cast<Shape>((static_cast<int>(shape) + 1) % length);
    }

    /// @brief Set the pulse width for the sine bit-crusher.
    ///
    /// @param pulseWidth_ the width of the pulse used for the sine bit-crusher
    ///
    inline void setPulseWidth(const uint8_t& pulseWidth_) {
        pulseWidth = pulseWidth_;
    }

    /// @brief Return the pulse width for the sine bit-crusher.
    ///
    /// @returns the width of the pulse used for the sine bit-crusher
    ///
    inline uint8_t getPulseWidth() {
        return pulseWidth;
    }

    /// @brief Set the value of the shift register.
    ///
    /// @param value the new value for the shift register
    ///
    inline void setLFSR(const uint16_t& seed) { rng = seed; }

    /// @brief Return the current value of the shift register.
    ///
    /// @returns the current value of the shift register
    ///
    inline uint16_t getLFSR() const { return rng; }

    /// @brief Set the sample to a new value.
    ///
    /// @param sample_ the new sample for the oscillator
    ///
    inline void setSample(const uint16_t& sample_) { sample = sample_; }

    /// @brief Return the current sample.
    ///
    /// @returns the current 12-bit sample from the oscillator
    ///
    inline uint16_t getSample() const { return sample; }

    /// @brief Return the value from the oscillator.
    ///
    /// @returns the 12-bit value of the oscillator normalize in \f$[-1, 1]\f$
    ///
    inline float getValue() const {
        // divide the 12-bit value by 4096.0 to normalize in [0.0, 1.0]
        // multiply by 2 and subtract 1 to get the value in [-1.0, 1.0]. A
        // closed gate renders mid-scale, as `RenderSilence()` does on the
        // hardware, so the gate needs no term of its own here.
        return 2 * (value / 4096.f) - 1;
    }

    /// @brief Process a sample from the oscillator.
    ///
    /// @param deltaTime the amount of time between samples
    ///
    void process(const float& deltaTime) {
        if (!gateOpen) {  // render silence, i.e., mid-scale
            value = 2048;
            return;
        }
        // Advance phase counter
        const float deltaPhase = Math::clip(freq * deltaTime, 1e-6f, 0.5f);
        phase += deltaPhase;
        if (phase >= 1.f) phase -= floor(phase);
        // calculate quantized versions of the phase and sample time
        const auto phaseQ = std::numeric_limits<uint16_t>::max() * phase;
        const auto deltaPhaseQ = std::numeric_limits<uint16_t>::max() * deltaPhase;
        switch(shape) {  // render the waveform
        case Shape::Sine:         { return renderSine(phaseQ);                  }
        case Shape::Triangle:     { return renderBandlimitedTriangle(phaseQ);   }
        case Shape::NES_Triangle: { return renderBandlimitedTriangle(phaseQ);   }
        case Shape::SampleHold:   { return renderNoise(phaseQ, deltaPhaseQ);    }
        case Shape::LFSR_Long:    { return renderNoiseNES(phaseQ, deltaPhaseQ); }
        case Shape::LFSR_Short:   { return renderNoiseNES(phaseQ, deltaPhaseQ); }
        case Shape::Pulse50:      // fall through to the pulse renderer, which
        case Shape::Pulse66:      // resolves the width from the shape itself
        case Shape::Pulse75:
        case Shape::Pulse87:
        case Shape::Pulse95:
        case Shape::PulseCV:      { return renderPulse(phase, deltaPhase);       }
        default:                  { return;                                     }
        };
    }

 private:
    /// Render a sine wave from the oscillator.
    void renderSine(const uint16_t& phase) {
        uint16_t aux_phase_increment = bitcrusher_increments[pulseWidth];
        aux_phase = aux_phase + aux_phase_increment;
        if (aux_phase < aux_phase_increment || !aux_phase_increment) {
            sample = interpolate(triangle_6, phase) << 8;
        }
        // set the value to the current sample
        value = sample >> 4;
    }

    /// Render a triangle wave from the oscillator.
    void renderBandlimitedTriangle(const uint16_t& phase) {
        // determine gains for mixing between wave-tables based on MIDI note
        uint8_t balance = ((note - 12) << 4) | ((note - 12) >> 4);
        uint8_t gain_2 = balance & 0xf0;
        uint8_t gain_1 = ~gain_2;
        // determine the base wave-table (NES triangle or regular triangle)
        uint8_t base = shape == Shape::NES_Triangle ? NES_TRIANGLE_0 : TRIANGLE_0;
        // lookup first wave-table
        uint8_t index = balance & 0xf;
        const uint8_t* wave_1 = lookup_table[base + index];
        // lookup second wave-table
        index = std::min<uint8_t>(index + 1, NUM_WAVETABLES);
        const uint8_t* wave_2 = lookup_table[base + index];
        // interpolate the value between the wave-tables
        value = interpolate(wave_1, wave_2, gain_1, gain_2, phase) >> 4;
    }

    /// Render NES noise from the oscillator.
    void renderNoiseNES(const uint16_t& phase, const uint16_t& deltaPhase) {
        if (phase < deltaPhase) {  // sample a new value
            uint8_t tap = shape == Shape::LFSR_Short ? rng >> 6 : rng >> 1;
            uint8_t random_bit = (rng ^ tap) & 1;
            rng >>= 1;
            if (random_bit) {
                rng |= 0x4000;
                sample = 0x0300;
            } else {
                sample = 0x0cff;
            }
        }
        // set the value to the current sample
        value = sample;
    }

    /// @brief Render a band-limited pulse wave from the oscillator.
    ///
    /// @param phase the phase of the oscillator in \f$[0, 1)\f$
    /// @param deltaPhase the amount the phase advances each sample
    /// @details
    /// The pulse keeps the DC offset that a duty cycle away from _50%_ implies,
    /// as the timer's output pin does, and quantizes to the 12 bits the other
    /// shapes render into. At the extremes of pitch and width the pulse may
    /// last less than a sample, in which case it thins out, much as the
    /// hardware's timer runs out of resolution.
    ///
    void renderPulse(const float& phase, const float& deltaPhase) {
        // resolve the duty cycle of the pulse from the shape
        const uint8_t width = shape == Shape::PulseCV ?
            Math::clip(pulseWidth, PULSE_WIDTH_MIN, PULSE_WIDTH_MAX) :
            pulse_widths[static_cast<int>(shape) - static_cast<int>(Shape::Pulse50)];
        const float duty = width / 256.f;
        // the naive pulse, i.e., the output pin of the timer sampled directly
        float pulse = phase < duty ? 1.f : -1.f;
        // correct the rising edge, which is at the wrap-around of the phase
        pulse += polyBlep(phase, deltaPhase);
        // correct the falling edge, which is at the end of the pulse
        float falling = phase - duty;
        if (falling < 0.f) falling += 1.f;
        pulse -= polyBlep(falling, deltaPhase);
        // quantize to 12-bit, where mid-scale is silence
        value = static_cast<uint16_t>(Math::clip(2048.f + 2047.f * pulse, 0.f, 4095.f));
    }

    /// Render and hold noise from the oscillator.
    void renderNoise(const uint16_t& phase, const uint16_t& deltaPhase) {
        if (phase < deltaPhase) {  // sample a new value
            rng = (rng >> 1) ^ (-(rng & 1) & 0xb400);
            sample = rng & 0x0fff;
            sample = 512 + ((sample * 3) >> 2);
        }
        // set the value to the current sample
        value = sample;
    }
};

/// the number of shapes the oscillator can render
const unsigned NUM_SHAPES = static_cast<unsigned>(DigitalOscillator::Shape::Count);

}  // namespace MutableIntstrumentsEdges

}  // namespace Oscillator

#endif  // DSP_OSCILLATORS_MI_EDGES_WAVETABLE_HPP
