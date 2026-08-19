// A Namco 163 chip algorithm for the disting NT.
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
#include "dsp/namco_163.hpp"
#include "dsp/wavetable4bit.hpp"

using NTPotatoChips::ChipVoice;
using NTPotatoChips::Output;

/// the chip emulator this algorithm hosts
typedef Namco163 Chip;

/// the number of oscillators on the chip
static constexpr unsigned OSC_COUNT = Chip::OSC_COUNT;

/// the number of samples in a wave-table
static constexpr unsigned SAMPLES_PER_WAVETABLE = 32;

/// the number of wave-tables the oscillators morph between
static constexpr unsigned NUM_WAVEFORMS = 5;

/// the volume level to render the chip at
static constexpr float VOLUME = 3.f;

// ---------------------------------------------------------------------------
// MARK: Parameters
// ---------------------------------------------------------------------------

/// the parameters of one oscillator, in the order they are declared
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

/// the parameters that follow the eight oscillator blocks
enum {
    kParamActiveOscillators = OSC_COUNT * kNumVoiceParams,
    kParamActiveOscillatorsAtt,
    kParamActiveOscillatorsInput,
    kParamWaveform,
    kParamWaveformAtt,
    kParamWaveformInput,
    /// the first of OSC_COUNT (bus, mode) output parameter pairs
    kParamOutput,
    kNumParameters = kParamOutput + 2 * OSC_COUNT
};

/// @brief Declare the seven parameters of one oscillator.
/// @param NAME the display prefix for the oscillator, e.g., "Voice 1"
#define VOICE_PARAMETERS( NAME ) \
    { .name = NAME " coarse", .min = -30, .max = 30, .def = 0, .unit = kNT_unitSemitones, .scaling = 0, .enumStrings = NULL }, \
    { .name = NAME " fine", .min = -100, .max = 100, .def = 0, .unit = kNT_unitCents, .scaling = 0, .enumStrings = NULL }, \
    NT_PARAMETER_CV_INPUT( NAME " V/Oct", 0, 0 ) \
    { .name = NAME " FM", .min = -100, .max = 100, .def = 0, .unit = kNT_unitNone, .scaling = kNT_scaling100, .enumStrings = NULL }, \
    NT_PARAMETER_CV_INPUT( NAME " FM input", 0, 0 ) \
    { .name = NAME " level", .min = 0, .max = 15, .def = 15, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL }, \
    NT_PARAMETER_CV_INPUT( NAME " level input", 0, 0 )

static const _NT_parameter parameters[] = {
    VOICE_PARAMETERS( "Voice 1" )
    VOICE_PARAMETERS( "Voice 2" )
    VOICE_PARAMETERS( "Voice 3" )
    VOICE_PARAMETERS( "Voice 4" )
    VOICE_PARAMETERS( "Voice 5" )
    VOICE_PARAMETERS( "Voice 6" )
    VOICE_PARAMETERS( "Voice 7" )
    VOICE_PARAMETERS( "Voice 8" )
    // the chip time-multiplexes its oscillators, so enabling more of them
    // lowers the sample rate of each
    { .name = "Active voices", .min = 1, .max = 8, .def = 4, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL },
    { .name = "Active voices att", .min = -100, .max = 100, .def = 0, .unit = kNT_unitNone, .scaling = kNT_scaling100, .enumStrings = NULL },
    NT_PARAMETER_CV_INPUT( "Active voices input", 0, 0 )
    // the oscillators share one wave RAM, morphed between the five built-in
    // wave-tables; the fractional part interpolates between neighbours
    { .name = "Waveform", .min = 100, .max = 500, .def = 100, .unit = kNT_unitNone, .scaling = kNT_scaling100, .enumStrings = NULL },
    { .name = "Waveform att", .min = -100, .max = 100, .def = 0, .unit = kNT_unitNone, .scaling = kNT_scaling100, .enumStrings = NULL },
    NT_PARAMETER_CV_INPUT( "Waveform input", 0, 0 )
    NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE( "Voice 1 output", 0, 13 )
    NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE( "Voice 2 output", 0, 14 )
    NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE( "Voice 3 output", 0, 15 )
    NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE( "Voice 4 output", 0, 16 )
    NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE( "Voice 5 output", 0, 17 )
    NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE( "Voice 6 output", 0, 18 )
    NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE( "Voice 7 output", 0, 19 )
    NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE( "Voice 8 output", 0, 20 )
};

static_assert(
    ARRAY_SIZE(parameters) == kNumParameters,
    "the parameter table and the parameter enum disagree"
);

/// @brief Declare the page contents of one oscillator.
/// @param OSC the index of the oscillator
#define VOICE_PAGE( OSC ) { \
    voiceParam(OSC, kVoiceCoarse), voiceParam(OSC, kVoiceFine), \
    voiceParam(OSC, kVoiceVOct), \
    voiceParam(OSC, kVoiceFm), voiceParam(OSC, kVoiceFmInput), \
    voiceParam(OSC, kVoiceLevel), voiceParam(OSC, kVoiceLevelInput), \
}

static const uint8_t pageVoice1[] = VOICE_PAGE( 0 );
static const uint8_t pageVoice2[] = VOICE_PAGE( 1 );
static const uint8_t pageVoice3[] = VOICE_PAGE( 2 );
static const uint8_t pageVoice4[] = VOICE_PAGE( 3 );
static const uint8_t pageVoice5[] = VOICE_PAGE( 4 );
static const uint8_t pageVoice6[] = VOICE_PAGE( 5 );
static const uint8_t pageVoice7[] = VOICE_PAGE( 6 );
static const uint8_t pageVoice8[] = VOICE_PAGE( 7 );

static const uint8_t pageChip[] = {
    kParamActiveOscillators, kParamActiveOscillatorsAtt,
    kParamActiveOscillatorsInput,
    kParamWaveform, kParamWaveformAtt, kParamWaveformInput,
};

static const uint8_t pageRouting[] = {
    kParamOutput + 0, kParamOutput + 1,
    kParamOutput + 2, kParamOutput + 3,
    kParamOutput + 4, kParamOutput + 5,
    kParamOutput + 6, kParamOutput + 7,
    kParamOutput + 8, kParamOutput + 9,
    kParamOutput + 10, kParamOutput + 11,
    kParamOutput + 12, kParamOutput + 13,
    kParamOutput + 14, kParamOutput + 15,
};

static const _NT_parameterPage pages[] = {
    { .name = "Voice 1", .numParams = ARRAY_SIZE(pageVoice1), .params = pageVoice1 },
    { .name = "Voice 2", .numParams = ARRAY_SIZE(pageVoice2), .params = pageVoice2 },
    { .name = "Voice 3", .numParams = ARRAY_SIZE(pageVoice3), .params = pageVoice3 },
    { .name = "Voice 4", .numParams = ARRAY_SIZE(pageVoice4), .params = pageVoice4 },
    { .name = "Voice 5", .numParams = ARRAY_SIZE(pageVoice5), .params = pageVoice5 },
    { .name = "Voice 6", .numParams = ARRAY_SIZE(pageVoice6), .params = pageVoice6 },
    { .name = "Voice 7", .numParams = ARRAY_SIZE(pageVoice7), .params = pageVoice7 },
    { .name = "Voice 8", .numParams = ARRAY_SIZE(pageVoice8), .params = pageVoice8 },
    { .name = "Chip", .numParams = ARRAY_SIZE(pageChip), .params = pageChip },
    { .name = "Routing", .numParams = ARRAY_SIZE(pageRouting), .params = pageRouting },
};

static const _NT_parameterPages parameterPages = {
    .numPages = ARRAY_SIZE(pages),
    .pages = pages,
};

// ---------------------------------------------------------------------------
// MARK: Algorithm
// ---------------------------------------------------------------------------

/// the wave-tables the oscillators morph between
static uint8_t const * const WAVETABLES[NUM_WAVEFORMS] = {
    SINE,
    PW5,
    RAMP_UP,
    TRIANGLE_DIST,
    RAMP_DOWN
};

/// @brief The state of a Name Corp Octal Wave Generator instance.
struct _nameCorpAlgorithm : public _NT_algorithm {
    /// @brief Initialize a new instance.
    _nameCorpAlgorithm() : voice(VOLUME) { }

    /// the chip emulator and its BLIP buffers
    ChipVoice<Chip> voice;

    /// the wave-table position the chip's RAM currently holds
    /// @details
    /// Rewriting the wave RAM costs sixteen register writes, so the position
    /// is cached and the RAM only refreshed when the morph actually moves.
    /// The sentinel is outside the parameter's range so that the first
    /// control-rate update always writes.
    float wavetablePosition = -1.f;
};

/// @brief Return the frequency register value for an oscillator.
///
/// @param self the algorithm to read parameters from
/// @param osc the index of the oscillator
/// @param voct the pitch CV in volts
/// @param fm the frequency modulation CV in volts
/// @returns the 18-bit frequency with the waveform length in the high bits
///
static inline uint32_t getFrequency(
    const _nameCorpAlgorithm* self,
    unsigned osc,
    float voct,
    float fm
) {
    // the length of the waveform in the chip's RAM, in the chip's own units
    static constexpr uint32_t WAVE_LENGTH = 64 - (SAMPLES_PER_WAVETABLE / 4);
    const int16_t* v = self->v;
    float octaves = NTPotatoChips::pitch(
        v[voiceParam(osc, kVoiceCoarse)], v[voiceParam(osc, kVoiceFine)]
    );
    octaves += voct;
    octaves += (v[voiceParam(osc, kVoiceFm)] / 100.f) * fm / 5.f;
    float freq = NTPotatoChips::frequency(octaves);
    // leaving the active oscillator count out of this calculation gives the
    // standard 103 behaviour, where enabling more oscillators detunes them all
    freq *= (WAVE_LENGTH * 15.f * 65536.f) / NTPotatoChips::CLOCK_RATE;
    freq = Math::clip(freq, 512.f, 262143.f);
    // the waveform length occupies the high six bits of the frequency's third
    // byte, i.e., bit 18 and up
    return static_cast<uint32_t>(freq) | (WAVE_LENGTH << 18);
}

/// @brief Return the level register value for an oscillator.
///
/// @param level the level parameter for the oscillator
/// @param cv the level CV in volts
/// @returns the 4-bit level
///
static inline uint8_t getVolume(int16_t level, float cv) {
    const float value = roundf(level * Math::Eurorack::fromDC(cv));
    return Math::clip(value, 0.f, 15.f);
}

/// @brief Return the number of oscillators the chip should run.
///
/// @param self the algorithm to read parameters from
/// @param cv the active voices CV in volts
/// @returns the number of active oscillators, in [1, 8]
///
static inline uint8_t getActiveOscillators(
    const _nameCorpAlgorithm* self,
    float cv
) {
    const float param = self->v[kParamActiveOscillators];
    const float att = self->v[kParamActiveOscillatorsAtt] / 100.f;
    // the CV covers all eight oscillators over the 10V DC range
    return Math::clip(
        param + att * 8.f * Math::Eurorack::fromDC(cv), 1.f, 8.f
    );
}

/// @brief Return the position of the oscillators within the wave-tables.
///
/// @param self the algorithm to read parameters from
/// @param cv the waveform CV in volts
/// @returns the fractional index of the wave-table in [0, NUM_WAVEFORMS - 1]
///
static inline float getWavetablePosition(
    const _nameCorpAlgorithm* self,
    float cv
) {
    const float param = self->v[kParamWaveform] / 100.f;
    const float att = self->v[kParamWaveformAtt] / 100.f;
    // rescale from a 7V range onto the parameter's span
    const float mod = NTPotatoChips::rescale(cv, -7.f, 7.f, -5.f, 5.f);
    // the wave-tables are numbered from one on the panel, so subtract one
    return Math::clip(param + att * mod, 1.f, 5.f) - 1.f;
}

/// @brief Write the interpolated wave-table to the chip's wave RAM.
///
/// @param apu the chip emulator to write to
/// @param position the fractional index of the wave-table to write
///
static void setWavetable(Chip& apu, float position) {
    // the wave-tables to interpolate between and the weight of the second
    const unsigned table0 = floorf(position);
    const unsigned table1 = ceilf(position);
    const float interpolate = position - table0;
    // the chip packs two four-bit samples into each byte of wave RAM, so
    // consider the samples two at a time
    for (unsigned i = 0; i < SAMPLES_PER_WAVETABLE / 2; i++) {
        const unsigned sample = i * 2;
        const uint8_t nibbleLo = (1.f - interpolate) * WAVETABLES[table0][sample] +
            interpolate * WAVETABLES[table1][sample];
        const uint8_t nibbleHi = (1.f - interpolate) * WAVETABLES[table0][sample + 1] +
            interpolate * WAVETABLES[table1][sample + 1];
        apu.write(i, (nibbleHi << 4) | nibbleLo);
    }
}

void calculateRequirements(
    _NT_algorithmRequirements& req,
    const int32_t* specifications
) {
    req.numParameters = ARRAY_SIZE(parameters);
    req.sram = sizeof(_nameCorpAlgorithm);
    req.dram = 0;
    req.dtc = 0;
    req.itc = 0;
}

_NT_algorithm* construct(
    const _NT_algorithmMemoryPtrs& ptrs,
    const _NT_algorithmRequirements& req,
    const int32_t* specifications
) {
    _nameCorpAlgorithm* alg = new (ptrs.sram) _nameCorpAlgorithm();
    alg->parameters = parameters;
    alg->parameterPages = &parameterPages;
    return alg;
}

void step(_NT_algorithm* self, float* busFrames, int numFramesBy4) {
    _nameCorpAlgorithm* pThis = (_nameCorpAlgorithm*)self;
    const int16_t* v = pThis->v;
    const unsigned numFrames = numFramesBy4 * 4;

    // resolve the busses once per step rather than once per frame
    const float* voct[OSC_COUNT];
    const float* fm[OSC_COUNT];
    const float* level[OSC_COUNT];
    Output outputs[OSC_COUNT];
    for (unsigned osc = 0; osc < OSC_COUNT; osc++) {
        voct[osc] = NTPotatoChips::inputBus(busFrames, numFrames, v[voiceParam(osc, kVoiceVOct)]);
        fm[osc] = NTPotatoChips::inputBus(busFrames, numFrames, v[voiceParam(osc, kVoiceFmInput)]);
        level[osc] = NTPotatoChips::inputBus(busFrames, numFrames, v[voiceParam(osc, kVoiceLevelInput)]);
        outputs[osc].bus = NTPotatoChips::outputBus(busFrames, numFrames, v[kParamOutput + 2 * osc]);
        outputs[osc].replace = v[kParamOutput + 2 * osc + 1];
    }
    const float* active = NTPotatoChips::inputBus(busFrames, numFrames, v[kParamActiveOscillatorsInput]);
    const float* waveform = NTPotatoChips::inputBus(busFrames, numFrames, v[kParamWaveformInput]);

    float samples[OSC_COUNT];
    for (unsigned frame = 0; frame < numFrames; frame++) {
        // audio rate: the pitch of each oscillator, which occupies three
        // registers per voice
        for (unsigned osc = 0; osc < OSC_COUNT; osc++) {
            const float pitchCV = NTPotatoChips::voltage(voct[osc], frame, 0.f);
            // the FM input normals to 5V so that the attenuverter alone can
            // offset the pitch, as it does in the Rack build
            const float fmCV = NTPotatoChips::voltage(fm[osc], frame, 5.f);
            const uint32_t freq = getFrequency(pThis, osc, pitchCV, fmCV);
            const unsigned offset = Chip::REGS_PER_VOICE * osc;
            pThis->voice.apu.write(Chip::FREQ_LOW + offset, freq & 0xff);
            pThis->voice.apu.write(Chip::FREQ_MEDIUM + offset, (freq >> 8) & 0xff);
            pThis->voice.apu.write(Chip::FREQ_HIGH + offset, (freq >> 16) & 0xff);
        }

        // control rate: the levels, the active voice count and the wave RAM
        if (pThis->voice.isControlRate()) {
            const uint8_t activeOscillators = getActiveOscillators(
                pThis, NTPotatoChips::voltage(active, frame, 0.f)
            );
            for (unsigned osc = 0; osc < OSC_COUNT; osc++) {
                const unsigned offset = Chip::REGS_PER_VOICE * osc;
                pThis->voice.apu.write(Chip::WAVE_ADDRESS + offset, 0);
                // the last oscillator's register also selects how many
                // oscillators the chip runs; writing it to every voice is
                // harmless and saves branching
                pThis->voice.apu.write(
                    Chip::VOLUME + offset,
                    ((activeOscillators - 1) << 4) | getVolume(
                        v[voiceParam(osc, kVoiceLevel)],
                        NTPotatoChips::voltage(level[osc], frame, 10.f)
                    )
                );
            }
            const float position = getWavetablePosition(
                pThis, NTPotatoChips::voltage(waveform, frame, 0.f)
            );
            if (position != pThis->wavetablePosition) {
                setWavetable(pThis->voice.apu, position);
                pThis->wavetablePosition = position;
            }
        }

        pThis->voice.advance(samples);
        // the Rack build normals unpatched outputs into the following voice
        NTPotatoChips::writeFrame(samples, outputs, OSC_COUNT, frame, true);
    }
}

static const _NT_factory factory = {
    .guid = NT_MULTICHAR( 'P', 'C', '6', '3' ),
    .name = "Name Corp Octal Wave Generator",
    .description = "Namco 163 (Nintendo Entertainment System)",
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
