// The oscillators of the Mutable Instruments Edges module, for the disting NT.
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
#include "dsp/mi_edges/wavetable.hpp"

using NTPotatoChips::Output;

/// the oscillator this algorithm hosts
typedef Oscillator::MutableIntstrumentsEdges::DigitalOscillator Voice;

/// the number of voices in the algorithm
static constexpr unsigned NUM_VOICES = 4;

/// the number of shapes each voice can render
static constexpr unsigned NUM_SHAPES = Oscillator::MutableIntstrumentsEdges::NUM_SHAPES;

// ---------------------------------------------------------------------------
// MARK: Parameters
// ---------------------------------------------------------------------------

/// the parameters of one voice, in the order they are declared
enum {
    kVoiceCoarse,
    kVoiceFine,
    kVoiceVOct,
    kVoiceFm,
    kVoiceFmInput,
    kVoiceLevel,
    kVoiceLevelInput,
    kVoiceShape,
    kVoiceOutput,
    kVoiceOutputMode,
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

/// the shapes the oscillator can produce, in the order the emulator lists them
static char const * const enumStringsShape[] = {
    "Sine",
    "Triangle",
    "NES triangle",
    "Sample & hold",
    "LFSR long",
    "LFSR short",
    "Pulse 50%",
    "Pulse 66%",
    "Pulse 75%",
    "Pulse 87%",
    "Pulse 95%",
    "Pulse CV",
};

static_assert(
    ARRAY_SIZE(enumStringsShape) == NUM_SHAPES,
    "the shape names and the oscillator's shapes disagree"
);

/// @brief Declare the parameters of one voice.
/// @param NAME the display prefix for the voice, e.g., "Voice 1"
/// @param OUT the default output bus for the voice
#define VOICE_PARAMETERS( NAME, OUT ) \
    { .name = NAME " coarse", .min = -30, .max = 30, .def = 0, .unit = kNT_unitSemitones, .scaling = 0, .enumStrings = NULL }, \
    { .name = NAME " fine", .min = -100, .max = 100, .def = 0, .unit = kNT_unitCents, .scaling = 0, .enumStrings = NULL }, \
    NT_PARAMETER_CV_INPUT( NAME " V/Oct", 0, 0 ) \
    { .name = NAME " FM", .min = -100, .max = 100, .def = 0, .unit = kNT_unitNone, .scaling = kNT_scaling100, .enumStrings = NULL }, \
    NT_PARAMETER_CV_INPUT( NAME " FM input", 0, 0 ) \
    { .name = NAME " level", .min = 0, .max = 255, .def = 255, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL }, \
    NT_PARAMETER_CV_INPUT( NAME " level input", 0, 0 ) \
    { .name = NAME " shape", .min = 0, .max = NUM_SHAPES - 1, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = enumStringsShape }, \
    NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE( NAME " output", 0, OUT )

static const _NT_parameter parameters[] = {
    VOICE_PARAMETERS( "Voice 1", 13 )
    VOICE_PARAMETERS( "Voice 2", 14 )
    VOICE_PARAMETERS( "Voice 3", 15 )
    VOICE_PARAMETERS( "Voice 4", 16 )
};

static_assert(
    ARRAY_SIZE(parameters) == kNumParameters,
    "the parameter table and the parameter enum disagree"
);

/// @brief Declare the page contents of one voice.
/// @param VOICE the index of the voice
#define VOICE_PAGE( VOICE ) { \
    voiceParam(VOICE, kVoiceCoarse), voiceParam(VOICE, kVoiceFine), \
    voiceParam(VOICE, kVoiceVOct), \
    voiceParam(VOICE, kVoiceFm), voiceParam(VOICE, kVoiceFmInput), \
    voiceParam(VOICE, kVoiceLevel), voiceParam(VOICE, kVoiceLevelInput), \
    voiceParam(VOICE, kVoiceShape), \
    voiceParam(VOICE, kVoiceOutput), voiceParam(VOICE, kVoiceOutputMode), \
}

static const uint8_t pageVoice1[] = VOICE_PAGE( 0 );
static const uint8_t pageVoice2[] = VOICE_PAGE( 1 );
static const uint8_t pageVoice3[] = VOICE_PAGE( 2 );
static const uint8_t pageVoice4[] = VOICE_PAGE( 3 );

static const _NT_parameterPage pages[] = {
    { .name = "Voice 1", .numParams = ARRAY_SIZE(pageVoice1), .params = pageVoice1 },
    { .name = "Voice 2", .numParams = ARRAY_SIZE(pageVoice2), .params = pageVoice2 },
    { .name = "Voice 3", .numParams = ARRAY_SIZE(pageVoice3), .params = pageVoice3 },
    { .name = "Voice 4", .numParams = ARRAY_SIZE(pageVoice4), .params = pageVoice4 },
};

static const _NT_parameterPages parameterPages = {
    .numPages = ARRAY_SIZE(pages),
    .pages = pages,
};

// ---------------------------------------------------------------------------
// MARK: Algorithm
// ---------------------------------------------------------------------------

/// @brief The state of a Blocks instance.
struct _blocksAlgorithm : public _NT_algorithm {
    /// @brief Initialize a new instance.
    _blocksAlgorithm() { }

    /// the oscillators
    Voice voice[NUM_VOICES];
};

void calculateRequirements(
    _NT_algorithmRequirements& req,
    const int32_t* specifications
) {
    req.numParameters = ARRAY_SIZE(parameters);
    req.sram = sizeof(_blocksAlgorithm);
    req.dram = 0;
    req.dtc = 0;
    req.itc = 0;
}

_NT_algorithm* construct(
    const _NT_algorithmMemoryPtrs& ptrs,
    const _NT_algorithmRequirements& req,
    const int32_t* specifications
) {
    _blocksAlgorithm* alg = new (ptrs.sram) _blocksAlgorithm();
    alg->parameters = parameters;
    alg->parameterPages = &parameterPages;
    return alg;
}

void step(_NT_algorithm* self, float* busFrames, int numFramesBy4) {
    _blocksAlgorithm* pThis = (_blocksAlgorithm*)self;
    const int16_t* v = pThis->v;
    const unsigned numFrames = numFramesBy4 * 4;
    const float sampleTime = 1.f / NT_globals.sampleRate;

    // resolve the busses once per step rather than once per frame
    const float* voct[NUM_VOICES];
    const float* fm[NUM_VOICES];
    const float* level[NUM_VOICES];
    Output outputs[NUM_VOICES];
    for (unsigned voice = 0; voice < NUM_VOICES; voice++) {
        voct[voice] = NTPotatoChips::inputBus(busFrames, numFrames, v[voiceParam(voice, kVoiceVOct)]);
        fm[voice] = NTPotatoChips::inputBus(busFrames, numFrames, v[voiceParam(voice, kVoiceFmInput)]);
        level[voice] = NTPotatoChips::inputBus(busFrames, numFrames, v[voiceParam(voice, kVoiceLevelInput)]);
        outputs[voice].bus = NTPotatoChips::outputBus(busFrames, numFrames, v[voiceParam(voice, kVoiceOutput)]);
        outputs[voice].replace = v[voiceParam(voice, kVoiceOutputMode)];
        pThis->voice[voice].setShape(
            static_cast<Voice::Shape>(v[voiceParam(voice, kVoiceShape)])
        );
    }

    float samples[NUM_VOICES];
    for (unsigned frame = 0; frame < numFrames; frame++) {
        for (unsigned voice = 0; voice < NUM_VOICES; voice++) {
            float octaves = NTPotatoChips::pitch(
                v[voiceParam(voice, kVoiceCoarse)],
                v[voiceParam(voice, kVoiceFine)]
            );
            octaves += NTPotatoChips::voltage(voct[voice], frame, 0.f);
            // the FM input normals to 5V so that the attenuverter alone can
            // offset the pitch, as it does in the Rack build
            const float attenuverter = v[voiceParam(voice, kVoiceFm)] / 100.f;
            const float modulation = NTPotatoChips::voltage(fm[voice], frame, 5.f);
            // the hardware routes the modulation CV to the width of the pulse,
            // in place of the pitch, when the width is CV controlled
            if (pThis->voice[voice].isPulseWidthCV()) {
                // center the width on a square, as the Rack build does
                const float width = 0.5f + attenuverter *
                    (Math::Eurorack::fromDC(modulation) - 0.5f);
                pThis->voice[voice].setPulseWidth(
                    static_cast<uint8_t>(roundf(255 * Math::clip(width, 0.f, 1.f)))
                );
            } else {
                octaves += attenuverter * modulation / 5.f;
            }
            pThis->voice[voice].setFrequency(NTPotatoChips::frequency(octaves));
            pThis->voice[voice].process(sampleTime);
            // the level input normals to 10V, i.e., unity
            const float gain = Math::clip(
                roundf(
                    v[voiceParam(voice, kVoiceLevel)] * Math::Eurorack::fromDC(
                        NTPotatoChips::voltage(level[voice], frame, 10.f)
                    )
                ) / 255.f,
                0.f,
                1.f
            );
            samples[voice] = gain * pThis->voice[voice].getValue();
        }
        // the Rack build normals unpatched outputs into the following voice
        NTPotatoChips::writeFrame(samples, outputs, NUM_VOICES, frame, true);
    }
}

static const _NT_factory factory = {
    .guid = NT_MULTICHAR( 'P', 'C', 'b', 'l' ),
    .name = "Blocks",
    .description = "The Mutable Instruments Edges oscillators",
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
