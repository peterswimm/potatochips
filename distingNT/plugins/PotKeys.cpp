// An Atari POKEY chip algorithm for the disting NT.
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
#include "dsp/atari_pokey.hpp"
#include "dsp/trigger/threshold.hpp"

using NTPotatoChips::ChipVoice;
using NTPotatoChips::Output;

/// the chip emulator this algorithm hosts
typedef AtariPOKEY Chip;

/// the number of oscillators on the chip
static constexpr unsigned OSC_COUNT = Chip::OSC_COUNT;

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
    kToneNoise,
    kToneNoiseInput,
    kToneLevel,
    kToneLevelInput,
    kNumToneParams
};

/// @brief Return the index of a parameter within a tone generator's block.
///
/// @param osc the index of the tone generator
/// @param param the index of the parameter within the generator's block
/// @returns the index of the parameter within the algorithm
///
static constexpr uint8_t tone(unsigned osc, unsigned param) {
    return osc * kNumToneParams + param;
}

/// the number of control register flags this algorithm exposes
/// @details
/// The two flags that join oscillators into 16-bit pairs are omitted, as they
/// are in the Rack build.
static constexpr unsigned CONTROL_COUNT = 6;

/// the parameters of one control register flag, in declaration order
enum {
    kControlEnabled,
    kControlInput,
    kNumControlParams
};

/// @brief Return the index of a parameter within a control flag's block.
///
/// @param control the index of the control flag among those exposed
/// @param param the index of the parameter within the flag's block
/// @returns the index of the parameter within the algorithm
///
static constexpr uint8_t controlParam(unsigned control, unsigned param) {
    return OSC_COUNT * kNumToneParams + control * kNumControlParams + param;
}

/// the parameters that follow the tone and control blocks
enum {
    /// the first of OSC_COUNT (bus, mode) output parameter pairs
    kParamOutput =
        OSC_COUNT * kNumToneParams + CONTROL_COUNT * kNumControlParams,
    kNumParameters = kParamOutput + 2 * OSC_COUNT
};

/// the bit each exposed control flag occupies in the control register
static const uint8_t CONTROL_BITS[CONTROL_COUNT] = { 0, 1, 2, 5, 6, 7 };

static char const * const enumStringsOffOn[] = { "Off", "On" };

/// @brief Declare the nine parameters of one tone generator.
/// @param NAME the display prefix for the generator, e.g., "Tone 1"
#define TONE_PARAMETERS( NAME ) \
    { .name = NAME " coarse", .min = -30, .max = 30, .def = 0, .unit = kNT_unitSemitones, .scaling = 0, .enumStrings = NULL }, \
    { .name = NAME " fine", .min = -100, .max = 100, .def = 0, .unit = kNT_unitCents, .scaling = 0, .enumStrings = NULL }, \
    NT_PARAMETER_CV_INPUT( NAME " V/Oct", 0, 0 ) \
    { .name = NAME " FM", .min = -100, .max = 100, .def = 0, .unit = kNT_unitNone, .scaling = kNT_scaling100, .enumStrings = NULL }, \
    NT_PARAMETER_CV_INPUT( NAME " FM input", 0, 0 ) \
    { .name = NAME " noise", .min = 0, .max = 7, .def = 7, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL }, \
    NT_PARAMETER_CV_INPUT( NAME " noise input", 0, 0 ) \
    { .name = NAME " level", .min = 0, .max = 15, .def = 7, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL }, \
    NT_PARAMETER_CV_INPUT( NAME " level input", 0, 0 )

/// @brief Declare the two parameters of one control register flag.
/// @param NAME the display name for the flag
#define CONTROL_PARAMETERS( NAME ) \
    { .name = NAME, .min = 0, .max = 1, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = enumStringsOffOn }, \
    NT_PARAMETER_CV_INPUT( NAME " input", 0, 0 )

static const _NT_parameter parameters[] = {
    TONE_PARAMETERS( "Tone 1" )
    TONE_PARAMETERS( "Tone 2" )
    TONE_PARAMETERS( "Tone 3" )
    TONE_PARAMETERS( "Tone 4" )
    CONTROL_PARAMETERS( "Low frequency" )
    CONTROL_PARAMETERS( "HP tone 2 from 4" )
    CONTROL_PARAMETERS( "HP tone 1 from 3" )
    CONTROL_PARAMETERS( "Tone 3 fast" )
    CONTROL_PARAMETERS( "Tone 1 fast" )
    CONTROL_PARAMETERS( "LFSR" )
    NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE( "Tone 1 output", 0, 13 )
    NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE( "Tone 2 output", 0, 14 )
    NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE( "Tone 3 output", 0, 15 )
    NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE( "Tone 4 output", 0, 16 )
};

static_assert(
    ARRAY_SIZE(parameters) == kNumParameters,
    "the parameter table and the parameter enum disagree"
);

/// @brief Declare the page contents of one tone generator.
/// @param OSC the index of the tone generator
#define TONE_PAGE( OSC ) { \
    tone(OSC, kToneCoarse), tone(OSC, kToneFine), tone(OSC, kToneVOct), \
    tone(OSC, kToneFm), tone(OSC, kToneFmInput), \
    tone(OSC, kToneNoise), tone(OSC, kToneNoiseInput), \
    tone(OSC, kToneLevel), tone(OSC, kToneLevelInput), \
}

static const uint8_t pageTone1[] = TONE_PAGE( 0 );
static const uint8_t pageTone2[] = TONE_PAGE( 1 );
static const uint8_t pageTone3[] = TONE_PAGE( 2 );
static const uint8_t pageTone4[] = TONE_PAGE( 3 );

static const uint8_t pageControl[] = {
    controlParam(0, kControlEnabled), controlParam(0, kControlInput),
    controlParam(1, kControlEnabled), controlParam(1, kControlInput),
    controlParam(2, kControlEnabled), controlParam(2, kControlInput),
    controlParam(3, kControlEnabled), controlParam(3, kControlInput),
    controlParam(4, kControlEnabled), controlParam(4, kControlInput),
    controlParam(5, kControlEnabled), controlParam(5, kControlInput),
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
    { .name = "Tone 4", .numParams = ARRAY_SIZE(pageTone4), .params = pageTone4 },
    { .name = "Control", .numParams = ARRAY_SIZE(pageControl), .params = pageControl },
    { .name = "Routing", .numParams = ARRAY_SIZE(pageRouting), .params = pageRouting },
};

static const _NT_parameterPages parameterPages = {
    .numPages = ARRAY_SIZE(pages),
    .pages = pages,
};

// ---------------------------------------------------------------------------
// MARK: Algorithm
// ---------------------------------------------------------------------------

/// @brief The state of a Pot Keys instance.
struct _potKeysAlgorithm : public _NT_algorithm {
    /// @brief Initialize a new instance.
    ///
    /// @param engine the polynomial tables the chip's oscillators share
    ///
    explicit _potKeysAlgorithm(Chip::Engine& engine) : voice(VOLUME, engine) { }

    /// the chip emulator and its BLIP buffers
    ChipVoice<Chip> voice;

    /// triggers for handling inputs to the control register flags
    Trigger::Threshold controlTriggers[CONTROL_COUNT];
};

/// @brief Return the 8-bit frequency register value for an oscillator.
///
/// @param self the algorithm to read parameters from
/// @param osc the index of the oscillator
/// @param voct the pitch CV in volts
/// @param fm the frequency modulation CV in volts
/// @returns the 8-bit frequency register value
///
static inline uint8_t getFrequency(
    const _potKeysAlgorithm* self,
    unsigned osc,
    float voct,
    float fm
) {
    // the minimal value for the frequency register to produce sound
    static constexpr float MIN = 2;
    // the maximal value for the frequency register
    static constexpr float MAX = 0xff;
    // the clock division of the oscillator relative to the CPU
    static constexpr float CLOCK_DIVISION = 58;
    const int16_t* v = self->v;
    float octaves = NTPotatoChips::pitch(
        v[tone(osc, kToneCoarse)], v[tone(osc, kToneFine)]
    );
    octaves += voct;
    octaves += (v[tone(osc, kToneFm)] / 100.f) * fm / 5.f;
    const float freq = NTPotatoChips::frequency(octaves);
    return Math::clip(
        NTPotatoChips::CLOCK_RATE / (CLOCK_DIVISION * freq), MIN, MAX
    );
}

/// @brief Return the noise register value for an oscillator.
///
/// @param noise the noise parameter for the oscillator
/// @param cv the noise CV in volts
/// @returns the 3-bit noise selection
/// @details
/// The parameter spans [0, 7] so the CV needs no scaling to cover its range.
///
static inline uint8_t getNoise(int16_t noise, float cv) {
    return Math::clip(noise + cv, 0.f, 7.f);
}

/// @brief Return the level register value for an oscillator.
///
/// @param level the level parameter for the oscillator
/// @param cv the level CV in volts
/// @returns the 4-bit level
///
static inline uint8_t getLevel(int16_t level, float cv) {
    const float value = roundf(level * Math::Eurorack::fromDC(cv));
    return Math::clip(value, 0.f, 15.f);
}

/// @brief Return the control register value.
///
/// @param self the algorithm to read parameters from
/// @param busses the input busses of the control flags, which may be NULL
/// @param frame the index of the frame being rendered
/// @returns the 8-bit control register value
///
static inline uint8_t getControl(
    _potKeysAlgorithm* self,
    const float* const* busses,
    unsigned frame
) {
    uint8_t controlByte = 0;
    for (unsigned control = 0; control < CONTROL_COUNT; control++) {
        self->controlTriggers[control].process(
            NTPotatoChips::triggerSignal(
                NTPotatoChips::voltage(busses[control], frame, 0.f)
            )
        );
        // the input inverts the parameter rather than overriding it
        const bool enabled = self->v[controlParam(control, kControlEnabled)];
        const bool state = enabled != self->controlTriggers[control].isHigh();
        controlByte |= state << CONTROL_BITS[control];
    }
    return controlByte;
}

void calculateRequirements(
    _NT_algorithmRequirements& req,
    const int32_t* specifications
) {
    req.numParameters = ARRAY_SIZE(parameters);
    req.sram = sizeof(_potKeysAlgorithm);
    // the polynomial tables and synthesizers the oscillators share
    req.dram = sizeof(Chip::Engine);
    req.dtc = 0;
    req.itc = 0;
}

_NT_algorithm* construct(
    const _NT_algorithmMemoryPtrs& ptrs,
    const _NT_algorithmRequirements& req,
    const int32_t* specifications
) {
    // the emulator takes the engine by reference so that its constructor
    // cannot fall back to the heap, which the module does not have
    Chip::Engine* engine = new (ptrs.dram) Chip::Engine();
    _potKeysAlgorithm* alg = new (ptrs.sram) _potKeysAlgorithm(*engine);
    alg->parameters = parameters;
    alg->parameterPages = &parameterPages;
    return alg;
}

void step(_NT_algorithm* self, float* busFrames, int numFramesBy4) {
    _potKeysAlgorithm* pThis = (_potKeysAlgorithm*)self;
    const int16_t* v = pThis->v;
    const unsigned numFrames = numFramesBy4 * 4;

    // resolve the busses once per step rather than once per frame
    const float* voct[OSC_COUNT];
    const float* fm[OSC_COUNT];
    const float* noise[OSC_COUNT];
    const float* level[OSC_COUNT];
    Output outputs[OSC_COUNT];
    for (unsigned osc = 0; osc < OSC_COUNT; osc++) {
        voct[osc] = NTPotatoChips::inputBus(busFrames, numFrames, v[tone(osc, kToneVOct)]);
        fm[osc] = NTPotatoChips::inputBus(busFrames, numFrames, v[tone(osc, kToneFmInput)]);
        noise[osc] = NTPotatoChips::inputBus(busFrames, numFrames, v[tone(osc, kToneNoiseInput)]);
        level[osc] = NTPotatoChips::inputBus(busFrames, numFrames, v[tone(osc, kToneLevelInput)]);
        outputs[osc].bus = NTPotatoChips::outputBus(busFrames, numFrames, v[kParamOutput + 2 * osc]);
        outputs[osc].replace = v[kParamOutput + 2 * osc + 1];
    }
    const float* control[CONTROL_COUNT];
    for (unsigned i = 0; i < CONTROL_COUNT; i++)
        control[i] = NTPotatoChips::inputBus(busFrames, numFrames, v[controlParam(i, kControlInput)]);

    float samples[OSC_COUNT];
    for (unsigned frame = 0; frame < numFrames; frame++) {
        // audio rate: the pitch of each oscillator
        for (unsigned osc = 0; osc < OSC_COUNT; osc++) {
            const float pitchCV = NTPotatoChips::voltage(voct[osc], frame, 0.f);
            // the FM input normals to 5V so that the attenuverter alone can
            // offset the pitch, as it does in the Rack build
            const float fmCV = NTPotatoChips::voltage(fm[osc], frame, 5.f);
            pThis->voice.apu.write(
                Chip::AUDF1 + Chip::REGS_PER_VOICE * osc,
                getFrequency(pThis, osc, pitchCV, fmCV)
            );
        }

        // control rate: the noise selections, levels and control register
        if (pThis->voice.isControlRate()) {
            for (unsigned osc = 0; osc < OSC_COUNT; osc++) {
                // the noise selection occupies the high three bits of the
                // register and the level the low four bits
                pThis->voice.apu.write(
                    Chip::AUDC1 + Chip::REGS_PER_VOICE * osc,
                    (getNoise(
                        v[tone(osc, kToneNoise)],
                        NTPotatoChips::voltage(noise[osc], frame, 0.f)
                    ) << 5) | getLevel(
                        v[tone(osc, kToneLevel)],
                        NTPotatoChips::voltage(level[osc], frame, 10.f)
                    )
                );
            }
            pThis->voice.apu.write(
                Chip::AUDCTL, getControl(pThis, control, frame)
            );
        }

        pThis->voice.advance(samples);
        // the Rack build normals unpatched outputs into the following voice
        NTPotatoChips::writeFrame(samples, outputs, OSC_COUNT, frame, true);
    }
}

static const _NT_factory factory = {
    .guid = NT_MULTICHAR( 'P', 'C', 'p', 'k' ),
    .name = "Pot Keys",
    .description = "Atari POKEY (Atari 8-bit, Atari 5200)",
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
