// A Nintendo GameBoy Sound System chip algorithm for the disting NT.
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
#include "dsp/nintendo_gameboy.hpp"
#include "dsp/trigger/threshold.hpp"
#include "dsp/wavetable4bit.hpp"

using NTPotatoChips::ChipVoice;
using NTPotatoChips::Output;

/// the chip emulator this algorithm hosts
typedef NintendoGBS Chip;

/// the number of oscillators on the chip
static constexpr unsigned OSC_COUNT = Chip::OSC_COUNT;

/// the number of pulse generators on the chip
static constexpr unsigned PULSE_COUNT = 2;

/// the number of oscillators with a frequency register
static constexpr unsigned PITCHED_COUNT = 3;

/// the number of samples in a wave-table
static constexpr unsigned SAMPLES_PER_WAVETABLE = 32;

/// the number of wave-tables the wave generator morphs between
static constexpr unsigned NUM_WAVEFORMS = 5;

/// the volume level to render the chip at
static constexpr float VOLUME = 3.f;

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
/// @param osc the index of the oscillator
/// @param param the index of the parameter within the oscillator's block
/// @returns the index of the parameter within the algorithm
///
static constexpr uint8_t voiceParam(unsigned osc, unsigned param) {
    return osc * kNumVoiceParams + param;
}

/// the parameters that follow the three pitched oscillator blocks
enum {
    kParamPulse1Width = PITCHED_COUNT * kNumVoiceParams,
    kParamPulse1WidthInput,
    kParamPulse2Width,
    kParamPulse2WidthInput,
    kParamWaveform,
    kParamWaveformInput,
    kParamNoisePeriod,
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

static char const * const enumStringsDuty[] = {
    "12.5%",
    "25%",
    "50%",
    "75%",
};

/// @brief Declare the seven parameters shared by the pitched oscillators.
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
    VOICE_PARAMETERS( "Pulse 1", 15, 10 )
    VOICE_PARAMETERS( "Pulse 2", 15, 10 )
    // the wave generator's level register selects one of four attenuations
    VOICE_PARAMETERS( "Wave", 3, 3 )
    { .name = "Pulse 1 duty", .min = 0, .max = 3, .def = 2, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = enumStringsDuty },
    NT_PARAMETER_CV_INPUT( "Pulse 1 width input", 0, 0 )
    { .name = "Pulse 2 duty", .min = 0, .max = 3, .def = 2, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = enumStringsDuty },
    NT_PARAMETER_CV_INPUT( "Pulse 2 width input", 0, 0 )
    // the wave generator morphs between the five built-in wave-tables; the
    // fractional part interpolates between neighbouring tables
    { .name = "Waveform", .min = 100, .max = 500, .def = 100, .unit = kNT_unitNone, .scaling = kNT_scaling100, .enumStrings = NULL },
    NT_PARAMETER_CV_INPUT( "Waveform input", 0, 0 )
    { .name = "Noise period", .min = 0, .max = 7, .def = 0, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL },
    NT_PARAMETER_CV_INPUT( "Noise period input", 0, 0 )
    { .name = "LFSR", .min = 0, .max = 1, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = enumStringsOffOn },
    NT_PARAMETER_CV_INPUT( "LFSR input", 0, 0 )
    { .name = "Noise level", .min = 0, .max = 15, .def = 10, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL },
    NT_PARAMETER_CV_INPUT( "Noise level input", 0, 0 )
    NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE( "Pulse 1 output", 0, 13 )
    NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE( "Pulse 2 output", 0, 14 )
    NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE( "Wave output", 0, 15 )
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

static const uint8_t pageWave[] = {
    voiceParam(2, kVoiceCoarse), voiceParam(2, kVoiceFine), voiceParam(2, kVoiceVOct),
    voiceParam(2, kVoiceFm), voiceParam(2, kVoiceFmInput),
    voiceParam(2, kVoiceLevel), voiceParam(2, kVoiceLevelInput),
    kParamWaveform, kParamWaveformInput,
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
    { .name = "Pulse 1", .numParams = ARRAY_SIZE(pagePulse1), .params = pagePulse1 },
    { .name = "Pulse 2", .numParams = ARRAY_SIZE(pagePulse2), .params = pagePulse2 },
    { .name = "Wave", .numParams = ARRAY_SIZE(pageWave), .params = pageWave },
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

/// the wave-tables the wave generator morphs between
static uint8_t const * const WAVETABLES[NUM_WAVEFORMS] = {
    SINE,
    PW5,
    RAMP_UP,
    TRIANGLE_DIST,
    RAMP_DOWN
};

/// @brief The state of a Pallet Town Waves System instance.
struct _palletTownAlgorithm : public _NT_algorithm {
    /// @brief Initialize a new instance.
    _palletTownAlgorithm() : voice(VOLUME) { }

    /// the chip emulator and its BLIP buffers
    ChipVoice<Chip> voice;

    /// a trigger for handling inputs to the LFSR input
    Trigger::Threshold lfsr;

    /// the wave-table position the chip's RAM currently holds
    /// @details
    /// Rewriting the wave RAM costs sixteen register writes, so the position
    /// is cached and the RAM only refreshed when the morph actually moves.
    /// The sentinel is outside the parameter's range so that the first
    /// control-rate update always writes.
    float wavetablePosition = -1.f;
};

/// @brief Return the 11-bit frequency register value for an oscillator.
///
/// @param self the algorithm to read parameters from
/// @param osc the index of the oscillator
/// @param voct the pitch CV in volts
/// @param fm the frequency modulation CV in volts
/// @returns the 11-bit frequency in a 16-bit container
///
static inline uint16_t getFrequency(
    const _palletTownAlgorithm* self,
    unsigned osc,
    float voct,
    float fm
) {
    const int16_t* v = self->v;
    float octaves = NTPotatoChips::pitch(
        v[voiceParam(osc, kVoiceCoarse)], v[voiceParam(osc, kVoiceFine)]
    );
    octaves += voct;
    octaves += (v[voiceParam(osc, kVoiceFm)] / 100.f) * fm / 5.f;
    const float freq = NTPotatoChips::frequency(octaves);
    // this chip counts down from a fixed period rather than up from zero
    const float period = 2048.f -
        (static_cast<uint32_t>(NTPotatoChips::CLOCK_RATE / freq) >> 5);
    return Math::clip(period, 8.f, 2035.f);
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

/// @brief Return the level register value for an oscillator.
///
/// @param level the level parameter for the oscillator
/// @param cv the level CV in volts
/// @param isWave true for the wave generator, which codes its level
/// differently from the pulse and noise generators
/// @returns the level, positioned within an 8-bit register value
///
static inline uint8_t getVolume(int16_t level, float cv, bool isWave) {
    const float max = isWave ? 3.f : 15.f;
    const uint8_t volume = Math::clip(
        roundf(level * Math::Eurorack::fromDC(cv)), 0.f, max
    );
    if (isWave) {
        // the wave generator's two-bit code attenuates rather than amplifies:
        // 00 is silent, 01 is full volume, 10 is 50% and 11 is 25%. Invert the
        // parameter so that it reads as [0, 25, 50, 100]%, and shift the two
        // bits into position 5.
        return volume == 3 ? 1 << 5 : static_cast<uint8_t>(4 - volume) << 5;
    }
    // the level occupies the high four bits of the register
    return volume << 4;
}

/// @brief Return the period register value for the noise generator.
///
/// @param self the algorithm to read parameters from
/// @param bus the noise period input bus, which may be NULL
/// @param frame the index of the frame being rendered
/// @returns the 3-bit clock shift for the noise generator
///
static inline uint8_t getNoisePeriod(
    const _palletTownAlgorithm* self,
    const float* bus,
    unsigned frame
) {
    // the maximal value for the period register
    static constexpr float MAX = 7;
    float freq = self->v[kParamNoisePeriod];
    // unlike the other CV inputs this one does not normal to a fixed voltage;
    // an unrouted input leaves the parameter alone
    if (bus) freq += bus[frame] / 2.f;
    // invert the parameter so that larger values have higher frequencies
    return MAX - Math::clip(floorf(freq), 0.f, MAX);
}

/// @brief Return the position of the wave generator within the wave-tables.
///
/// @param self the algorithm to read parameters from
/// @param cv the waveform CV in volts
/// @returns the fractional index of the wave-table in [0, NUM_WAVEFORMS - 1]
///
static inline float getWavetablePosition(
    const _palletTownAlgorithm* self,
    float cv
) {
    const float param = self->v[kParamWaveform] / 100.f;
    // rescale from a 7V range onto the parameter's span
    const float mod = NTPotatoChips::rescale(cv, -7.f, 7.f, -5.f, 5.f);
    // the wave-tables are numbered from one on the panel, so subtract one
    return Math::clip(param + mod, 1.f, 5.f) - 1.f;
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
        const uint8_t nibbleHi = (1.f - interpolate) * WAVETABLES[table0][sample] +
            interpolate * WAVETABLES[table1][sample];
        const uint8_t nibbleLo = (1.f - interpolate) * WAVETABLES[table0][sample + 1] +
            interpolate * WAVETABLES[table1][sample + 1];
        apu.write(Chip::WAVE_TABLE_VALUES + i, (nibbleHi << 4) | nibbleLo);
    }
}

void calculateRequirements(
    _NT_algorithmRequirements& req,
    const int32_t* specifications
) {
    req.numParameters = ARRAY_SIZE(parameters);
    req.sram = sizeof(_palletTownAlgorithm);
    req.dram = 0;
    req.dtc = 0;
    req.itc = 0;
}

_NT_algorithm* construct(
    const _NT_algorithmMemoryPtrs& ptrs,
    const _NT_algorithmRequirements& req,
    const int32_t* specifications
) {
    _palletTownAlgorithm* alg = new (ptrs.sram) _palletTownAlgorithm();
    alg->parameters = parameters;
    alg->parameterPages = &parameterPages;
    return alg;
}

void step(_NT_algorithm* self, float* busFrames, int numFramesBy4) {
    _palletTownAlgorithm* pThis = (_palletTownAlgorithm*)self;
    const int16_t* v = pThis->v;
    const unsigned numFrames = numFramesBy4 * 4;

    // resolve the busses once per step rather than once per frame
    const float* voct[PITCHED_COUNT];
    const float* fm[PITCHED_COUNT];
    const float* level[OSC_COUNT];
    const float* width[PULSE_COUNT];
    Output outputs[OSC_COUNT];
    for (unsigned osc = 0; osc < PITCHED_COUNT; osc++) {
        voct[osc] = NTPotatoChips::inputBus(busFrames, numFrames, v[voiceParam(osc, kVoiceVOct)]);
        fm[osc] = NTPotatoChips::inputBus(busFrames, numFrames, v[voiceParam(osc, kVoiceFmInput)]);
        level[osc] = NTPotatoChips::inputBus(busFrames, numFrames, v[voiceParam(osc, kVoiceLevelInput)]);
    }
    level[Chip::NOISE] = NTPotatoChips::inputBus(busFrames, numFrames, v[kParamNoiseLevelInput]);
    width[0] = NTPotatoChips::inputBus(busFrames, numFrames, v[kParamPulse1WidthInput]);
    width[1] = NTPotatoChips::inputBus(busFrames, numFrames, v[kParamPulse2WidthInput]);
    for (unsigned osc = 0; osc < OSC_COUNT; osc++) {
        outputs[osc].bus = NTPotatoChips::outputBus(busFrames, numFrames, v[kParamOutput + 2 * osc]);
        outputs[osc].replace = v[kParamOutput + 2 * osc + 1];
    }
    const float* waveform = NTPotatoChips::inputBus(busFrames, numFrames, v[kParamWaveformInput]);
    const float* noisePeriod = NTPotatoChips::inputBus(busFrames, numFrames, v[kParamNoisePeriodInput]);
    const float* lfsrInput = NTPotatoChips::inputBus(busFrames, numFrames, v[kParamLfsrInput]);

    // the duty cycle parameters of the two pulse generators
    static const uint8_t dutyParams[PULSE_COUNT] = {
        kParamPulse1Width, kParamPulse2Width
    };

    float samples[OSC_COUNT];
    for (unsigned frame = 0; frame < numFrames; frame++) {
        // audio rate: the pitch of each pitched oscillator, which occupies two
        // registers per voice
        for (unsigned osc = 0; osc < PITCHED_COUNT; osc++) {
            const float pitchCV = NTPotatoChips::voltage(voct[osc], frame, 0.f);
            // the FM input normals to 5V so that the attenuverter alone can
            // offset the pitch, as it does in the Rack build
            const float fmCV = NTPotatoChips::voltage(fm[osc], frame, 5.f);
            const uint16_t freq = getFrequency(pThis, osc, pitchCV, fmCV);
            // the wave generator's registers are not part of the pulse array
            const bool isPulse = osc < PULSE_COUNT;
            const unsigned lo = isPulse
                ? Chip::PULSE0_FREQ_LO + Chip::REGS_PER_VOICE * osc
                : Chip::WAVE_FREQ_LO;
            const unsigned hi = isPulse
                ? Chip::PULSE0_TRIG_LENGTH_ENABLE_HI + Chip::REGS_PER_VOICE * osc
                : Chip::WAVE_TRIG_LENGTH_ENABLE_FREQ_HI;
            pThis->voice.apu.write(lo, freq & 0xff);
            // bit 7 keeps the oscillator triggered
            pThis->voice.apu.write(hi, 0x80 | ((freq >> 8) & 0x07));
        }

        // control rate: everything else
        if (pThis->voice.isControlRate()) {
            // keep the chip powered and both stereo halves open
            pThis->voice.apu.write(Chip::POWER_CONTROL_STATUS, 0x80);
            pThis->voice.apu.write(Chip::STEREO_ENABLES, 0xff);
            pThis->voice.apu.write(Chip::STEREO_VOLUME, 0xff);

            for (unsigned osc = 0; osc < PULSE_COUNT; osc++) {
                pThis->voice.apu.write(
                    Chip::PULSE0_DUTY_LENGTH_LOAD + Chip::REGS_PER_VOICE * osc,
                    getPulseWidth(
                        v[dutyParams[osc]],
                        NTPotatoChips::voltage(width[osc], frame, 0.f)
                    )
                );
                pThis->voice.apu.write(
                    Chip::PULSE0_START_VOLUME + Chip::REGS_PER_VOICE * osc,
                    getVolume(
                        v[voiceParam(osc, kVoiceLevel)],
                        NTPotatoChips::voltage(level[osc], frame, 10.f),
                        false
                    )
                );
            }

            pThis->voice.apu.write(Chip::WAVE_DAC_POWER, 0x80);
            pThis->voice.apu.write(
                Chip::WAVE_VOLUME_CODE,
                getVolume(
                    v[voiceParam(Chip::WAVETABLE, kVoiceLevel)],
                    NTPotatoChips::voltage(level[Chip::WAVETABLE], frame, 10.f),
                    true
                )
            );

            pThis->lfsr.process(
                NTPotatoChips::triggerSignal(
                    NTPotatoChips::voltage(lfsrInput, frame, 0.f)
                )
            );
            // the input inverts the parameter rather than overriding it
            const bool enabled = v[kParamLfsr];
            const bool isLfsr = enabled != pThis->lfsr.isHigh();
            const uint8_t clockShift =
                (isLfsr << 3) | getNoisePeriod(pThis, noisePeriod, frame);
            if (pThis->voice.apu.read(Chip::NOISE_CLOCK_SHIFT) != clockShift) {
                pThis->voice.apu.write(Chip::NOISE_CLOCK_SHIFT, clockShift);
                pThis->voice.apu.write(Chip::NOISE_TRIG_LENGTH_ENABLE, 0x80);
            }
            const uint8_t noiseVolume = getVolume(
                v[kParamNoiseLevel],
                NTPotatoChips::voltage(level[Chip::NOISE], frame, 10.f),
                false
            );
            if (pThis->voice.apu.read(Chip::NOISE_START_VOLUME) != noiseVolume) {
                pThis->voice.apu.write(Chip::NOISE_START_VOLUME, noiseVolume);
                // the generator has to be re-triggered when its volume changes
                pThis->voice.apu.write(Chip::NOISE_TRIG_LENGTH_ENABLE, 0x80);
            } else if (pThis->voice.apu.read(Chip::NOISE_TRIG_LENGTH_ENABLE) != 0x80) {
                // triggering resets the phase of the noise, so only enable the
                // generator when it is not already enabled
                pThis->voice.apu.write(Chip::NOISE_TRIG_LENGTH_ENABLE, 0x80);
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
    .guid = NT_MULTICHAR( 'P', 'C', 'g', 'b' ),
    .name = "Pallet Town Waves System",
    .description = "Nintendo GameBoy Sound System",
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
