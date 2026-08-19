// A Sony S-DSP BRR sample player algorithm for the disting NT.
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
#include "dsp/sony_s_dsp/brr_sample_player.hpp"
#include "dsp/sony_s_dsp/hyaw_sample.hpp"
#include "dsp/trigger/threshold.hpp"

/// the sample player this algorithm hosts
typedef SonyS_DSP::BRR_SamplePlayer Player;

/// the number of voices in the algorithm
static constexpr unsigned NUM_VOICES = 8;

// ---------------------------------------------------------------------------
// MARK: Parameters
// ---------------------------------------------------------------------------

/// the parameters of one voice, in the order they are declared
enum {
    kVoiceCoarse,
    kVoiceFine,
    kVoiceVOct,
    kVoiceFmInput,
    kVoiceGate,
    kVoicePhaseMod,
    kVoicePhaseModInput,
    kVoiceVolumeLeft,
    kVoiceVolumeLeftInput,
    kVoiceVolumeRight,
    kVoiceVolumeRightInput,
    kVoiceOutputLeft,
    kVoiceOutputLeftMode,
    kVoiceOutputRight,
    kVoiceOutputRightMode,
    kNumVoiceParams
};

/// @brief Return the index of a parameter within a voice's block.
///
/// @param voice the index of the voice
/// @param param the index of the parameter within the voice's block
/// @returns the index of the parameter within the algorithm
///
static constexpr uint8_t voiceParam(unsigned voice, unsigned param) {
    return voice * kNumVoiceParams + param;
}

enum { kNumParameters = NUM_VOICES * kNumVoiceParams };

static char const * const enumStringsOffOn[] = { "Off", "On" };

/// @brief Declare the parameters of one voice.
/// @param NAME the display prefix for the voice, e.g., "Voice 1"
#define VOICE_PARAMETERS( NAME ) \
    { .name = NAME " coarse", .min = -72, .max = 72, .def = 48, .unit = kNT_unitSemitones, .scaling = 0, .enumStrings = NULL }, \
    { .name = NAME " fine", .min = -100, .max = 100, .def = 0, .unit = kNT_unitCents, .scaling = 0, .enumStrings = NULL }, \
    NT_PARAMETER_CV_INPUT( NAME " V/Oct", 0, 0 ) \
    NT_PARAMETER_CV_INPUT( NAME " FM input", 0, 0 ) \
    NT_PARAMETER_CV_INPUT( NAME " gate", 0, 0 ) \
    { .name = NAME " phase mod", .min = 0, .max = 1, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = enumStringsOffOn }, \
    NT_PARAMETER_CV_INPUT( NAME " phase mod input", 0, 0 ) \
    { .name = NAME " level L", .min = -128, .max = 127, .def = 127, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL }, \
    NT_PARAMETER_CV_INPUT( NAME " level L input", 0, 0 ) \
    { .name = NAME " level R", .min = -128, .max = 127, .def = 127, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL }, \
    NT_PARAMETER_CV_INPUT( NAME " level R input", 0, 0 ) \
    NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE( NAME " output L", 0, 13 ) \
    NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE( NAME " output R", 0, 14 )

static const _NT_parameter parameters[] = {
    VOICE_PARAMETERS( "Voice 1" )
    VOICE_PARAMETERS( "Voice 2" )
    VOICE_PARAMETERS( "Voice 3" )
    VOICE_PARAMETERS( "Voice 4" )
    VOICE_PARAMETERS( "Voice 5" )
    VOICE_PARAMETERS( "Voice 6" )
    VOICE_PARAMETERS( "Voice 7" )
    VOICE_PARAMETERS( "Voice 8" )
};

static_assert(
    ARRAY_SIZE(parameters) == kNumParameters,
    "the parameter table and the parameter enum disagree"
);

/// @brief Declare the page contents of one voice.
/// @param VOICE the index of the voice
#define VOICE_PAGE( VOICE ) { \
    voiceParam(VOICE, kVoiceCoarse), voiceParam(VOICE, kVoiceFine), \
    voiceParam(VOICE, kVoiceVOct), voiceParam(VOICE, kVoiceFmInput), \
    voiceParam(VOICE, kVoiceGate), \
    voiceParam(VOICE, kVoicePhaseMod), voiceParam(VOICE, kVoicePhaseModInput), \
    voiceParam(VOICE, kVoiceVolumeLeft), voiceParam(VOICE, kVoiceVolumeLeftInput), \
    voiceParam(VOICE, kVoiceVolumeRight), voiceParam(VOICE, kVoiceVolumeRightInput), \
    voiceParam(VOICE, kVoiceOutputLeft), voiceParam(VOICE, kVoiceOutputLeftMode), \
    voiceParam(VOICE, kVoiceOutputRight), voiceParam(VOICE, kVoiceOutputRightMode), \
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

/// @brief The state of a Super Sampler instance.
struct _superSamplerAlgorithm : public _NT_algorithm {
    /// @brief Initialize a new instance.
    ///
    /// @param ram the chip's 64KB sample RAM, allocated in DRAM
    ///
    explicit _superSamplerAlgorithm(uint8_t* ram) {
        for (unsigned voice = 0; voice < NUM_VOICES; voice++) {
            apu[voice].set_ram(ram);
            apu[voice].setWavePage(0);
            apu[voice].setWaveIndex(0);
        }
    }

    /// the sample players, which share the chip's sample RAM
    Player apu[NUM_VOICES];

    /// triggers for the gate inputs
    Trigger::Threshold gateTriggers[NUM_VOICES];

    /// triggers for the phase modulation enable inputs
    Trigger::Threshold phaseModTriggers[NUM_VOICES];
};

/// @brief Write the built-in sample into the chip's RAM in BRR format.
///
/// @param ram the chip's 64KB sample RAM
///
static void setupSourceDirectory(uint8_t* ram) {
    memset(ram, 0, Player::SIZE_OF_RAM);
    // the source directory sits at the start of RAM and points at the block
    // immediately following it
    SonyS_DSP::SourceDirectoryEntry* dir =
        reinterpret_cast<SonyS_DSP::SourceDirectoryEntry*>(&ram[0]);
    dir->start = 4;
    dir->loop = 4;
    // pack the sample into nine-byte BRR blocks of sixteen samples each
    const unsigned blocks = SonyS_DSP::HYAW_SAMPLE_LENGTH / 16;
    for (unsigned block_index = 0; block_index <= blocks; block_index++) {
        SonyS_DSP::BitRateReductionBlock* block =
            reinterpret_cast<SonyS_DSP::BitRateReductionBlock*>(
                &ram[4 + 9 * block_index]
            );
        block->header.flags.set_volume(
            SonyS_DSP::BitRateReductionBlock::MAX_VOLUME
        );
        block->header.flags.filter = 0;
        block->header.flags.is_loop = 0;
        block->header.flags.is_end = block_index + 1 >= blocks;
        for (unsigned i = 0;
                i < 2 * SonyS_DSP::BitRateReductionBlock::NUM_SAMPLES;
                i += 2) {
            // hyaw_sample() reads zero past the end of the sample, which the
            // last block runs into. The samples are signed, so the packing is
            // done unsigned: shifting a negative value left is undefined, and
            // the sign extension of a negative sample is what sets the high
            // nibble here, as it does in the Rack build.
            const unsigned hi = static_cast<unsigned>(
                SonyS_DSP::hyaw_sample(i + 16 * block_index)
            );
            const unsigned lo = static_cast<unsigned>(
                SonyS_DSP::hyaw_sample(i + 16 * block_index + 1)
            ) << 4;
            block->samples[i / 2] = static_cast<uint8_t>(hi | lo);
        }
    }
}

/// @brief Return the level register value for a voice.
///
/// @param level the level parameter for the voice
/// @param cv the level CV in volts
/// @returns the 8-bit signed level
///
static inline int8_t getVolume(int16_t level, float cv) {
    return Math::clip(level + cv, -128.f, 127.f);
}

void calculateRequirements(
    _NT_algorithmRequirements& req,
    const int32_t* specifications
) {
    req.numParameters = ARRAY_SIZE(parameters);
    req.sram = sizeof(_superSamplerAlgorithm);
    // the chip's 64KB sample RAM, shared by every voice
    req.dram = Player::SIZE_OF_RAM;
    req.dtc = 0;
    req.itc = 0;
}

_NT_algorithm* construct(
    const _NT_algorithmMemoryPtrs& ptrs,
    const _NT_algorithmRequirements& req,
    const int32_t* specifications
) {
    setupSourceDirectory(ptrs.dram);
    _superSamplerAlgorithm* alg =
        new (ptrs.sram) _superSamplerAlgorithm(ptrs.dram);
    alg->parameters = parameters;
    alg->parameterPages = &parameterPages;
    return alg;
}

void step(_NT_algorithm* self, float* busFrames, int numFramesBy4) {
    _superSamplerAlgorithm* pThis = (_superSamplerAlgorithm*)self;
    const int16_t* v = pThis->v;
    const unsigned numFrames = numFramesBy4 * 4;

    // resolve the busses once per step rather than once per frame
    const float* voct[NUM_VOICES];
    const float* fm[NUM_VOICES];
    const float* gate[NUM_VOICES];
    const float* phaseMod[NUM_VOICES];
    const float* volumeLeft[NUM_VOICES];
    const float* volumeRight[NUM_VOICES];
    float* outLeft[NUM_VOICES];
    float* outRight[NUM_VOICES];
    for (unsigned voice = 0; voice < NUM_VOICES; voice++) {
        voct[voice] = NTPotatoChips::inputBus(busFrames, numFrames, v[voiceParam(voice, kVoiceVOct)]);
        fm[voice] = NTPotatoChips::inputBus(busFrames, numFrames, v[voiceParam(voice, kVoiceFmInput)]);
        gate[voice] = NTPotatoChips::inputBus(busFrames, numFrames, v[voiceParam(voice, kVoiceGate)]);
        phaseMod[voice] = NTPotatoChips::inputBus(busFrames, numFrames, v[voiceParam(voice, kVoicePhaseModInput)]);
        volumeLeft[voice] = NTPotatoChips::inputBus(busFrames, numFrames, v[voiceParam(voice, kVoiceVolumeLeftInput)]);
        volumeRight[voice] = NTPotatoChips::inputBus(busFrames, numFrames, v[voiceParam(voice, kVoiceVolumeRightInput)]);
        outLeft[voice] = NTPotatoChips::outputBus(busFrames, numFrames, v[voiceParam(voice, kVoiceOutputLeft)]);
        outRight[voice] = NTPotatoChips::outputBus(busFrames, numFrames, v[voiceParam(voice, kVoiceOutputRight)]);
    }

    static constexpr float MAX = 32767.f;

    for (unsigned frame = 0; frame < numFrames; frame++) {
        for (unsigned voice = 0; voice < NUM_VOICES; voice++) {
            Player& apu = pThis->apu[voice];
            float octaves = NTPotatoChips::pitch(
                v[voiceParam(voice, kVoiceCoarse)],
                v[voiceParam(voice, kVoiceFine)]
            );
            octaves += NTPotatoChips::voltage(voct[voice], frame, 0.f);
            octaves += NTPotatoChips::voltage(fm[voice], frame, 0.f) / 5.f;
            apu.setFrequency(NTPotatoChips::frequency(octaves));
            apu.setVolumeLeft(getVolume(
                v[voiceParam(voice, kVoiceVolumeLeft)],
                NTPotatoChips::voltage(volumeLeft[voice], frame, 0.f)
            ));
            apu.setVolumeRight(getVolume(
                v[voiceParam(voice, kVoiceVolumeRight)],
                NTPotatoChips::voltage(volumeRight[voice], frame, 0.f)
            ));
            const bool trigger = pThis->gateTriggers[voice].process(
                NTPotatoChips::triggerSignal(
                    NTPotatoChips::voltage(gate[voice], frame, 0.f)
                )
            );
            pThis->phaseModTriggers[voice].process(
                NTPotatoChips::triggerSignal(
                    NTPotatoChips::voltage(phaseMod[voice], frame, 0.f)
                )
            );
            // the input inverts the parameter rather than overriding it. The
            // first voice has no preceding voice to modulate it, so it never
            // takes phase modulation.
            const bool enabled = v[voiceParam(voice, kVoicePhaseMod)];
            const bool isPhaseMod = voice &&
                (enabled != pThis->phaseModTriggers[voice].isHigh());
            SonyS_DSP::StereoSample output;
            apu.run(
                output,
                trigger,
                pThis->gateTriggers[voice].isHigh(),
                isPhaseMod ? pThis->apu[voice - 1].getOutput() : 0
            );
            NTPotatoChips::write(
                outLeft[voice],
                frame,
                Math::Eurorack::toAC(output.samples[0] / MAX),
                v[voiceParam(voice, kVoiceOutputLeftMode)]
            );
            NTPotatoChips::write(
                outRight[voice],
                frame,
                Math::Eurorack::toAC(output.samples[1] / MAX),
                v[voiceParam(voice, kVoiceOutputRightMode)]
            );
        }
    }
}

static const _NT_factory factory = {
    .guid = NT_MULTICHAR( 'P', 'C', 's', 'p' ),
    .name = "Super Sampler",
    .description = "Sony S-DSP BRR sample player, eight voices (Nintendo SNES)",
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
