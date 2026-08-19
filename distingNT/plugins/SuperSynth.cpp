// A Sony S-DSP synthesizer algorithm for the disting NT.
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
#include "dsp/sony_s_dsp/processor.hpp"
#include "dsp/trigger/threshold.hpp"

/// the chip emulator this algorithm hosts
typedef SonyS_DSP::Processor Chip;

/// the number of voices on the chip
static constexpr unsigned VOICE_COUNT = Chip::VOICE_COUNT;

/// the number of coefficients in the chip's FIR filter
static constexpr unsigned FIR_COEFFICIENT_COUNT = Chip::FIR_COEFFICIENT_COUNT;

/// the length of the chip's echo buffer, which the sample directory follows
static constexpr unsigned ECHO_LENGTH = 15 * 2 * 1024;

// ---------------------------------------------------------------------------
// MARK: Parameters
// ---------------------------------------------------------------------------

/// the global parameters, which precede the voice blocks
enum {
    kParamNoiseFrequency,
    kParamEchoDelay,
    kParamEchoFeedback,
    kParamEchoVolumeLeft,
    kParamEchoVolumeRight,
    kParamMainVolumeLeft,
    kParamMainVolumeRight,
    kParamOutputLeft,
    kParamOutputLeftMode,
    kParamOutputRight,
    kParamOutputRightMode,
    /// the first of FIR_COEFFICIENT_COUNT coefficient parameters
    kParamFirCoefficient,
    kNumGlobalParams = kParamFirCoefficient + FIR_COEFFICIENT_COUNT
};

/// the parameters of one voice, in the order they are declared
enum {
    kVoiceCoarse,
    kVoiceFine,
    kVoiceVOct,
    kVoiceFmInput,
    kVoiceGate,
    kVoiceVolumeLeft,
    kVoiceVolumeRight,
    kVoiceAttack,
    kVoiceDecay,
    kVoiceSustainLevel,
    kVoiceSustainRate,
    kVoiceNoiseEnable,
    kVoiceEchoEnable,
    kVoicePhaseMod,
    kNumVoiceParams
};

/// @brief Return the index of a parameter within a voice's block.
///
/// @param voice the index of the voice
/// @param param the index of the parameter within the voice's block
/// @returns the index of the parameter within the algorithm
///
static constexpr uint8_t voiceParam(unsigned voice, unsigned param) {
    return kNumGlobalParams + voice * kNumVoiceParams + param;
}

enum { kNumParameters = kNumGlobalParams + VOICE_COUNT * kNumVoiceParams };

static char const * const enumStringsOffOn[] = { "Off", "On" };

/// @brief Declare the parameters of one voice.
/// @param NAME the display prefix for the voice, e.g., "Voice 1"
#define VOICE_PARAMETERS( NAME ) \
    { .name = NAME " coarse", .min = -48, .max = 48, .def = 24, .unit = kNT_unitSemitones, .scaling = 0, .enumStrings = NULL }, \
    { .name = NAME " fine", .min = -100, .max = 100, .def = 0, .unit = kNT_unitCents, .scaling = 0, .enumStrings = NULL }, \
    NT_PARAMETER_CV_INPUT( NAME " V/Oct", 0, 0 ) \
    NT_PARAMETER_CV_INPUT( NAME " FM input", 0, 0 ) \
    NT_PARAMETER_CV_INPUT( NAME " gate", 0, 0 ) \
    { .name = NAME " level L", .min = -128, .max = 127, .def = 127, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL }, \
    { .name = NAME " level R", .min = -128, .max = 127, .def = 127, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL }, \
    { .name = NAME " attack", .min = 0, .max = 15, .def = 0, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL }, \
    { .name = NAME " decay", .min = 0, .max = 7, .def = 0, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL }, \
    { .name = NAME " sustain", .min = 0, .max = 7, .def = 0, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL }, \
    { .name = NAME " sustain rate", .min = 0, .max = 31, .def = 0, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL }, \
    { .name = NAME " noise", .min = 0, .max = 1, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = enumStringsOffOn }, \
    { .name = NAME " echo", .min = 0, .max = 1, .def = 1, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = enumStringsOffOn }, \
    { .name = NAME " phase mod", .min = 0, .max = 1, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = enumStringsOffOn }

/// @brief Declare one FIR coefficient parameter.
/// @param NAME the display name for the coefficient
/// @param DEF the coefficient's default value
#define COEFFICIENT_PARAMETER( NAME, DEF ) \
    { .name = NAME, .min = -128, .max = 127, .def = DEF, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL },

static const _NT_parameter parameters[] = {
    { .name = "Noise frequency", .min = 0, .max = 31, .def = 16, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL },
    { .name = "Echo delay", .min = 0, .max = 15, .def = 0, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL },
    { .name = "Echo feedback", .min = -128, .max = 127, .def = 0, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL },
    { .name = "Echo level L", .min = -128, .max = 127, .def = 127, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL },
    { .name = "Echo level R", .min = -128, .max = 127, .def = 127, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL },
    { .name = "Main level L", .min = -128, .max = 127, .def = 127, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL },
    { .name = "Main level R", .min = -128, .max = 127, .def = 127, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL },
    NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE( "Output L", 0, 13 )
    NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE( "Output R", 0, 14 )
    // the emulator's own defaults: an impulse, i.e., a transparent filter
    COEFFICIENT_PARAMETER( "FIR 1", 127 )
    COEFFICIENT_PARAMETER( "FIR 2", 0 )
    COEFFICIENT_PARAMETER( "FIR 3", 0 )
    COEFFICIENT_PARAMETER( "FIR 4", 0 )
    COEFFICIENT_PARAMETER( "FIR 5", 0 )
    COEFFICIENT_PARAMETER( "FIR 6", 0 )
    COEFFICIENT_PARAMETER( "FIR 7", 0 )
    COEFFICIENT_PARAMETER( "FIR 8", 0 )
    VOICE_PARAMETERS( "Voice 1" ),
    VOICE_PARAMETERS( "Voice 2" ),
    VOICE_PARAMETERS( "Voice 3" ),
    VOICE_PARAMETERS( "Voice 4" ),
    VOICE_PARAMETERS( "Voice 5" ),
    VOICE_PARAMETERS( "Voice 6" ),
    VOICE_PARAMETERS( "Voice 7" ),
    VOICE_PARAMETERS( "Voice 8" ),
};

static_assert(
    ARRAY_SIZE(parameters) == kNumParameters,
    "the parameter table and the parameter enum disagree"
);

static const uint8_t pageMix[] = {
    kParamMainVolumeLeft, kParamMainVolumeRight,
    kParamOutputLeft, kParamOutputLeftMode,
    kParamOutputRight, kParamOutputRightMode,
    kParamNoiseFrequency,
};

static const uint8_t pageEcho[] = {
    kParamEchoDelay, kParamEchoFeedback,
    kParamEchoVolumeLeft, kParamEchoVolumeRight,
};

static const uint8_t pageFIR[] = {
    kParamFirCoefficient + 0, kParamFirCoefficient + 1,
    kParamFirCoefficient + 2, kParamFirCoefficient + 3,
    kParamFirCoefficient + 4, kParamFirCoefficient + 5,
    kParamFirCoefficient + 6, kParamFirCoefficient + 7,
};

/// @brief Declare the page contents of one voice.
/// @param VOICE the index of the voice
#define VOICE_PAGE( VOICE ) { \
    voiceParam(VOICE, kVoiceCoarse), voiceParam(VOICE, kVoiceFine), \
    voiceParam(VOICE, kVoiceVOct), voiceParam(VOICE, kVoiceFmInput), \
    voiceParam(VOICE, kVoiceGate), \
    voiceParam(VOICE, kVoiceVolumeLeft), voiceParam(VOICE, kVoiceVolumeRight), \
    voiceParam(VOICE, kVoiceAttack), voiceParam(VOICE, kVoiceDecay), \
    voiceParam(VOICE, kVoiceSustainLevel), voiceParam(VOICE, kVoiceSustainRate), \
    voiceParam(VOICE, kVoiceNoiseEnable), voiceParam(VOICE, kVoiceEchoEnable), \
    voiceParam(VOICE, kVoicePhaseMod), \
}

static const uint8_t pageVoice1[] = VOICE_PAGE( 0 );
static const uint8_t pageVoice2[] = VOICE_PAGE( 1 );
static const uint8_t pageVoice3[] = VOICE_PAGE( 2 );
static const uint8_t pageVoice4[] = VOICE_PAGE( 3 );
static const uint8_t pageVoice5[] = VOICE_PAGE( 4 );
static const uint8_t pageVoice6[] = VOICE_PAGE( 5 );
static const uint8_t pageVoice7[] = VOICE_PAGE( 6 );
static const uint8_t pageVoice8[] = VOICE_PAGE( 7 );

static const _NT_parameterPage pages[] = {
    { .name = "Mix", .numParams = ARRAY_SIZE(pageMix), .params = pageMix },
    { .name = "Echo", .numParams = ARRAY_SIZE(pageEcho), .params = pageEcho },
    { .name = "FIR", .numParams = ARRAY_SIZE(pageFIR), .params = pageFIR },
    { .name = "Voice 1", .numParams = ARRAY_SIZE(pageVoice1), .params = pageVoice1 },
    { .name = "Voice 2", .numParams = ARRAY_SIZE(pageVoice2), .params = pageVoice2 },
    { .name = "Voice 3", .numParams = ARRAY_SIZE(pageVoice3), .params = pageVoice3 },
    { .name = "Voice 4", .numParams = ARRAY_SIZE(pageVoice4), .params = pageVoice4 },
    { .name = "Voice 5", .numParams = ARRAY_SIZE(pageVoice5), .params = pageVoice5 },
    { .name = "Voice 6", .numParams = ARRAY_SIZE(pageVoice6), .params = pageVoice6 },
    { .name = "Voice 7", .numParams = ARRAY_SIZE(pageVoice7), .params = pageVoice7 },
    { .name = "Voice 8", .numParams = ARRAY_SIZE(pageVoice8), .params = pageVoice8 },
};

static const _NT_parameterPages parameterPages = {
    .numPages = ARRAY_SIZE(pages),
    .pages = pages,
};

// ---------------------------------------------------------------------------
// MARK: Algorithm
// ---------------------------------------------------------------------------

/// @brief The state of a Super Synth instance.
struct _superSynthAlgorithm : public _NT_algorithm {
    /// @brief Initialize a new instance.
    ///
    /// @param apu_ the chip emulator, allocated in DRAM
    ///
    explicit _superSynthAlgorithm(Chip* apu_) : apu(apu_) { }

    /// the chip emulator
    /// @details
    /// The emulator and the 64KB of RAM it shares with the CPU both live in
    /// DRAM; only this handle and the gate triggers sit in SRAM.
    Chip* apu;

    /// triggers for detecting key-on and key-off events on the gate inputs
    Trigger::Threshold gateTriggers[VOICE_COUNT][2];
};

/// @brief Write the chip's registers and RAM to their initial state.
///
/// @param apu the chip emulator to set up
/// @param ram the chip's 64KB of RAM
///
static void setupSourceDirectory(Chip& apu, uint8_t* ram) {
    // the echo buffer starts at 0x8000, i.e., offset 128 pages
    apu.write(Chip::ECHO_BUFFER_START_OFFSET, 128);
    // the sample directory follows the echo buffer
    apu.write(Chip::OFFSET_SOURCE_DIRECTORY, ECHO_LENGTH / 0x100);
    // every voice plays the first sample in the directory
    for (unsigned voice = 0; voice < VOICE_COUNT; voice++)
        apu.write((voice << 4) | Chip::SOURCE_NUMBER, 0);
    // the directory's first entry points at the block that follows it
    SonyS_DSP::SourceDirectoryEntry* dir =
        reinterpret_cast<SonyS_DSP::SourceDirectoryEntry*>(&ram[ECHO_LENGTH]);
    dir->start = ECHO_LENGTH + 4;
    dir->loop = ECHO_LENGTH + 4;
    // a single looping BRR block holding one cycle of a ramp wave
    SonyS_DSP::BitRateReductionBlock* block =
        reinterpret_cast<SonyS_DSP::BitRateReductionBlock*>(
            &ram[ECHO_LENGTH + 4]
        );
    block->header.flags.set_volume(
        SonyS_DSP::BitRateReductionBlock::MAX_VOLUME
    );
    block->header.flags.filter = 0;
    block->header.flags.is_loop = 1;
    block->header.flags.is_end = 1;
    static const uint8_t samples[8] = {
        0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF
    };
    for (unsigned i = 0; i < SonyS_DSP::BitRateReductionBlock::NUM_SAMPLES; i++)
        block->samples[i] = samples[i];
}

void calculateRequirements(
    _NT_algorithmRequirements& req,
    const int32_t* specifications
) {
    req.numParameters = ARRAY_SIZE(parameters);
    req.sram = sizeof(_superSynthAlgorithm);
    // the chip's 64KB of RAM, followed by the emulator itself
    req.dram = Chip::SIZE_OF_RAM + sizeof(Chip);
    req.dtc = 0;
    req.itc = 0;
}

_NT_algorithm* construct(
    const _NT_algorithmMemoryPtrs& ptrs,
    const _NT_algorithmRequirements& req,
    const int32_t* specifications
) {
    uint8_t* ram = ptrs.dram;
    memset(ram, 0, Chip::SIZE_OF_RAM);
    Chip* apu = new (ram + Chip::SIZE_OF_RAM) Chip(ram);
    apu->reset();
    setupSourceDirectory(*apu, ram);
    _superSynthAlgorithm* alg = new (ptrs.sram) _superSynthAlgorithm(apu);
    alg->parameters = parameters;
    alg->parameterPages = &parameterPages;
    return alg;
}

void step(_NT_algorithm* self, float* busFrames, int numFramesBy4) {
    _superSynthAlgorithm* pThis = (_superSynthAlgorithm*)self;
    const int16_t* v = pThis->v;
    const unsigned numFrames = numFramesBy4 * 4;
    Chip& apu = *pThis->apu;

    // resolve the busses once per step rather than once per frame
    const float* voct[VOICE_COUNT];
    const float* fm[VOICE_COUNT];
    const float* gate[VOICE_COUNT];
    for (unsigned voice = 0; voice < VOICE_COUNT; voice++) {
        voct[voice] = NTPotatoChips::inputBus(busFrames, numFrames, v[voiceParam(voice, kVoiceVOct)]);
        fm[voice] = NTPotatoChips::inputBus(busFrames, numFrames, v[voiceParam(voice, kVoiceFmInput)]);
        gate[voice] = NTPotatoChips::inputBus(busFrames, numFrames, v[voiceParam(voice, kVoiceGate)]);
    }
    float* outLeft = NTPotatoChips::outputBus(busFrames, numFrames, v[kParamOutputLeft]);
    float* outRight = NTPotatoChips::outputBus(busFrames, numFrames, v[kParamOutputRight]);
    const bool replaceLeft = v[kParamOutputLeftMode];
    const bool replaceRight = v[kParamOutputRightMode];

    static constexpr float MAX = 32767.f;

    for (unsigned frame = 0; frame < numFrames; frame++) {
        apu.write(Chip::FLAGS, v[kParamNoiseFrequency]);

        // the gate inputs key voices on and off through bit masks
        uint8_t keyOn = 0;
        uint8_t keyOff = 0;
        for (unsigned voice = 0; voice < VOICE_COUNT; voice++) {
            const float cv = NTPotatoChips::voltage(gate[voice], frame, 0.f);
            keyOn |= pThis->gateTriggers[voice][0].process(
                NTPotatoChips::triggerSignal(cv)
            ) << voice;
            // the falling edge is detected by triggering off the inverse
            keyOff |= pThis->gateTriggers[voice][1].process(
                NTPotatoChips::triggerSignal(10.f - cv)
            ) << voice;
        }
        if (keyOn) {
            // clear the key-off register so that every voice can be keyed on
            apu.write(Chip::KEY_OFF, 0);
            apu.write(Chip::KEY_ON, keyOn);
        }
        if (keyOff) apu.write(Chip::KEY_OFF, keyOff);

        apu.write(Chip::ECHO_FEEDBACK, v[kParamEchoFeedback]);
        apu.write(Chip::ECHO_DELAY, v[kParamEchoDelay]);
        apu.write(Chip::MAIN_VOLUME_LEFT, v[kParamMainVolumeLeft]);
        apu.write(Chip::MAIN_VOLUME_RIGHT, v[kParamMainVolumeRight]);
        apu.write(Chip::ECHO_VOLUME_LEFT, v[kParamEchoVolumeLeft]);
        apu.write(Chip::ECHO_VOLUME_RIGHT, v[kParamEchoVolumeRight]);

        // the per-voice switches are packed into one register each
        uint8_t echoEnable = 0;
        uint8_t noiseEnable = 0;
        uint8_t pitchModulation = 0;
        for (unsigned voice = 0; voice < VOICE_COUNT; voice++) {
            echoEnable |= (v[voiceParam(voice, kVoiceEchoEnable)] != 0) << voice;
            noiseEnable |= (v[voiceParam(voice, kVoiceNoiseEnable)] != 0) << voice;
            // the first voice has no preceding voice to modulate it
            if (voice)
                pitchModulation |= (v[voiceParam(voice, kVoicePhaseMod)] != 0) << voice;
        }
        apu.write(Chip::ECHO_ENABLE, echoEnable);
        apu.write(Chip::NOISE_ENABLE, noiseEnable);
        apu.write(Chip::PITCH_MODULATION, pitchModulation);

        for (unsigned voice = 0; voice < VOICE_COUNT; voice++) {
            // the voice index occupies the high nibble of the register address
            const uint8_t mask = voice << 4;
            float octaves = NTPotatoChips::pitch(
                v[voiceParam(voice, kVoiceCoarse)],
                v[voiceParam(voice, kVoiceFine)]
            );
            octaves += NTPotatoChips::voltage(voct[voice], frame, 0.f);
            octaves += NTPotatoChips::voltage(fm[voice], frame, 0.f) / 5.f;
            const uint16_t pitch = SonyS_DSP::get_pitch(
                NTPotatoChips::frequency(octaves)
            );
            apu.write(mask | Chip::PITCH_LOW, pitch & 0xff);
            apu.write(mask | Chip::PITCH_HIGH, (pitch >> 8) & 0xff);
            // the high bit of ADSR_1 enables the envelope generator
            apu.write(
                mask | Chip::ADSR_1,
                0x80 |
                    (static_cast<uint8_t>(v[voiceParam(voice, kVoiceDecay)]) << 4) |
                    static_cast<uint8_t>(v[voiceParam(voice, kVoiceAttack)])
            );
            apu.write(
                mask | Chip::ADSR_2,
                (static_cast<uint8_t>(v[voiceParam(voice, kVoiceSustainLevel)]) << 5) |
                    static_cast<uint8_t>(v[voiceParam(voice, kVoiceSustainRate)])
            );
            apu.write(mask | Chip::VOLUME_LEFT, v[voiceParam(voice, kVoiceVolumeLeft)]);
            apu.write(mask | Chip::VOLUME_RIGHT, v[voiceParam(voice, kVoiceVolumeRight)]);
        }

        for (unsigned i = 0; i < FIR_COEFFICIENT_COUNT; i++) {
            // the coefficient index occupies the high nibble of the address
            apu.write(
                (i << 4) | Chip::FIR_COEFFICIENTS,
                v[kParamFirCoefficient + i]
            );
        }

        short sample[2] = { 0, 0 };
        apu.run(sample);
        NTPotatoChips::write(
            outLeft, frame, Math::Eurorack::toAC(sample[0] / MAX), replaceLeft
        );
        NTPotatoChips::write(
            outRight, frame, Math::Eurorack::toAC(sample[1] / MAX), replaceRight
        );
    }
}

static const _NT_factory factory = {
    .guid = NT_MULTICHAR( 'P', 'C', 's', 'y' ),
    .name = "Super Synth",
    .description = "Sony S-DSP, eight voices with echo (Nintendo SNES)",
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
