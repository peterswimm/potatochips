// A Ricoh 2A03 chip algorithm for the disting NT.
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
#include "dsp/ricoh_2a03.hpp"
#include "dsp/trigger/threshold.hpp"

using NTPotatoChips::ChipVoice;
using NTPotatoChips::Output;

/// the chip emulator this algorithm hosts
typedef Ricoh2A03 Chip;

/// the number of oscillators on the chip
static constexpr unsigned OSC_COUNT = Chip::OSC_COUNT;

/// the number of pulse generators on the chip
static constexpr unsigned PULSE_COUNT = Chip::TRIANGLE;

/// the number of oscillators with a frequency register, i.e., all but the noise
static constexpr unsigned PITCHED_COUNT = Chip::NOISE;

/// the volume level to render the chip at
static constexpr float VOLUME = 6.f;

// ---------------------------------------------------------------------------
// MARK: Parameters
// ---------------------------------------------------------------------------

/// the parameters shared by the pitched oscillators, in declaration order
enum {
    kVoiceCoarse,
    kVoiceFine,
    kVoiceVOct,
    kVoiceFm,
    kVoiceFmInput,
    kVoiceLevel,
    kVoiceLevelInput,
    kNumVoiceParams
};

/// @brief Return the index of a parameter within a pitched voice's block.
///
/// @param voice the index of the oscillator
/// @param param the index of the parameter within the oscillator's block
/// @returns the index of the parameter within the algorithm
///
static constexpr uint8_t voiceParam(unsigned voice, unsigned param) {
    return voice * kNumVoiceParams + param;
}

/// the parameters that follow the three pitched oscillator blocks
enum {
    kParamPulse1Width = PITCHED_COUNT * kNumVoiceParams,
    kParamPulse1WidthInput,
    kParamPulse2Width,
    kParamPulse2WidthInput,
    kParamTriangleSync,
    kParamNoisePeriod,
    kParamNoisePeriodInput,
    kParamLfsr,
    kParamLfsrInput,
    kParamNoiseLevel,
    kParamNoiseLevelInput,
    kParamNoiseSync,
    /// the first of OSC_COUNT (bus, mode) output parameter pairs
    kParamOutput,
    kNumParameters = kParamOutput + 2 * OSC_COUNT
};

static char const * const enumStringsOffOn[] = { "Off", "On" };

static char const * const enumStringsDuty[] = {
    "12.5%",
    "25%",
    "50%",
    "75%",
};

/// @brief Declare the seven parameters shared by the pitched oscillators.
/// @param NAME the display prefix for the oscillator, e.g., "Pulse 1"
#define VOICE_PARAMETERS( NAME ) \
    { .name = NAME " coarse", .min = -30, .max = 30, .def = 0, .unit = kNT_unitSemitones, .scaling = 0, .enumStrings = NULL }, \
    { .name = NAME " fine", .min = -100, .max = 100, .def = 0, .unit = kNT_unitCents, .scaling = 0, .enumStrings = NULL }, \
    NT_PARAMETER_CV_INPUT( NAME " V/Oct", 0, 0 ) \
    { .name = NAME " FM", .min = -100, .max = 100, .def = 0, .unit = kNT_unitNone, .scaling = kNT_scaling100, .enumStrings = NULL }, \
    NT_PARAMETER_CV_INPUT( NAME " FM input", 0, 0 ) \
    { .name = NAME " level", .min = 0, .max = 15, .def = 10, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL }, \
    NT_PARAMETER_CV_INPUT( NAME " level input", 0, 0 )

static const _NT_parameter parameters[] = {
    VOICE_PARAMETERS( "Pulse 1" )
    VOICE_PARAMETERS( "Pulse 2" )
    VOICE_PARAMETERS( "Triangle" )
    { .name = "Pulse 1 duty", .min = 0, .max = 3, .def = 2, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = enumStringsDuty },
    NT_PARAMETER_CV_INPUT( "Pulse 1 width input", 0, 0 )
    { .name = "Pulse 2 duty", .min = 0, .max = 3, .def = 2, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = enumStringsDuty },
    NT_PARAMETER_CV_INPUT( "Pulse 2 width input", 0, 0 )
    NT_PARAMETER_CV_INPUT( "Triangle sync", 0, 0 )
    { .name = "Noise period", .min = 0, .max = 15, .def = 7, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL },
    NT_PARAMETER_CV_INPUT( "Noise period input", 0, 0 )
    { .name = "LFSR", .min = 0, .max = 1, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = enumStringsOffOn },
    NT_PARAMETER_CV_INPUT( "LFSR input", 0, 0 )
    { .name = "Noise level", .min = 0, .max = 15, .def = 10, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL },
    NT_PARAMETER_CV_INPUT( "Noise level input", 0, 0 )
    NT_PARAMETER_CV_INPUT( "Noise sync", 0, 0 )
    NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE( "Pulse 1 output", 0, 13 )
    NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE( "Pulse 2 output", 0, 14 )
    NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE( "Triangle output", 0, 15 )
    NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE( "Noise output", 0, 16 )
};

static_assert(
    ARRAY_SIZE(parameters) == kNumParameters,
    "the parameter table and the parameter enum disagree"
);

static const uint8_t pagePulse1[] = {
    voiceParam(0, kVoiceCoarse), voiceParam(0, kVoiceFine), voiceParam(0, kVoiceVOct),
    voiceParam(0, kVoiceFm), voiceParam(0, kVoiceFmInput),
    voiceParam(0, kVoiceLevel), voiceParam(0, kVoiceLevelInput),
    kParamPulse1Width, kParamPulse1WidthInput,
};

static const uint8_t pagePulse2[] = {
    voiceParam(1, kVoiceCoarse), voiceParam(1, kVoiceFine), voiceParam(1, kVoiceVOct),
    voiceParam(1, kVoiceFm), voiceParam(1, kVoiceFmInput),
    voiceParam(1, kVoiceLevel), voiceParam(1, kVoiceLevelInput),
    kParamPulse2Width, kParamPulse2WidthInput,
};

static const uint8_t pageTriangle[] = {
    voiceParam(2, kVoiceCoarse), voiceParam(2, kVoiceFine), voiceParam(2, kVoiceVOct),
    voiceParam(2, kVoiceFm), voiceParam(2, kVoiceFmInput),
    voiceParam(2, kVoiceLevel), voiceParam(2, kVoiceLevelInput),
    kParamTriangleSync,
};

static const uint8_t pageNoise[] = {
    kParamNoisePeriod, kParamNoisePeriodInput,
    kParamLfsr, kParamLfsrInput,
    kParamNoiseLevel, kParamNoiseLevelInput,
    kParamNoiseSync,
};

static const uint8_t pageRouting[] = {
    kParamOutput + 0, kParamOutput + 1,
    kParamOutput + 2, kParamOutput + 3,
    kParamOutput + 4, kParamOutput + 5,
    kParamOutput + 6, kParamOutput + 7,
};

static const _NT_parameterPage pages[] = {
    { .name = "Pulse 1", .numParams = ARRAY_SIZE(pagePulse1), .params = pagePulse1 },
    { .name = "Pulse 2", .numParams = ARRAY_SIZE(pagePulse2), .params = pagePulse2 },
    { .name = "Triangle", .numParams = ARRAY_SIZE(pageTriangle), .params = pageTriangle },
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

/// @brief The state of an Infinite Stairs instance.
struct _infiniteStairsAlgorithm : public _NT_algorithm {
    /// @brief Initialize a new instance.
    _infiniteStairsAlgorithm() : voice(VOLUME) { }

    /// the chip emulator and its BLIP buffers
    ChipVoice<Chip> voice;

    /// a trigger for handling inputs to the LFSR input
    Trigger::Threshold lfsr;

    /// triggers for the triangle and noise sync inputs
    Trigger::Threshold syncTriggers[OSC_COUNT - PULSE_COUNT];
};

/// @brief Return the frequency register value for a pitched oscillator.
///
/// @param self the algorithm to read parameters from
/// @param voice the index of the oscillator
/// @param voct the pitch CV in volts
/// @param fm the frequency modulation CV in volts
/// @param freqMin the minimal register value that produces sound
/// @param freqMax the maximal register value
/// @param clockDivision the oscillator's clock division relative to the CPU
/// @returns the frequency register value in a 16-bit container
/// @details
/// The pulse generators use freqMin = 8, freqMax = 1023 and a division of 16;
/// the triangle uses freqMin = 2, freqMax = 2047 and a division of 32.
///
static inline uint16_t getFrequency(
    const _infiniteStairsAlgorithm* self,
    unsigned voice,
    float voct,
    float fm,
    float freqMin,
    float freqMax,
    float clockDivision
) {
    const int16_t* v = self->v;
    float octaves = NTPotatoChips::pitch(
        v[voiceParam(voice, kVoiceCoarse)], v[voiceParam(voice, kVoiceFine)]
    );
    octaves += voct;
    octaves += (v[voiceParam(voice, kVoiceFm)] / 100.f) * fm / 5.f;
    const float freq = NTPotatoChips::frequency(octaves);
    return Math::clip(
        (NTPotatoChips::CLOCK_RATE / (clockDivision * freq)) - 1,
        freqMin,
        freqMax
    );
}

/// @brief Return the level register value for an oscillator.
///
/// @param level the level parameter for the oscillator
/// @param cv the level CV in volts
/// @returns the 4-bit level
///
static inline uint8_t getVolume(int16_t level, float cv) {
    // the maximal value for the level register
    static constexpr float MAX = 15;
    const float value = roundf(level * Math::Eurorack::fromDC(cv));
    return Math::clip(value, 0.f, MAX);
}

/// @brief Return the duty cycle bits for a pulse generator.
///
/// @param duty the duty cycle parameter for the generator
/// @param cv the pulse width CV in volts
/// @returns the duty cycle in the high two bits of an 8-bit container
///
static inline uint8_t getPulseWidth(int16_t duty, float cv) {
    // the maximal value for the duty cycle register
    static constexpr float MAX = 3;
    const float value = duty + NTPotatoChips::rescale(cv, 0.f, 7.f, 0.f, 4.f);
    // shift the duty cycle over into the high two bits
    return static_cast<uint8_t>(Math::clip(value, 0.f, MAX)) << 6;
}

/// @brief Return the period register value for the noise generator.
///
/// @param self the algorithm to read parameters from
/// @param bus the noise period input bus, which may be NULL
/// @param frame the index of the frame being rendered
/// @returns the 4-bit period for the noise generator
///
static inline uint8_t getNoisePeriod(
    const _infiniteStairsAlgorithm* self,
    const float* bus,
    unsigned frame
) {
    // the maximal value for the period register
    static constexpr float MAX = 15;
    float freq = self->v[kParamNoisePeriod];
    // unlike the other CV inputs this one does not normal to a fixed voltage;
    // an unrouted input leaves the parameter alone
    if (bus) freq += bus[frame] / 2.f;
    // invert the parameter so that larger values have higher frequencies
    return MAX - Math::clip(floorf(freq), 0.f, MAX);
}

/// @brief Condition a sync input the way the Rack build does.
///
/// @param cv the voltage on the sync input
/// @returns the signal to pass to a Trigger::Threshold
/// @details
/// This module syncs from a much lower threshold than it triggers its other
/// inputs from: the trigger fires at 20mV and rearms below 10mV.
///
static inline float syncSignal(float cv) {
    return NTPotatoChips::rescale(cv, 0.01f, 0.02f, 0.f, 1.f);
}

void calculateRequirements(
    _NT_algorithmRequirements& req,
    const int32_t* specifications
) {
    req.numParameters = ARRAY_SIZE(parameters);
    req.sram = sizeof(_infiniteStairsAlgorithm);
    req.dram = 0;
    req.dtc = 0;
    req.itc = 0;
}

_NT_algorithm* construct(
    const _NT_algorithmMemoryPtrs& ptrs,
    const _NT_algorithmRequirements& req,
    const int32_t* specifications
) {
    _infiniteStairsAlgorithm* alg =
        new (ptrs.sram) _infiniteStairsAlgorithm();
    alg->parameters = parameters;
    alg->parameterPages = &parameterPages;
    return alg;
}

void step(_NT_algorithm* self, float* busFrames, int numFramesBy4) {
    _infiniteStairsAlgorithm* pThis = (_infiniteStairsAlgorithm*)self;
    const int16_t* v = pThis->v;
    const unsigned numFrames = numFramesBy4 * 4;

    // resolve the busses once per step rather than once per frame
    const float* voct[PITCHED_COUNT];
    const float* fm[PITCHED_COUNT];
    const float* level[OSC_COUNT];
    const float* width[PULSE_COUNT];
    const float* sync[OSC_COUNT - PULSE_COUNT];
    Output outputs[OSC_COUNT];
    for (unsigned voice = 0; voice < PITCHED_COUNT; voice++) {
        voct[voice] = NTPotatoChips::inputBus(busFrames, numFrames, v[voiceParam(voice, kVoiceVOct)]);
        fm[voice] = NTPotatoChips::inputBus(busFrames, numFrames, v[voiceParam(voice, kVoiceFmInput)]);
        level[voice] = NTPotatoChips::inputBus(busFrames, numFrames, v[voiceParam(voice, kVoiceLevelInput)]);
    }
    level[Chip::NOISE] = NTPotatoChips::inputBus(busFrames, numFrames, v[kParamNoiseLevelInput]);
    width[0] = NTPotatoChips::inputBus(busFrames, numFrames, v[kParamPulse1WidthInput]);
    width[1] = NTPotatoChips::inputBus(busFrames, numFrames, v[kParamPulse2WidthInput]);
    sync[0] = NTPotatoChips::inputBus(busFrames, numFrames, v[kParamTriangleSync]);
    sync[1] = NTPotatoChips::inputBus(busFrames, numFrames, v[kParamNoiseSync]);
    for (unsigned osc = 0; osc < OSC_COUNT; osc++) {
        outputs[osc].bus = NTPotatoChips::outputBus(busFrames, numFrames, v[kParamOutput + 2 * osc]);
        outputs[osc].replace = v[kParamOutput + 2 * osc + 1];
    }
    const float* noisePeriod = NTPotatoChips::inputBus(busFrames, numFrames, v[kParamNoisePeriodInput]);
    const float* lfsrInput = NTPotatoChips::inputBus(busFrames, numFrames, v[kParamLfsrInput]);

    // the duty cycle parameters are adjacent to their input parameters
    static const uint8_t dutyParams[PULSE_COUNT] = {
        kParamPulse1Width, kParamPulse2Width
    };

    float samples[OSC_COUNT];
    for (unsigned frame = 0; frame < numFrames; frame++) {
        // audio rate: pitch and hard sync
        for (unsigned voice = 0; voice < PITCHED_COUNT; voice++) {
            const float pitchCV = NTPotatoChips::voltage(voct[voice], frame, 0.f);
            // the FM input normals to 5V so that the attenuverter alone can
            // offset the pitch, as it does in the Rack build
            const float fmCV = NTPotatoChips::voltage(fm[voice], frame, 5.f);
            // the pulse generators and the triangle divide the clock and
            // range over their frequency registers differently
            const bool isPulse = voice < PULSE_COUNT;
            pThis->voice.apu.set_frequency(
                voice,
                getFrequency(
                    pThis, voice, pitchCV, fmCV,
                    isPulse ? 8 : 2,
                    isPulse ? 1023 : 2047,
                    isPulse ? 16 : 32
                )
            );
        }
        for (unsigned i = 0; i < OSC_COUNT - PULSE_COUNT; i++) {
            if (pThis->syncTriggers[i].process(
                    syncSignal(NTPotatoChips::voltage(sync[i], frame, 0.f))))
                pThis->voice.apu.reset_phase(PULSE_COUNT + i);
        }

        // control rate: the levels, duty cycles and noise generator
        if (pThis->voice.isControlRate()) {
            for (unsigned voice = 0; voice < PULSE_COUNT; voice++) {
                // the duty cycle occupies the high two bits of the register
                // and the level the low four bits
                pThis->voice.apu.set_voice_volume(
                    voice,
                    getPulseWidth(
                        v[dutyParams[voice]],
                        NTPotatoChips::voltage(width[voice], frame, 0.f)
                    ) | getVolume(
                        v[voiceParam(voice, kVoiceLevel)],
                        NTPotatoChips::voltage(level[voice], frame, 10.f)
                    )
                );
            }
            pThis->voice.apu.set_voice_volume(
                Chip::TRIANGLE,
                getVolume(
                    v[voiceParam(Chip::TRIANGLE, kVoiceLevel)],
                    NTPotatoChips::voltage(level[Chip::TRIANGLE], frame, 10.f)
                )
            );
            pThis->lfsr.process(
                NTPotatoChips::triggerSignal(
                    NTPotatoChips::voltage(lfsrInput, frame, 0.f)
                )
            );
            // the input toggles the parameter
            const bool isLfsr = v[kParamLfsr] - pThis->lfsr.isHigh();
            pThis->voice.apu.set_noise_period(
                getNoisePeriod(pThis, noisePeriod, frame), isLfsr
            );
            pThis->voice.apu.set_voice_volume(
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
    .guid = NT_MULTICHAR( 'P', 'C', '2', 'A' ),
    .name = "Infinite Stairs",
    .description = "Ricoh 2A03 (Nintendo Entertainment System)",
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
