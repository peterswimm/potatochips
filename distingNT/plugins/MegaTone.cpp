// A Texas Instruments SN76489 chip algorithm for the disting NT.
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
#include "dsp/texas_instruments_sn76489.hpp"
#include "dsp/trigger/threshold.hpp"

using NTPotatoChips::ChipVoice;
using NTPotatoChips::Output;

/// the chip emulator this algorithm hosts
typedef TexasInstrumentsSN76489 Chip;

/// the number of oscillators on the chip, i.e., three tones and one noise
static constexpr unsigned OSC_COUNT = Chip::OSC_COUNT;

/// the number of tone generators on the chip
static constexpr unsigned TONE_COUNT = Chip::TONE_COUNT;

/// the volume level to render the chip at
static constexpr float VOLUME = 3.f;

// ---------------------------------------------------------------------------
// MARK: Parameters
// ---------------------------------------------------------------------------

/// the parameters of one tone generator, in the order they are declared
enum {
    kToneCoarse,
    kToneFine,
    kToneVOct,
    kToneFm,
    kToneFmInput,
    kToneLevel,
    kToneLevelInput,
    kNumToneParams
};

/// @brief Return the index of a parameter within a tone generator's block.
///
/// @param voice the index of the tone generator
/// @param param the index of the parameter within the generator's block
/// @returns the index of the parameter within the algorithm
///
static constexpr uint8_t tone(unsigned voice, unsigned param) {
    return voice * kNumToneParams + param;
}

/// the parameters that follow the three tone generator blocks
enum {
    kParamNoisePeriod = TONE_COUNT * kNumToneParams,
    kParamNoisePeriodInput,
    kParamLfsr,
    kParamLfsrInput,
    kParamNoiseLevel,
    kParamNoiseLevelInput,
    /// the first of OSC_COUNT (bus, mode) output parameter pairs
    kParamOutput,
    kNumParameters = kParamOutput + 2 * OSC_COUNT
};

static char const * const enumStringsOffOn[] = { "Off", "On" };

/// @brief Declare the seven parameters of one tone generator.
/// @param NAME the display prefix for the generator, e.g., "Tone 1"
#define TONE_PARAMETERS( NAME ) \
    { .name = NAME " coarse", .min = -30, .max = 30, .def = 0, .unit = kNT_unitSemitones, .scaling = 0, .enumStrings = NULL }, \
    { .name = NAME " fine", .min = -100, .max = 100, .def = 0, .unit = kNT_unitCents, .scaling = 0, .enumStrings = NULL }, \
    NT_PARAMETER_CV_INPUT( NAME " V/Oct", 0, 0 ) \
    { .name = NAME " FM", .min = -100, .max = 100, .def = 0, .unit = kNT_unitNone, .scaling = kNT_scaling100, .enumStrings = NULL }, \
    NT_PARAMETER_CV_INPUT( NAME " FM input", 0, 0 ) \
    { .name = NAME " level", .min = 0, .max = 15, .def = 7, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL }, \
    NT_PARAMETER_CV_INPUT( NAME " level input", 0, 0 )

static const _NT_parameter parameters[] = {
    TONE_PARAMETERS( "Tone 1" )
    TONE_PARAMETERS( "Tone 2" )
    TONE_PARAMETERS( "Tone 3" )
    { .name = "Noise period", .min = 0, .max = 3, .def = 0, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL },
    NT_PARAMETER_CV_INPUT( "Noise period input", 0, 0 )
    { .name = "LFSR", .min = 0, .max = 1, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = enumStringsOffOn },
    NT_PARAMETER_CV_INPUT( "LFSR input", 0, 0 )
    { .name = "Noise level", .min = 0, .max = 15, .def = 7, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL },
    NT_PARAMETER_CV_INPUT( "Noise level input", 0, 0 )
    NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE( "Tone 1 output", 0, 13 )
    NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE( "Tone 2 output", 0, 14 )
    NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE( "Tone 3 output", 0, 15 )
    NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE( "Noise output", 0, 16 )
};

static_assert(
    ARRAY_SIZE(parameters) == kNumParameters,
    "the parameter table and the parameter enum disagree"
);

static const uint8_t pageTone1[] = {
    tone(0, kToneCoarse), tone(0, kToneFine), tone(0, kToneVOct),
    tone(0, kToneFm), tone(0, kToneFmInput),
    tone(0, kToneLevel), tone(0, kToneLevelInput),
};

static const uint8_t pageTone2[] = {
    tone(1, kToneCoarse), tone(1, kToneFine), tone(1, kToneVOct),
    tone(1, kToneFm), tone(1, kToneFmInput),
    tone(1, kToneLevel), tone(1, kToneLevelInput),
};

static const uint8_t pageTone3[] = {
    tone(2, kToneCoarse), tone(2, kToneFine), tone(2, kToneVOct),
    tone(2, kToneFm), tone(2, kToneFmInput),
    tone(2, kToneLevel), tone(2, kToneLevelInput),
};

static const uint8_t pageNoise[] = {
    kParamNoisePeriod, kParamNoisePeriodInput,
    kParamLfsr, kParamLfsrInput,
    kParamNoiseLevel, kParamNoiseLevelInput,
};

static const uint8_t pageRouting[] = {
    kParamOutput + 0, kParamOutput + 1,
    kParamOutput + 2, kParamOutput + 3,
    kParamOutput + 4, kParamOutput + 5,
    kParamOutput + 6, kParamOutput + 7,
};

static const _NT_parameterPage pages[] = {
    { .name = "Tone 1", .numParams = ARRAY_SIZE(pageTone1), .params = pageTone1 },
    { .name = "Tone 2", .numParams = ARRAY_SIZE(pageTone2), .params = pageTone2 },
    { .name = "Tone 3", .numParams = ARRAY_SIZE(pageTone3), .params = pageTone3 },
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

/// @brief The state of a Mega Tone instance.
struct _megaToneAlgorithm : public _NT_algorithm {
    /// @brief Initialize a new instance.
    _megaToneAlgorithm() : voice(VOLUME) { }

    /// the chip emulator and its BLIP buffers
    ChipVoice<Chip> voice;

    /// a trigger for handling inputs to the LFSR input
    Trigger::Threshold lfsr;
};

/// @brief Return the 10-bit frequency register value for a tone generator.
///
/// @param self the algorithm to read parameters from
/// @param voice the index of the tone generator
/// @param voct the pitch CV in volts
/// @param fm the frequency modulation CV in volts
/// @returns the 10-bit frequency in a 16-bit container
///
static inline uint16_t getFrequency(
    const _megaToneAlgorithm* self,
    unsigned voice,
    float voct,
    float fm
) {
    // the minimal value for the frequency register to produce sound
    static constexpr float FREQ10BIT_MIN = 9;
    // the maximal value for the frequency register
    static constexpr float FREQ10BIT_MAX = 1023;
    // the clock division of the voice relative to the CPU
    static constexpr float CLOCK_DIVISION = 32;
    const int16_t* v = self->v;
    float octaves = NTPotatoChips::pitch(
        v[tone(voice, kToneCoarse)], v[tone(voice, kToneFine)]
    );
    octaves += voct;
    octaves += (v[tone(voice, kToneFm)] / 100.f) * fm / 5.f;
    const float freq = NTPotatoChips::frequency(octaves);
    return Math::clip(
        NTPotatoChips::CLOCK_RATE / (CLOCK_DIVISION * freq),
        FREQ10BIT_MIN,
        FREQ10BIT_MAX
    );
}

/// @brief Return the attenuation register value for an oscillator.
///
/// @param level the level parameter for the oscillator
/// @param cv the level CV in volts
/// @returns the 4-bit attenuation, i.e., the inverted level
///
static inline uint8_t getVolume(int16_t level, float cv) {
    // the maximal value for the level register
    static constexpr float MAX = 15;
    const float value = roundf(level * Math::Eurorack::fromDC(cv));
    // the register attenuates rather than amplifies, so invert the level
    return MAX - Math::clip(value, 0.f, MAX);
}

/// @brief Return the period register value for the noise generator.
///
/// @param self the algorithm to read parameters from
/// @param bus the noise period input bus, which may be NULL
/// @param frame the index of the frame being rendered
/// @returns the 2-bit period for the noise generator
///
static inline uint8_t getNoisePeriod(
    const _megaToneAlgorithm* self,
    const float* bus,
    unsigned frame
) {
    // the maximal value for the period register
    static constexpr float MAX = 3;
    float freq = self->v[kParamNoisePeriod];
    // unlike the other CV inputs this one does not normal to a fixed voltage;
    // an unrouted input leaves the parameter alone
    if (bus) freq += bus[frame] / 2.f;
    // invert the parameter so that larger values have higher frequencies
    return MAX - Math::clip(floorf(freq), 0.f, MAX);
}

void calculateRequirements(
    _NT_algorithmRequirements& req,
    const int32_t* specifications
) {
    req.numParameters = ARRAY_SIZE(parameters);
    req.sram = sizeof(_megaToneAlgorithm);
    req.dram = 0;
    req.dtc = 0;
    req.itc = 0;
}

_NT_algorithm* construct(
    const _NT_algorithmMemoryPtrs& ptrs,
    const _NT_algorithmRequirements& req,
    const int32_t* specifications
) {
    _megaToneAlgorithm* alg = new (ptrs.sram) _megaToneAlgorithm();
    alg->parameters = parameters;
    alg->parameterPages = &parameterPages;
    return alg;
}

void step(_NT_algorithm* self, float* busFrames, int numFramesBy4) {
    _megaToneAlgorithm* pThis = (_megaToneAlgorithm*)self;
    const int16_t* v = pThis->v;
    const unsigned numFrames = numFramesBy4 * 4;

    // resolve the busses once per step rather than once per frame
    const float* voct[TONE_COUNT];
    const float* fm[TONE_COUNT];
    const float* level[OSC_COUNT];
    Output outputs[OSC_COUNT];
    for (unsigned voice = 0; voice < TONE_COUNT; voice++) {
        voct[voice] = NTPotatoChips::inputBus(busFrames, numFrames, v[tone(voice, kToneVOct)]);
        fm[voice] = NTPotatoChips::inputBus(busFrames, numFrames, v[tone(voice, kToneFmInput)]);
        level[voice] = NTPotatoChips::inputBus(busFrames, numFrames, v[tone(voice, kToneLevelInput)]);
    }
    level[Chip::NOISE] = NTPotatoChips::inputBus(busFrames, numFrames, v[kParamNoiseLevelInput]);
    for (unsigned osc = 0; osc < OSC_COUNT; osc++) {
        outputs[osc].bus = NTPotatoChips::outputBus(busFrames, numFrames, v[kParamOutput + 2 * osc]);
        outputs[osc].replace = v[kParamOutput + 2 * osc + 1];
    }
    const float* noisePeriod = NTPotatoChips::inputBus(busFrames, numFrames, v[kParamNoisePeriodInput]);
    const float* lfsrInput = NTPotatoChips::inputBus(busFrames, numFrames, v[kParamLfsrInput]);

    float samples[OSC_COUNT];
    for (unsigned frame = 0; frame < numFrames; frame++) {
        // audio rate: the pitch of the tone generators
        for (unsigned voice = 0; voice < TONE_COUNT; voice++) {
            const float pitchCV = NTPotatoChips::voltage(voct[voice], frame, 0.f);
            // the FM input normals to 5V so that the attenuverter alone can
            // offset the pitch, as it does in the Rack build
            const float fmCV = NTPotatoChips::voltage(fm[voice], frame, 5.f);
            pThis->voice.apu.set_frequency(
                voice, getFrequency(pThis, voice, pitchCV, fmCV)
            );
        }

        // control rate: the levels and the noise generator
        if (pThis->voice.isControlRate()) {
            for (unsigned voice = 0; voice < TONE_COUNT; voice++) {
                // the level inputs normal to 10V, i.e., unity
                pThis->voice.apu.set_amplifier_level(
                    voice,
                    getVolume(
                        v[tone(voice, kToneLevel)],
                        NTPotatoChips::voltage(level[voice], frame, 10.f)
                    )
                );
            }
            pThis->lfsr.process(
                NTPotatoChips::triggerSignal(
                    NTPotatoChips::voltage(lfsrInput, frame, 0.f)
                )
            );
            // the input toggles the parameter, and the chip takes the flag
            // with "periodic" rather than "white" semantics
            const bool white = v[kParamLfsr] - pThis->lfsr.isHigh();
            pThis->voice.apu.set_noise(
                getNoisePeriod(pThis, noisePeriod, frame), !white, false
            );
            pThis->voice.apu.set_amplifier_level(
                Chip::NOISE,
                getVolume(
                    v[kParamNoiseLevel],
                    NTPotatoChips::voltage(level[Chip::NOISE], frame, 10.f)
                )
            );
        }

        pThis->voice.advance(samples);
        // the Rack build normals unpatched outputs into the following voice
        NTPotatoChips::writeFrame(samples, outputs, OSC_COUNT, frame, true);
    }
}

static const _NT_factory factory = {
    .guid = NT_MULTICHAR( 'P', 'C', 's', 'n' ),
    .name = "Mega Tone",
    .description = "Texas Instruments SN76489 (Sega Master System, BBC Micro)",
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
