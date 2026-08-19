// A General Instrument AY-3-8910 chip algorithm for the disting NT.
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

#include "nt_potatochips/chip.hpp"
#include "dsp/general_instrument_ay_3_8910.hpp"
#include "dsp/trigger/threshold.hpp"

using NTPotatoChips::ChipVoice;
using NTPotatoChips::Output;

/// the chip emulator this algorithm hosts
typedef GeneralInstrumentAy_3_8910 Chip;

/// the number of oscillators on the chip
static constexpr unsigned OSC_COUNT = Chip::OSC_COUNT;

/// the volume level to render the chip at
static constexpr float VOLUME = 2.5f;

// ---------------------------------------------------------------------------
// MARK: Parameters
// ---------------------------------------------------------------------------

/// the parameters of one pulse oscillator, in the order they are declared
enum {
    kPulseCoarse,
    kPulseFine,
    kPulseVOct,
    kPulseFm,
    kPulseFmInput,
    kPulseLevel,
    kPulseLevelInput,
    kPulseSync,
    kPulseTone,
    kPulseNoise,
    kPulseEnvelopeOn,
    kNumPulseParams
};

/// @brief Return the index of a parameter within an oscillator's block.
///
/// @param osc the index of the oscillator
/// @param param the index of the parameter within the oscillator's block
/// @returns the index of the parameter within the algorithm
///
static constexpr uint8_t pulse(unsigned osc, unsigned param) {
    return osc * kNumPulseParams + param;
}

/// the parameters that follow the three pulse oscillator blocks
enum {
    kParamEnvelopeCoarse = OSC_COUNT * kNumPulseParams,
    kParamEnvelopeFine,
    kParamEnvelopeVOct,
    kParamEnvelopeSync,
    kParamEnvelopeMode,
    kParamNoisePeriod,
    kParamNoisePeriodInput,
    /// the first of OSC_COUNT (bus, mode) output parameter pairs
    kParamOutput,
    kNumParameters = kParamOutput + 2 * OSC_COUNT
};

static char const * const enumStringsOffOn[] = { "Off", "On" };

static char const * const enumStringsEnvelopeMode[] = {
    "Attack",
    "Decay",
    "Attack hold",
    "Decay hold",
    "Ramp up",
    "Ramp down",
    "Triangle up",
    "Triangle down",
};

/// @brief Declare the eleven parameters of one pulse oscillator.
/// @param NAME the display prefix for the oscillator, e.g., "A"
#define PULSE_PARAMETERS( NAME ) \
    { .name = NAME " coarse", .min = -60, .max = 60, .def = 0, .unit = kNT_unitSemitones, .scaling = 0, .enumStrings = NULL }, \
    { .name = NAME " fine", .min = -100, .max = 100, .def = 0, .unit = kNT_unitCents, .scaling = 0, .enumStrings = NULL }, \
    NT_PARAMETER_CV_INPUT( NAME " V/Oct", 0, 0 ) \
    { .name = NAME " FM", .min = -100, .max = 100, .def = 0, .unit = kNT_unitNone, .scaling = kNT_scaling100, .enumStrings = NULL }, \
    NT_PARAMETER_CV_INPUT( NAME " FM input", 0, 0 ) \
    { .name = NAME " level", .min = 0, .max = 15, .def = 10, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL }, \
    NT_PARAMETER_CV_INPUT( NAME " level input", 0, 0 ) \
    NT_PARAMETER_CV_INPUT( NAME " sync", 0, 0 ) \
    { .name = NAME " tone", .min = 0, .max = 1, .def = 1, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = enumStringsOffOn }, \
    { .name = NAME " noise", .min = 0, .max = 1, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = enumStringsOffOn }, \
    { .name = NAME " envelope", .min = 0, .max = 1, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = enumStringsOffOn },

static const _NT_parameter parameters[] = {
    PULSE_PARAMETERS( "A" )
    PULSE_PARAMETERS( "B" )
    PULSE_PARAMETERS( "C" )
    // the envelope generator runs from 1Hz rather than C4, so its range is
    // stated relative to 1Hz: the Rack build's knob spans [-5.5, 9] octaves
    { .name = "Env coarse", .min = -66, .max = 108, .def = 21, .unit = kNT_unitSemitones, .scaling = 0, .enumStrings = NULL },
    { .name = "Env fine", .min = -100, .max = 100, .def = 0, .unit = kNT_unitCents, .scaling = 0, .enumStrings = NULL },
    NT_PARAMETER_CV_INPUT( "Env V/Oct", 0, 0 )
    NT_PARAMETER_CV_INPUT( "Env sync", 0, 0 )
    { .name = "Env mode", .min = 0, .max = 7, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = enumStringsEnvelopeMode },
    { .name = "Noise period", .min = 0, .max = 31, .def = 0, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL },
    NT_PARAMETER_CV_INPUT( "Noise period input", 0, 0 )
    NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE( "A output", 0, 13 )
    NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE( "B output", 0, 14 )
    NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE( "C output", 0, 15 )
};

static_assert(
    ARRAY_SIZE(parameters) == kNumParameters,
    "the parameter table and the parameter enum disagree"
);

static const uint8_t pageA[] = {
    pulse(0, kPulseCoarse), pulse(0, kPulseFine), pulse(0, kPulseVOct),
    pulse(0, kPulseFm), pulse(0, kPulseFmInput),
    pulse(0, kPulseLevel), pulse(0, kPulseLevelInput),
    pulse(0, kPulseSync),
    pulse(0, kPulseTone), pulse(0, kPulseNoise), pulse(0, kPulseEnvelopeOn),
};

static const uint8_t pageB[] = {
    pulse(1, kPulseCoarse), pulse(1, kPulseFine), pulse(1, kPulseVOct),
    pulse(1, kPulseFm), pulse(1, kPulseFmInput),
    pulse(1, kPulseLevel), pulse(1, kPulseLevelInput),
    pulse(1, kPulseSync),
    pulse(1, kPulseTone), pulse(1, kPulseNoise), pulse(1, kPulseEnvelopeOn),
};

static const uint8_t pageC[] = {
    pulse(2, kPulseCoarse), pulse(2, kPulseFine), pulse(2, kPulseVOct),
    pulse(2, kPulseFm), pulse(2, kPulseFmInput),
    pulse(2, kPulseLevel), pulse(2, kPulseLevelInput),
    pulse(2, kPulseSync),
    pulse(2, kPulseTone), pulse(2, kPulseNoise), pulse(2, kPulseEnvelopeOn),
};

static const uint8_t pageEnvelope[] = {
    kParamEnvelopeCoarse, kParamEnvelopeFine, kParamEnvelopeVOct,
    kParamEnvelopeSync, kParamEnvelopeMode,
};

static const uint8_t pageNoise[] = {
    kParamNoisePeriod, kParamNoisePeriodInput,
};

static const uint8_t pageRouting[] = {
    kParamOutput + 0, kParamOutput + 1,
    kParamOutput + 2, kParamOutput + 3,
    kParamOutput + 4, kParamOutput + 5,
};

static const _NT_parameterPage pages[] = {
    { .name = "Pulse A", .numParams = ARRAY_SIZE(pageA), .params = pageA },
    { .name = "Pulse B", .numParams = ARRAY_SIZE(pageB), .params = pageB },
    { .name = "Pulse C", .numParams = ARRAY_SIZE(pageC), .params = pageC },
    { .name = "Envelope", .numParams = ARRAY_SIZE(pageEnvelope), .params = pageEnvelope },
    { .name = "Noise", .numParams = ARRAY_SIZE(pageNoise), .params = pageNoise },
    { .name = "Routing", .numParams = ARRAY_SIZE(pageRouting), .params = pageRouting },
};

static const _NT_parameterPages parameterPages = {
    .numPages = ARRAY_SIZE(pages),
    .pages = pages,
};

// ---------------------------------------------------------------------------
// MARK: Algorithm
// ---------------------------------------------------------------------------

/// @brief The state of a Jairasullator instance.
struct _jairasullatorAlgorithm : public _NT_algorithm {
    /// @brief Initialize a new instance.
    _jairasullatorAlgorithm() : voice(VOLUME) { }

    /// the chip emulator and its BLIP buffers
    ChipVoice<Chip> voice;

    /// triggers for the per-oscillator sync inputs and the envelope sync input
    Trigger::Threshold syncTriggers[OSC_COUNT + 1];
};

/// @brief Return the 12-bit frequency register value for an oscillator.
///
/// @param self the algorithm to read parameters from
/// @param osc the index of the oscillator
/// @param voct the pitch CV in volts
/// @param fm the frequency modulation CV in volts
/// @returns the 12-bit frequency in a 16-bit container
///
static inline uint16_t getFrequency(
    const _jairasullatorAlgorithm* self,
    unsigned osc,
    float voct,
    float fm
) {
    // the minimal value for the frequency register to produce sound
    static constexpr float FREQ12BIT_MIN = 2;
    // the maximal value for the frequency register
    static constexpr float FREQ12BIT_MAX = 4095;
    // the clock division of the oscillator relative to the CPU
    static constexpr float CLOCK_DIVISION = 2 * 16;
    const int16_t* v = self->v;
    float octaves = NTPotatoChips::pitch(
        v[pulse(osc, kPulseCoarse)], v[pulse(osc, kPulseFine)]
    );
    octaves += voct;
    octaves += (v[pulse(osc, kPulseFm)] / 100.f) * fm / 5.f;
    const float freq = NTPotatoChips::frequency(octaves);
    return Math::clip(
        NTPotatoChips::CLOCK_RATE / (CLOCK_DIVISION * freq),
        FREQ12BIT_MIN,
        FREQ12BIT_MAX
    );
}

/// @brief Return the 4-bit level register value for an oscillator.
///
/// @param self the algorithm to read parameters from
/// @param osc the index of the oscillator
/// @param voct the pitch CV in volts
/// @param fm the frequency modulation CV in volts
/// @param level the level CV in volts
/// @returns the 4-bit level value in an 8-bit container
/// @details
/// When both the tone and noise of an oscillator are disabled its level
/// register acts as a DAC. In that mode the pitch parameter and CV bias the
/// level CV and the FM parameter and CV scale it, as in the Rack build.
///
static inline uint8_t getLevel(
    const _jairasullatorAlgorithm* self,
    unsigned osc,
    float voct,
    float fm,
    float level
) {
    // the maximal value for the level register
    static constexpr float MAX = 15;
    const int16_t* v = self->v;
    if (self->voice.apu.is_dac_enabled(osc)) {
        float offset = NTPotatoChips::rescale(
            NTPotatoChips::pitch(
                v[pulse(osc, kPulseCoarse)], v[pulse(osc, kPulseFine)]
            ),
            -5.f, 5.f, 0.f, 5.f
        );
        offset += voct / 2.f;
        float scale = NTPotatoChips::rescale(
            v[pulse(osc, kPulseFm)] / 100.f, -1.f, 1.f, 0.f, 2.f
        );
        scale += -1.f + fm / 5.f;
        level = scale * (offset + level);
    }
    const float value = roundf(
        v[pulse(osc, kPulseLevel)] * Math::Eurorack::fromDC(level)
    );
    return Math::clip(value, 0.f, MAX);
}

/// @brief Return the mixer byte enabling the tone and noise of each oscillator.
///
/// @param self the algorithm to read parameters from
/// @returns the 6-bit mixer byte, where a set bit disables a generator
///
static inline uint8_t getChannelEnables(const _jairasullatorAlgorithm* self) {
    uint8_t mixerByte = 0;
    for (unsigned osc = 0; osc < OSC_COUNT; osc++) {
        // the tone flags occupy bits [0, 3) and the noise flags bits [3, 6)
        mixerByte |= !self->v[pulse(osc, kPulseTone)] << osc;
        mixerByte |= !self->v[pulse(osc, kPulseNoise)] << (OSC_COUNT + osc);
    }
    return mixerByte;
}

/// @brief Return the period register value for the noise generator.
///
/// @param self the algorithm to read parameters from
/// @param cv the noise period CV in volts
/// @returns the 5-bit period for the noise oscillator
///
static inline uint8_t getNoisePeriod(
    const _jairasullatorAlgorithm* self,
    float cv
) {
    // the maximal value for the period register
    static constexpr float MAX = 31;
    // scale such that [0, 7]V controls the full range of the parameter
    const float mod = NTPotatoChips::rescale(cv, 0.f, 7.f, 0.f, MAX);
    const float param = self->v[kParamNoisePeriod];
    // invert the parameter so that larger values have higher frequencies
    return MAX - Math::clip(floorf(param + mod), 0.f, MAX);
}

/// @brief Return the 16-bit period register value for the envelope generator.
///
/// @param self the algorithm to read parameters from
/// @param voct the envelope pitch CV in volts
/// @returns the 16-bit envelope period
///
static inline uint16_t getEnvelopePeriod(
    const _jairasullatorAlgorithm* self,
    float voct
) {
    // the minimal value for the period register to produce sound
    static constexpr float FREQ16BIT_MIN = 1;
    // the maximal value for the period register
    static constexpr float FREQ16BIT_MAX = 0xffff;
    // the clock division of the envelope generator relative to the CPU
    static constexpr float CLOCK_DIVISION = 2 * 256;
    const float octaves = NTPotatoChips::pitch(
        self->v[kParamEnvelopeCoarse], self->v[kParamEnvelopeFine]
    ) + voct;
    const float freq = NTPotatoChips::frequencyFrom1Hz(octaves);
    return Math::clip(
        NTPotatoChips::CLOCK_RATE / (CLOCK_DIVISION * freq),
        FREQ16BIT_MIN,
        FREQ16BIT_MAX
    );
}

/// @brief Return the envelope shape register value.
///
/// @param self the algorithm to read parameters from
/// @returns the 4-bit envelope mode
///
static inline uint8_t getEnvelopeMode(const _jairasullatorAlgorithm* self) {
    // map the eight shapes offered by the UI onto the chip's mode register
    // Bit 4: Continue, Bit 3: Attack, Bit 2: Alternate, Bit 1: Hold
    static constexpr uint8_t ENV_MODE_MAP[8] = {
        0b1111,  // attack, then hold low
        0b1001,  // decay, then hold low
        0b1101,  // attack, then hold high
        0b1011,  // decay, then hold high
        0b1100,  // repeated attack
        0b1000,  // repeated decay
        0b1110,  // attack and decay, alternating
        0b1010   // decay and attack, alternating
    };
    return ENV_MODE_MAP[self->v[kParamEnvelopeMode] & 0x7];
}

void calculateRequirements(
    _NT_algorithmRequirements& req,
    const int32_t* specifications
) {
    req.numParameters = ARRAY_SIZE(parameters);
    req.sram = sizeof(_jairasullatorAlgorithm);
    req.dram = 0;
    req.dtc = 0;
    req.itc = 0;
}

_NT_algorithm* construct(
    const _NT_algorithmMemoryPtrs& ptrs,
    const _NT_algorithmRequirements& req,
    const int32_t* specifications
) {
    _jairasullatorAlgorithm* alg = new (ptrs.sram) _jairasullatorAlgorithm();
    alg->parameters = parameters;
    alg->parameterPages = &parameterPages;
    return alg;
}

void step(_NT_algorithm* self, float* busFrames, int numFramesBy4) {
    _jairasullatorAlgorithm* pThis = (_jairasullatorAlgorithm*)self;
    const int16_t* v = pThis->v;
    const unsigned numFrames = numFramesBy4 * 4;

    // resolve the busses once per step rather than once per frame
    const float* voct[OSC_COUNT];
    const float* fm[OSC_COUNT];
    const float* level[OSC_COUNT];
    const float* sync[OSC_COUNT];
    Output outputs[OSC_COUNT];
    for (unsigned osc = 0; osc < OSC_COUNT; osc++) {
        voct[osc] = NTPotatoChips::inputBus(busFrames, numFrames, v[pulse(osc, kPulseVOct)]);
        fm[osc] = NTPotatoChips::inputBus(busFrames, numFrames, v[pulse(osc, kPulseFmInput)]);
        level[osc] = NTPotatoChips::inputBus(busFrames, numFrames, v[pulse(osc, kPulseLevelInput)]);
        sync[osc] = NTPotatoChips::inputBus(busFrames, numFrames, v[pulse(osc, kPulseSync)]);
        outputs[osc].bus = NTPotatoChips::outputBus(busFrames, numFrames, v[kParamOutput + 2 * osc]);
        outputs[osc].replace = v[kParamOutput + 2 * osc + 1];
    }
    const float* envelopeVOct = NTPotatoChips::inputBus(busFrames, numFrames, v[kParamEnvelopeVOct]);
    const float* envelopeSync = NTPotatoChips::inputBus(busFrames, numFrames, v[kParamEnvelopeSync]);
    const float* noisePeriod = NTPotatoChips::inputBus(busFrames, numFrames, v[kParamNoisePeriodInput]);

    float samples[OSC_COUNT];
    for (unsigned frame = 0; frame < numFrames; frame++) {
        // audio rate: pitch, level and hard sync
        for (unsigned osc = 0; osc < OSC_COUNT; osc++) {
            if (pThis->syncTriggers[osc].process(
                    NTPotatoChips::triggerSignal(NTPotatoChips::voltage(sync[osc], frame, 0.f))))
                pThis->voice.apu.reset_phase(osc);
            const float pitchCV = NTPotatoChips::voltage(voct[osc], frame, 0.f);
            // the FM input normals to 5V so that the attenuverter alone can
            // offset the pitch, as it does in the Rack build
            const float fmCV = NTPotatoChips::voltage(fm[osc], frame, 5.f);
            // set the frequency before the level; in DAC mode the level
            // depends on the pitch and FM controls
            pThis->voice.apu.set_frequency(osc, getFrequency(pThis, osc, pitchCV, fmCV));
            // the level input normals to 10V, i.e., unity
            const float levelCV = NTPotatoChips::voltage(level[osc], frame, 10.f);
            pThis->voice.apu.set_voice_volume(
                osc,
                getLevel(pThis, osc, pitchCV, fmCV, levelCV),
                v[pulse(osc, kPulseEnvelopeOn)]
            );
        }
        if (pThis->syncTriggers[OSC_COUNT].process(
                NTPotatoChips::triggerSignal(NTPotatoChips::voltage(envelopeSync, frame, 0.f))))
            pThis->voice.apu.reset_envelope_phase();

        // control rate: the mixer, noise and envelope registers
        if (pThis->voice.isControlRate()) {
            pThis->voice.apu.set_channel_enables(getChannelEnables(pThis));
            pThis->voice.apu.set_envelope_mode(getEnvelopeMode(pThis));
            pThis->voice.apu.set_noise_period(
                getNoisePeriod(pThis, NTPotatoChips::voltage(noisePeriod, frame, 0.f))
            );
            pThis->voice.apu.set_envelope_period(
                getEnvelopePeriod(pThis, NTPotatoChips::voltage(envelopeVOct, frame, 0.f))
            );
        }

        pThis->voice.advance(samples);
        // the Rack build normals unpatched outputs into the following voice
        NTPotatoChips::writeFrame(samples, outputs, OSC_COUNT, frame, true);
    }
}

static const _NT_factory factory = {
    .guid = NT_MULTICHAR( 'P', 'C', 'a', 'y' ),
    .name = "Jairasullator",
    .description = "General Instrument AY-3-8910 (MSX, ZX Spectrum)",
    .numSpecifications = 0,
    .specifications = NULL,
    .calculateStaticRequirements = NULL,
    .initialise = NULL,
    .calculateRequirements = calculateRequirements,
    .construct = construct,
    .parameterChanged = NULL,
    .step = step,
    .draw = NULL,
    .midiRealtime = NULL,
    .midiMessage = NULL,
    .tags = kNT_tagInstrument,
};

uintptr_t pluginEntry(_NT_selector selector, uint32_t data) {
    switch (selector) {
    case kNT_selector_version:
        return kNT_apiVersionCurrent;
    case kNT_selector_numFactories:
        return 1;
    case kNT_selector_factoryInfo:
        return (uintptr_t)((data == 0) ? &factory : NULL);
    }
    return 0;
}
