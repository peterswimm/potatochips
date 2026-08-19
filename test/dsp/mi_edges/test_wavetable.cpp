// Test cases for the MutableIntstrumentsEdges::DigitalOscillator structure.
//
// Copyright (c) 2020 Christian Kauten
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//

// `dsp/mi_edges/wavetable.hpp` references `rack::dsp::FREQ_C4`, which is the
// only piece of VCV Rack the DSP layer touches. Stand it in rather than carry
// a Rack dependency into the test, as `nt_potatochips/compat.hpp` does for the
// disting NT.
namespace rack {
namespace dsp {
/// the frequency of the note C4 in Hz
static constexpr float FREQ_C4 = 261.6256f;
}  // namespace dsp
}  // namespace rack

#include "dsp/mi_edges/wavetable.hpp"
#define CATCH_CONFIG_MAIN
#include "catch.hpp"

using Oscillator::MutableIntstrumentsEdges::DigitalOscillator;
using Oscillator::MutableIntstrumentsEdges::NUM_SHAPES;
using Oscillator::MutableIntstrumentsEdges::pulse_widths;
using Oscillator::MutableIntstrumentsEdges::PULSE_WIDTH_MIN;
using Oscillator::MutableIntstrumentsEdges::PULSE_WIDTH_MAX;

/// the sample time to render test signals at, i.e., 44.1kHz
static constexpr float SAMPLE_TIME = 1.f / 44100.f;

/// @brief Render an oscillator and return the mean of its output.
///
/// @param oscillator the oscillator to render
/// @param samples the number of samples to render
/// @returns the mean of the rendered samples
///
static float mean(DigitalOscillator& oscillator, unsigned samples) {
    double total = 0.0;
    for (unsigned i = 0; i < samples; i++) {
        oscillator.process(SAMPLE_TIME);
        total += oscillator.getValue();
    }
    return total / samples;
}

// ---------------------------------------------------------------------------
// MARK: shapes
// ---------------------------------------------------------------------------

SCENARIO("the shapes of the oscillator are enumerated") {
    GIVEN("the shape enumeration") {
        THEN("the shapes of the digital oscillator keep their indexes") {
            // patches store the index of the shape, so these may not move
            REQUIRE(0 == static_cast<int>(DigitalOscillator::Shape::Sine));
            REQUIRE(1 == static_cast<int>(DigitalOscillator::Shape::Triangle));
            REQUIRE(2 == static_cast<int>(DigitalOscillator::Shape::NES_Triangle));
            REQUIRE(3 == static_cast<int>(DigitalOscillator::Shape::SampleHold));
            REQUIRE(4 == static_cast<int>(DigitalOscillator::Shape::LFSR_Long));
            REQUIRE(5 == static_cast<int>(DigitalOscillator::Shape::LFSR_Short));
        }
        THEN("the pulse shapes of the timer oscillators follow them") {
            REQUIRE(6 == static_cast<int>(DigitalOscillator::Shape::Pulse50));
            REQUIRE(11 == static_cast<int>(DigitalOscillator::Shape::PulseCV));
            REQUIRE(12 == NUM_SHAPES);
        }
    }
    GIVEN("an initialized oscillator") {
        DigitalOscillator oscillator;
        WHEN("the shape is cycled through the whole enumeration") {
            THEN("every shape is visited and the cycle returns to the start") {
                for (unsigned i = 0; i < NUM_SHAPES; i++) {
                    REQUIRE(i == static_cast<unsigned>(oscillator.getShape()));
                    oscillator.cycleShape();
                }
                REQUIRE(DigitalOscillator::Shape::Sine == oscillator.getShape());
            }
        }
        WHEN("a pulse shape is selected") {
            oscillator.setShape(DigitalOscillator::Shape::Pulse50);
            THEN("the width does not come from a CV") {
                REQUIRE_FALSE(oscillator.isPulseWidthCV());
            }
        }
        WHEN("the CV controlled pulse shape is selected") {
            oscillator.setShape(DigitalOscillator::Shape::PulseCV);
            THEN("the width comes from a CV") {
                REQUIRE(oscillator.isPulseWidthCV());
            }
        }
    }
}

// ---------------------------------------------------------------------------
// MARK: pulse rendering
// ---------------------------------------------------------------------------

SCENARIO("the oscillator renders pulse waves") {
    GIVEN("an oscillator rendering each fixed pulse width at 100Hz") {
        THEN("the duty cycle of the pulse matches the hardware's table") {
            for (unsigned i = 0; i < 5; i++) {
                DigitalOscillator oscillator;
                oscillator.setShape(static_cast<DigitalOscillator::Shape>(
                    static_cast<int>(DigitalOscillator::Shape::Pulse50) + i
                ));
                oscillator.setFrequency(100.f);
                // the mean of a pulse of amplitude 1 is 2 * duty - 1
                const float duty = pulse_widths[i] / 256.f;
                REQUIRE(2.f * duty - 1.f == Approx(mean(oscillator, 44100)).margin(0.01));
            }
        }
    }
    GIVEN("an oscillator rendering the CV controlled pulse at 100Hz") {
        DigitalOscillator oscillator;
        oscillator.setShape(DigitalOscillator::Shape::PulseCV);
        oscillator.setFrequency(100.f);
        WHEN("the width is set to the middle of its range") {
            oscillator.setPulseWidth(128);
            THEN("the pulse is a square wave") {
                REQUIRE(0.f == Approx(mean(oscillator, 44100)).margin(0.01));
            }
        }
        WHEN("the width is set below the range the hardware allows") {
            oscillator.setPulseWidth(0);
            THEN("the width is clamped rather than silenced") {
                const float duty = PULSE_WIDTH_MIN / 256.f;
                REQUIRE(2.f * duty - 1.f == Approx(mean(oscillator, 44100)).margin(0.01));
            }
        }
        WHEN("the width is set above the range the hardware allows") {
            oscillator.setPulseWidth(255);
            THEN("the width is clamped rather than held high") {
                const float duty = PULSE_WIDTH_MAX / 256.f;
                REQUIRE(2.f * duty - 1.f == Approx(mean(oscillator, 44100)).margin(0.01));
            }
        }
    }
}

// ---------------------------------------------------------------------------
// MARK: band-limiting
// ---------------------------------------------------------------------------

/// @brief Return a sample of a band-limited pulse by additive synthesis.
///
/// @param phase the phase of the pulse in \f$[0, 1)\f$
/// @param duty the duty cycle of the pulse in \f$[0, 1)\f$
/// @param harmonics the number of harmonics to sum, i.e., those below Nyquist
/// @returns the sample of an alias-free pulse with the given duty cycle
///
static double bandlimitedPulse(double phase, double duty, unsigned harmonics) {
    double value = 2 * duty - 1;
    for (unsigned k = 1; k <= harmonics; k++) {
        const double a = 2 * sin(2 * M_PI * k * duty) / (k * M_PI);
        const double b = 2 * (1 - cos(2 * M_PI * k * duty)) / (k * M_PI);
        value += a * cos(2 * M_PI * k * phase) + b * sin(2 * M_PI * k * phase);
    }
    return value;
}

SCENARIO("the pulse waves of the oscillator are band-limited") {
    GIVEN("an oscillator rendering each fixed pulse width at 440Hz") {
        static constexpr float FREQUENCY = 440.f;
        static constexpr unsigned SAMPLES = 8192;
        // the number of harmonics of the pulse that fall below Nyquist
        const unsigned harmonics = (1.f / SAMPLE_TIME) / (2 * FREQUENCY);
        THEN("the pulse is closer to an alias-free pulse than an uncorrected one") {
            for (unsigned i = 0; i < 5; i++) {
                DigitalOscillator oscillator;
                oscillator.setShape(static_cast<DigitalOscillator::Shape>(
                    static_cast<int>(DigitalOscillator::Shape::Pulse50) + i
                ));
                oscillator.setFrequency(FREQUENCY);
                const double duty = pulse_widths[i] / 256.0;
                const double deltaPhase = FREQUENCY * SAMPLE_TIME;
                double rendered = 0.0;
                double uncorrected = 0.0;
                for (unsigned sample = 0; sample < SAMPLES; sample++) {
                    // the oscillator advances its phase before it renders
                    const double phase = fmod((sample + 1) * deltaPhase, 1.0);
                    const double reference = bandlimitedPulse(phase, duty, harmonics);
                    oscillator.process(SAMPLE_TIME);
                    const double error = oscillator.getValue() - reference;
                    rendered += error * error;
                    // the same pulse with no correction at its edges
                    const double naive = (phase < duty ? 1.0 : -1.0) - reference;
                    uncorrected += naive * naive;
                }
                // the correction cuts the error by more than a third, and the
                // ratio inverts if it is ever applied the wrong way round
                REQUIRE(rendered < 0.7 * uncorrected);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// MARK: output range
// ---------------------------------------------------------------------------

SCENARIO("the oscillator is rendered across its range of pitches") {
    GIVEN("an oscillator and the pitches of a five octave sweep") {
        static const float FREQUENCIES[] = {
            0.f, 1.f, 20.f, 100.f, 440.f, 1000.f, 8000.f, 20000.f, 40000.f
        };
        THEN("every shape stays within the output range of the module") {
            for (unsigned shape = 0; shape < NUM_SHAPES; shape++) {
                for (const float& frequency : FREQUENCIES) {
                    DigitalOscillator oscillator;
                    oscillator.setShape(static_cast<DigitalOscillator::Shape>(shape));
                    oscillator.setFrequency(frequency);
                    for (unsigned i = 0; i < 4410; i++) {
                        oscillator.process(SAMPLE_TIME);
                        const float value = oscillator.getValue();
                        REQUIRE(std::isfinite(value));
                        REQUIRE(value >= -1.f);
                        REQUIRE(value <= 1.f);
                    }
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// MARK: gate
// ---------------------------------------------------------------------------

SCENARIO("the gate of the oscillator is closed") {
    GIVEN("an oscillator with a closed gate") {
        DigitalOscillator oscillator;
        oscillator.setFrequency(100.f);
        oscillator.gateOpen = false;
        WHEN("the oscillator is rendered") {
            oscillator.process(SAMPLE_TIME);
            THEN("the output is silent, i.e., mid-scale as on the hardware") {
                REQUIRE(0.f == Approx(oscillator.getValue()).margin(1e-3));
            }
        }
    }
}
