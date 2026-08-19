// A Konami VRC6 chip algorithm for the disting NT.
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
#include "dsp/konami_vrc6.hpp"
#include "dsp/trigger/threshold.hpp"

using NTPotatoChips::ChipVoice;
using NTPotatoChips::Output;

/// the chip emulator this algorithm hosts
typedef KonamiVRC6 Chip;

/// the number of oscillators on the chip, i.e., two pulses and one saw
static constexpr unsigned OSC_COUNT = Chip::OSC_COUNT;

/// the number of pulse generators on the chip
static constexpr unsigned PULSE_COUNT = Chip::SAW;

/// the volume level to render the chip at
static constexpr float VOLUME = 5.f;

// ---------------------------------------------------------------------------
// MARK: Parameters
// ---------------------------------------------------------------------------

/// the parameters shared by every oscillator, in the order they are declared
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

/// @brief Return the index of a parameter within an oscillator's block.
///
/// @param osc the index of the oscillator
/// @param param the index of the parameter within the oscillator's block
/// @returns the index of the parameter within the algorithm
///
static constexpr uint8_t voiceParam(unsigned osc, unsigned param) {
    return osc * kNumVoiceParams + param;
}

/// the parameters that follow the three oscillator blocks
enum {
    kParamPulse1Width = OSC_COUNT * kNumVoiceParams,
    kParamPulse1WidthInput,
    kParamPulse2Width,
    kParamPulse2WidthInput,
    kParamSawSync,
    /// the first of OSC_COUNT (bus, mode) output parameter pairs
    kParamOutput,
    kNumParameters = kParamOutput + 2 * OSC_COUNT
};

/// @brief Declare the seven parameters shared by every oscillator.
/// @param NAME the display prefix for the oscillator, e.g., "Pulse 1"
/// @param LEVEL_MAX the maximal value of the oscillator's level register
/// @param LEVEL_DEF the default value of the oscillator's level register
#define VOICE_PARAMETERS( NAME, LEVEL_MAX, LEVEL_DEF ) \
    { .name = NAME " coarse", .min = -30, .max = 30, .def = 0, .unit = kNT_unitSemitones, .scaling = 0, .enumStrings = NULL }, \
    { .name = NAME " fine", .min = -100, .max = 100, .def = 0, .unit = kNT_unitCents, .scaling = 0, .enumStrings = NULL }, \
    NT_PARAMETER_CV_INPUT( NAME " V/Oct", 0, 0 ) \
    { .name = NAME " FM", .min = -100, .max = 100, .def = 0, .unit = kNT_unitNone, .scaling = kNT_scaling100, .enumStrings = NULL }, \
    NT_PARAMETER_CV_INPUT( NAME " FM input", 0, 0 ) \
    { .name = NAME " level", .min = 0, .max = LEVEL_MAX, .def = LEVEL_DEF, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL }, \
    NT_PARAMETER_CV_INPUT( NAME " level input", 0, 0 )

static const _NT_parameter parameters[] = {
    VOICE_PARAMETERS( "Pulse 1", 15, 12 )
    VOICE_PARAMETERS( "Pulse 2", 15, 12 )
    // the saw generator's level register is six bits rather than four
    VOICE_PARAMETERS( "Saw", 63, 32 )
    { .name = "Pulse 1 duty", .min = 0, .max = 7, .def = 7, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL },
    NT_PARAMETER_CV_INPUT( "Pulse 1 width input", 0, 0 )
    { .name = "Pulse 2 duty", .min = 0, .max = 7, .def = 7, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL },
    NT_PARAMETER_CV_INPUT( "Pulse 2 width input", 0, 0 )
    NT_PARAMETER_CV_INPUT( "Saw sync", 0, 0 )
    NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE( "Pulse 1 output", 0, 13 )
    NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE( "Pulse 2 output", 0, 14 )
    NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE( "Saw output", 0, 15 )
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

static const uint8_t pageSaw[] = {
    voiceParam(2, kVoiceCoarse), voiceParam(2, kVoiceFine), voiceParam(2, kVoiceVOct),
    voiceParam(2, kVoiceFm), voiceParam(2, kVoiceFmInput),
    voiceParam(2, kVoiceLevel), voiceParam(2, kVoiceLevelInput),
    kParamSawSync,
};

static const uint8_t pageRouting[] = {
    kParamOutput + 0, kParamOutput + 1,
    kParamOutput + 2, kParamOutput + 3,
    kParamOutput + 4, kParamOutput + 5,
};

static const _NT_parameterPage pages[] = {
    { .name = "Pulse 1", .numParams = ARRAY_SIZE(pagePulse1), .params = pagePulse1 },
    { .name = "Pulse 2", .numParams = ARRAY_SIZE(pagePulse2), .params = pagePulse2 },
    { .name = "Saw", .numParams = ARRAY_SIZE(pageSaw), .params = pageSaw },
    { .name = "Routing", .numParams = ARRAY_SIZE(pageRouting), .params = pageRouting },
};

static const _NT_parameterPages parameterPages = {
    .numPages = ARRAY_SIZE(pages),
    .pages = pages,
};

// ---------------------------------------------------------------------------
// MARK: Algorithm
// ---------------------------------------------------------------------------

/// @brief The state of a Step Saw instance.
struct _stepSawAlgorithm : public _NT_algorithm {
    /// @brief Initialize a new instance.
    _stepSawAlgorithm() : voice(VOLUME) { }

    /// the chip emulator and its BLIP buffers
    ChipVoice<Chip> voice;

    /// a trigger for handling inputs to the saw generator's sync input
    Trigger::Threshold syncTrigger;
};

/// @brief Return the 12-bit frequency register value for an oscillator.
///
/// @param self the algorithm to read parameters from
/// @param osc the index of the oscillator
/// @param voct the pitch CV in volts
/// @param fm the frequency modulation CV in volts
/// @param freqMin the minimal register value that produces sound
/// @param clockDivision the oscillator's clock division relative to the CPU
/// @returns the 12-bit frequency in a 16-bit container
/// @details
/// The pulse generators use freqMin = 4 and a division of 16; the saw
/// generator uses freqMin = 3 and a division of 14.
///
static inline uint16_t getFrequency(
    const _stepSawAlgorithm* self,
    unsigned osc,
    float voct,
    float fm,
    float freqMin,
    float clockDivision
) {
    // the maximal value for the frequency register
    static constexpr float FREQ12BIT_MAX = 4095;
    const int16_t* v = self->v;
    float octaves = NTPotatoChips::pitch(
        v[voiceParam(osc, kVoiceCoarse)], v[voiceParam(osc, kVoiceFine)]
    );
    octaves += voct;
    // unlike the other chips in the collection this one attenuates the FM
    // input against the 10V DC range rather than the 5V AC range
    octaves += (v[voiceParam(osc, kVoiceFm)] / 100.f) * Math::Eurorack::fromDC(fm);
    const float freq = Math::Eurorack::voct2freq(octaves);
    return Math::clip(
        (NTPotatoChips::CLOCK_RATE / (clockDivision * freq)) - 1,
        freqMin,
        FREQ12BIT_MAX
    );
}

/// @brief Return the level register value for an oscillator.
///
/// @param level the level parameter for the oscillator
/// @param cv the level CV in volts
/// @param max the maximal value of the oscillator's level register
/// @returns the level, in the low bits of an 8-bit container
///
static inline uint8_t getLevel(int16_t level, float cv, float max) {
    return Math::clip(level * Math::Eurorack::fromDC(cv), 0.f, max);
}

/// @brief Return the duty cycle bits for a pulse generator.
///
/// @param duty the duty cycle parameter for the generator
/// @param cv the pulse width CV in volts
/// @returns the duty cycle in the high four bits of an 8-bit container
///
static inline uint8_t getPulseWidth(int16_t duty, float cv) {
    // the maximal value for the duty cycle register
    static constexpr float MAX = 0b111;
    // shift the duty cycle over into the high four bits
    return static_cast<uint8_t>(Math::clip(duty + cv, 0.f, MAX)) << 4;
}

/// @brief Write an oscillator's frequency to the chip.
///
/// @param apu the chip emulator to write to
/// @param osc the index of the oscillator to set the frequency of
/// @param freq the 12-bit frequency register value
///
static inline void setFrequency(Chip& apu, unsigned osc, uint16_t freq) {
    const unsigned offset = Chip::REGS_PER_OSC * osc;
    apu.write(Chip::PULSE0_PERIOD_LOW + offset, freq & 0xff);
    // the high bits of the period register share a byte with the enable bit
    apu.write(
        Chip::PULSE0_PERIOD_HIGH + offset,
        ((freq >> 8) & 0x0f) | Chip::PERIOD_HIGH_ENABLED
    );
}

/// @brief Condition a sync input the way the Rack build does.
///
/// @param cv the voltage on the sync input
/// @returns the signal to pass to a Trigger::Threshold
/// @details
/// This module syncs from a much lower threshold than the other modules
/// trigger from: the trigger fires at 20mV and rearms below 10mV.
///
static inline float syncSignal(float cv) {
    return NTPotatoChips::rescale(cv, 0.01f, 0.02f, 0.f, 1.f);
}

void calculateRequirements(
    _NT_algorithmRequirements& req,
    const int32_t* specifications
) {
    req.numParameters = ARRAY_SIZE(parameters);
    req.sram = sizeof(_stepSawAlgorithm);
    req.dram = 0;
    req.dtc = 0;
    req.itc = 0;
}

_NT_algorithm* construct(
    const _NT_algorithmMemoryPtrs& ptrs,
    const _NT_algorithmRequirements& req,
    const int32_t* specifications
) {
    _stepSawAlgorithm* alg = new (ptrs.sram) _stepSawAlgorithm();
    alg->parameters = parameters;
    alg->parameterPages = &parameterPages;
    return alg;
}

void step(_NT_algorithm* self, float* busFrames, int numFramesBy4) {
    _stepSawAlgorithm* pThis = (_stepSawAlgorithm*)self;
    const int16_t* v = pThis->v;
    const unsigned numFrames = numFramesBy4 * 4;

    // resolve the busses once per step rather than once per frame
    const float* voct[OSC_COUNT];
    const float* fm[OSC_COUNT];
    const float* level[OSC_COUNT];
    const float* width[PULSE_COUNT];
    Output outputs[OSC_COUNT];
    for (unsigned osc = 0; osc < OSC_COUNT; osc++) {
        voct[osc] = NTPotatoChips::inputBus(busFrames, numFrames, v[voiceParam(osc, kVoiceVOct)]);
        fm[osc] = NTPotatoChips::inputBus(busFrames, numFrames, v[voiceParam(osc, kVoiceFmInput)]);
        level[osc] = NTPotatoChips::inputBus(busFrames, numFrames, v[voiceParam(osc, kVoiceLevelInput)]);
        outputs[osc].bus = NTPotatoChips::outputBus(busFrames, numFrames, v[kParamOutput + 2 * osc]);
        outputs[osc].replace = v[kParamOutput + 2 * osc + 1];
    }
    width[0] = NTPotatoChips::inputBus(busFrames, numFrames, v[kParamPulse1WidthInput]);
    width[1] = NTPotatoChips::inputBus(busFrames, numFrames, v[kParamPulse2WidthInput]);
    const float* sawSync = NTPotatoChips::inputBus(busFrames, numFrames, v[kParamSawSync]);

    // the duty cycle parameters of the two pulse generators
    static const uint8_t dutyParams[PULSE_COUNT] = {
        kParamPulse1Width, kParamPulse2Width
    };
    // the maximal level register value of each oscillator
    static const float levelMax[OSC_COUNT] = { 15.f, 15.f, 63.f };

    float samples[OSC_COUNT];
    for (unsigned frame = 0; frame < numFrames; frame++) {
        // audio rate: hard sync and the pitch of each oscillator
        if (pThis->syncTrigger.process(
                syncSignal(NTPotatoChips::voltage(sawSync, frame, 0.f))))
            pThis->voice.apu.reset_phase(Chip::SAW);
        for (unsigned osc = 0; osc < OSC_COUNT; osc++) {
            const float pitchCV = NTPotatoChips::voltage(voct[osc], frame, 0.f);
            // the FM input normals to 5V so that the attenuverter alone can
            // offset the pitch, as it does in the Rack build
            const float fmCV = NTPotatoChips::voltage(fm[osc], frame, 5.f);
            // the saw generator divides the clock by 14 rather than 16
            const bool isPulse = osc < PULSE_COUNT;
            setFrequency(
                pThis->voice.apu,
                osc,
                getFrequency(
                    pThis, osc, pitchCV, fmCV,
                    isPulse ? 4 : 3,
                    isPulse ? 16 : 14
                )
            );
        }

        // control rate: the duty cycles and levels
        if (pThis->voice.isControlRate()) {
            for (unsigned osc = 0; osc < OSC_COUNT; osc++) {
                // the saw generator has no duty cycle; its level occupies the
                // whole register
                const uint8_t duty = osc < PULSE_COUNT
                    ? getPulseWidth(
                        v[dutyParams[osc]],
                        NTPotatoChips::voltage(width[osc], frame, 0.f)
                    )
                    : 0;
                pThis->voice.apu.write(
                    Chip::PULSE0_DUTY_VOLUME + Chip::REGS_PER_OSC * osc,
                    duty | getLevel(
                        v[voiceParam(osc, kVoiceLevel)],
                        NTPotatoChips::voltage(level[osc], frame, 10.f),
                        levelMax[osc]
                    )
                );
            }
        }

        pThis->voice.advance(samples);
        // the Rack build normals unpatched outputs into the following voice
        NTPotatoChips::writeFrame(samples, outputs, OSC_COUNT, frame, true);
    }
}

static const _NT_factory factory = {
    .guid = NT_MULTICHAR( 'P', 'C', 'V', '6' ),
    .name = "Step Saw",
    .description = "Konami VRC6 (Akumajou Densetsu)",
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
