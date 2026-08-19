// A Yamaha YM2612 4-operator FM voice algorithm for the disting NT.
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
#include "dsp/yamaha_ym2612/voice4op.hpp"
#include "dsp/trigger/threshold.hpp"

using NTPotatoChips::ControlRateDivider;
using NTPotatoChips::Output;

/// the voice this algorithm hosts
typedef YamahaYM2612::Voice4Op Voice;

/// the number of operators in the voice
static constexpr unsigned NUM_OPERATORS = Voice::NUM_OPERATORS;

// ---------------------------------------------------------------------------
// MARK: Parameters
// ---------------------------------------------------------------------------

/// the global parameters, which precede the operator blocks
enum {
    kParamAlgorithm,
    kParamAlgorithmInput,
    kParamFeedback,
    kParamFeedbackInput,
    kParamLfo,
    kParamLfoInput,
    kParamSaturation,
    kParamSaturationInput,
    kParamPreventClicks,
    kNumGlobalParams
};

/// the parameters of one operator, in the order they are declared
enum {
    kOpCoarse,
    kOpFine,
    kOpPitchInput,
    kOpAttack,
    kOpAttackInput,
    kOpTotalLevel,
    kOpTotalLevelInput,
    kOpDecay,
    kOpDecayInput,
    kOpSustainLevel,
    kOpSustainLevelInput,
    kOpSustainRate,
    kOpSustainRateInput,
    kOpRelease,
    kOpReleaseInput,
    kOpMultiplier,
    kOpMultiplierInput,
    kOpFmSensitivity,
    kOpFmSensitivityInput,
    kOpAmSensitivity,
    kOpAmSensitivityInput,
    kOpRateScale,
    kOpLoopingEnvelope,
    kOpGate,
    kOpRetrigger,
    kNumOperatorParams
};

/// @brief Return the index of a parameter within an operator's block.
///
/// @param op the index of the operator
/// @param param the index of the parameter within the operator's block
/// @returns the index of the parameter within the algorithm
///
static constexpr uint8_t opParam(unsigned op, unsigned param) {
    return kNumGlobalParams + op * kNumOperatorParams + param;
}

/// the parameters that follow the four operator blocks
enum {
    kParamOutput = kNumGlobalParams + NUM_OPERATORS * kNumOperatorParams,
    kParamOutputMode,
    kNumParameters
};

static char const * const enumStringsOffOn[] = { "Off", "On" };

/// @brief Declare the twenty five parameters of one operator.
/// @param NAME the display prefix for the operator, e.g., "Op 1"
#define OPERATOR_PARAMETERS( NAME ) \
    { .name = NAME " coarse", .min = -60, .max = 60, .def = 0, .unit = kNT_unitSemitones, .scaling = 0, .enumStrings = NULL }, \
    { .name = NAME " fine", .min = -100, .max = 100, .def = 0, .unit = kNT_unitCents, .scaling = 0, .enumStrings = NULL }, \
    NT_PARAMETER_CV_INPUT( NAME " pitch input", 0, 0 ) \
    { .name = NAME " attack", .min = 1, .max = 31, .def = 31, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL }, \
    NT_PARAMETER_CV_INPUT( NAME " attack input", 0, 0 ) \
    { .name = NAME " level", .min = 0, .max = 100, .def = 100, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL }, \
    NT_PARAMETER_CV_INPUT( NAME " level input", 0, 0 ) \
    { .name = NAME " decay", .min = 0, .max = 31, .def = 0, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL }, \
    NT_PARAMETER_CV_INPUT( NAME " decay input", 0, 0 ) \
    { .name = NAME " sustain", .min = 0, .max = 15, .def = 15, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL }, \
    NT_PARAMETER_CV_INPUT( NAME " sustain input", 0, 0 ) \
    { .name = NAME " sustain rate", .min = 0, .max = 31, .def = 0, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL }, \
    NT_PARAMETER_CV_INPUT( NAME " sustain rate input", 0, 0 ) \
    { .name = NAME " release", .min = 0, .max = 15, .def = 15, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL }, \
    NT_PARAMETER_CV_INPUT( NAME " release input", 0, 0 ) \
    { .name = NAME " multiplier", .min = 0, .max = 15, .def = 1, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL }, \
    NT_PARAMETER_CV_INPUT( NAME " multiplier input", 0, 0 ) \
    { .name = NAME " FM depth", .min = 0, .max = 7, .def = 0, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL }, \
    NT_PARAMETER_CV_INPUT( NAME " FM depth input", 0, 0 ) \
    { .name = NAME " AM depth", .min = 0, .max = 3, .def = 0, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL }, \
    NT_PARAMETER_CV_INPUT( NAME " AM depth input", 0, 0 ) \
    { .name = NAME " rate scale", .min = 0, .max = 3, .def = 0, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL }, \
    { .name = NAME " looping env", .min = 0, .max = 1, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = enumStringsOffOn }, \
    NT_PARAMETER_CV_INPUT( NAME " gate", 0, 0 ) \
    NT_PARAMETER_CV_INPUT( NAME " trigger", 0, 0 )

static const _NT_parameter parameters[] = {
    { .name = "Algorithm", .min = 0, .max = 7, .def = 7, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL },
    NT_PARAMETER_CV_INPUT( "Algorithm input", 0, 0 )
    { .name = "Feedback", .min = 0, .max = 7, .def = 0, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL },
    NT_PARAMETER_CV_INPUT( "Feedback input", 0, 0 )
    { .name = "LFO", .min = 0, .max = 7, .def = 0, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL },
    NT_PARAMETER_CV_INPUT( "LFO input", 0, 0 )
    { .name = "Saturation", .min = 0, .max = 127, .def = 127, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL },
    NT_PARAMETER_CV_INPUT( "Saturation input", 0, 0 )
    { .name = "Prevent clicks", .min = 0, .max = 1, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = enumStringsOffOn },
    OPERATOR_PARAMETERS( "Op 1" )
    OPERATOR_PARAMETERS( "Op 2" )
    OPERATOR_PARAMETERS( "Op 3" )
    OPERATOR_PARAMETERS( "Op 4" )
    NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE( "Output", 0, 13 )
};

static_assert(
    ARRAY_SIZE(parameters) == kNumParameters,
    "the parameter table and the parameter enum disagree"
);

static const uint8_t pageGlobal[] = {
    kParamAlgorithm, kParamAlgorithmInput,
    kParamFeedback, kParamFeedbackInput,
    kParamLfo, kParamLfoInput,
    kParamSaturation, kParamSaturationInput,
    kParamPreventClicks,
};

/// @brief Declare the envelope page contents of one operator.
/// @param OP the index of the operator
#define OPERATOR_ENVELOPE_PAGE( OP ) { \
    opParam(OP, kOpAttack), opParam(OP, kOpAttackInput), \
    opParam(OP, kOpDecay), opParam(OP, kOpDecayInput), \
    opParam(OP, kOpSustainLevel), opParam(OP, kOpSustainLevelInput), \
    opParam(OP, kOpSustainRate), opParam(OP, kOpSustainRateInput), \
    opParam(OP, kOpRelease), opParam(OP, kOpReleaseInput), \
    opParam(OP, kOpRateScale), opParam(OP, kOpLoopingEnvelope), \
    opParam(OP, kOpGate), opParam(OP, kOpRetrigger), \
}

/// @brief Declare the oscillator page contents of one operator.
/// @param OP the index of the operator
#define OPERATOR_TONE_PAGE( OP ) { \
    opParam(OP, kOpCoarse), opParam(OP, kOpFine), opParam(OP, kOpPitchInput), \
    opParam(OP, kOpMultiplier), opParam(OP, kOpMultiplierInput), \
    opParam(OP, kOpTotalLevel), opParam(OP, kOpTotalLevelInput), \
    opParam(OP, kOpFmSensitivity), opParam(OP, kOpFmSensitivityInput), \
    opParam(OP, kOpAmSensitivity), opParam(OP, kOpAmSensitivityInput), \
}

static const uint8_t pageTone1[] = OPERATOR_TONE_PAGE( 0 );
static const uint8_t pageEnv1[] = OPERATOR_ENVELOPE_PAGE( 0 );
static const uint8_t pageTone2[] = OPERATOR_TONE_PAGE( 1 );
static const uint8_t pageEnv2[] = OPERATOR_ENVELOPE_PAGE( 1 );
static const uint8_t pageTone3[] = OPERATOR_TONE_PAGE( 2 );
static const uint8_t pageEnv3[] = OPERATOR_ENVELOPE_PAGE( 2 );
static const uint8_t pageTone4[] = OPERATOR_TONE_PAGE( 3 );
static const uint8_t pageEnv4[] = OPERATOR_ENVELOPE_PAGE( 3 );

static const uint8_t pageRouting[] = { kParamOutput, kParamOutputMode };

static const _NT_parameterPage pages[] = {
    { .name = "Voice", .numParams = ARRAY_SIZE(pageGlobal), .params = pageGlobal },
    { .name = "Op 1", .numParams = ARRAY_SIZE(pageTone1), .params = pageTone1 },
    { .name = "Op 1 env", .numParams = ARRAY_SIZE(pageEnv1), .params = pageEnv1 },
    { .name = "Op 2", .numParams = ARRAY_SIZE(pageTone2), .params = pageTone2 },
    { .name = "Op 2 env", .numParams = ARRAY_SIZE(pageEnv2), .params = pageEnv2 },
    { .name = "Op 3", .numParams = ARRAY_SIZE(pageTone3), .params = pageTone3 },
    { .name = "Op 3 env", .numParams = ARRAY_SIZE(pageEnv3), .params = pageEnv3 },
    { .name = "Op 4", .numParams = ARRAY_SIZE(pageTone4), .params = pageTone4 },
    { .name = "Op 4 env", .numParams = ARRAY_SIZE(pageEnv4), .params = pageEnv4 },
    { .name = "Routing", .numParams = ARRAY_SIZE(pageRouting), .params = pageRouting },
};

static const _NT_parameterPages parameterPages = {
    .numPages = ARRAY_SIZE(pages),
    .pages = pages,
};

// ---------------------------------------------------------------------------
// MARK: Algorithm
// ---------------------------------------------------------------------------

/// @brief The state of a Boss Fight instance.
struct _bossFightAlgorithm : public _NT_algorithm {
    /// @brief Initialize a new instance.
    ///
    /// @param voice_ the voice to render with, allocated in DRAM
    ///
    explicit _bossFightAlgorithm(Voice* voice_) : voice(voice_) { }

    /// the voice to render with
    /// @details
    /// The voice carries the chip's sine and envelope tables, which are too
    /// large to hold in the algorithm's SRAM allocation.
    Voice* voice;

    /// the divider that gates the control-rate register updates
    ControlRateDivider cvDivider;

    /// triggers for opening and closing the operator gates
    Trigger::Threshold gateTriggers[NUM_OPERATORS];

    /// triggers for handling the operator re-trigger inputs
    Trigger::Threshold retriggerTriggers[NUM_OPERATORS];
};

/// @brief Return a register value from a parameter and its CV input.
///
/// @param param the value of the parameter
/// @param cv the voltage on the parameter's CV input
/// @param min the minimal value for the register
/// @param max the maximal value for the register
/// @returns the register value
/// @details
/// The CV covers the register's full range over 8V, as in the Rack build.
///
static inline uint8_t getParam(
    int16_t param,
    float cv,
    unsigned min,
    unsigned max
) {
    const float mod = max * cv / 8.f;
    return Math::clip(
        static_cast<int>(param + mod),
        static_cast<int>(min),
        static_cast<int>(max)
    );
}

/// @brief Return the output saturation level.
///
/// @param self the algorithm to read parameters from
/// @param cv the saturation CV in volts
/// @returns the saturation level in [0, 127]
///
static inline int32_t getSaturation(
    const _bossFightAlgorithm* self,
    float cv
) {
    static constexpr float MAX = 127;
    const float mod = MAX * Math::Eurorack::fromDC(cv);
    return Math::clip(self->v[kParamSaturation] + mod, 0.f, MAX);
}

void calculateRequirements(
    _NT_algorithmRequirements& req,
    const int32_t* specifications
) {
    req.numParameters = ARRAY_SIZE(parameters);
    req.sram = sizeof(_bossFightAlgorithm);
    // the voice holds the chip's sine and envelope tables
    req.dram = sizeof(Voice);
    req.dtc = 0;
    req.itc = 0;
}

_NT_algorithm* construct(
    const _NT_algorithmMemoryPtrs& ptrs,
    const _NT_algorithmRequirements& req,
    const int32_t* specifications
) {
    Voice* voice = new (ptrs.dram) Voice(
        NT_globals.sampleRate, NTPotatoChips::CLOCK_RATE
    );
    _bossFightAlgorithm* alg = new (ptrs.sram) _bossFightAlgorithm(voice);
    alg->parameters = parameters;
    alg->parameterPages = &parameterPages;
    return alg;
}

void step(_NT_algorithm* self, float* busFrames, int numFramesBy4) {
    _bossFightAlgorithm* pThis = (_bossFightAlgorithm*)self;
    const int16_t* v = pThis->v;
    const unsigned numFrames = numFramesBy4 * 4;
    Voice& voice = *pThis->voice;

    // resolve the busses once per step rather than once per frame
    const float* algorithmBus = NTPotatoChips::inputBus(busFrames, numFrames, v[kParamAlgorithmInput]);
    const float* feedbackBus = NTPotatoChips::inputBus(busFrames, numFrames, v[kParamFeedbackInput]);
    const float* lfoBus = NTPotatoChips::inputBus(busFrames, numFrames, v[kParamLfoInput]);
    const float* saturationBus = NTPotatoChips::inputBus(busFrames, numFrames, v[kParamSaturationInput]);
    // the per-operator inputs, in the order of the operator parameter block
    static const uint8_t INPUTS[] = {
        kOpPitchInput, kOpAttackInput, kOpTotalLevelInput, kOpDecayInput,
        kOpSustainLevelInput, kOpSustainRateInput, kOpReleaseInput,
        kOpMultiplierInput, kOpFmSensitivityInput, kOpAmSensitivityInput,
        kOpGate, kOpRetrigger
    };
    const float* busses[NUM_OPERATORS][ARRAY_SIZE(INPUTS)];
    for (unsigned op = 0; op < NUM_OPERATORS; op++) {
        for (unsigned i = 0; i < ARRAY_SIZE(INPUTS); i++) {
            busses[op][i] = NTPotatoChips::inputBus(
                busFrames, numFrames, v[opParam(op, INPUTS[i])]
            );
        }
    }
    // the index of each input within the busses array
    enum {
        kBusPitch, kBusAttack, kBusTotalLevel, kBusDecay, kBusSustainLevel,
        kBusSustainRate, kBusRelease, kBusMultiplier, kBusFmSensitivity,
        kBusAmSensitivity, kBusGate, kBusRetrigger
    };

    float* out = NTPotatoChips::outputBus(busFrames, numFrames, v[kParamOutput]);
    const bool replace = v[kParamOutputMode];
    const bool preventClicks = v[kParamPreventClicks];

    for (unsigned frame = 0; frame < numFrames; frame++) {
        // control rate: the voice and envelope registers
        if (pThis->cvDivider.process()) {
            voice.set_algorithm(getParam(
                v[kParamAlgorithm],
                NTPotatoChips::voltage(algorithmBus, frame, 0.f), 0, 7
            ));
            voice.set_feedback(getParam(
                v[kParamFeedback],
                NTPotatoChips::voltage(feedbackBus, frame, 0.f), 0, 7
            ));
            voice.set_lfo(getParam(
                v[kParamLfo],
                NTPotatoChips::voltage(lfoBus, frame, 0.f), 0, 7
            ));
            // the gate and re-trigger inputs normal forward from the previous
            // operator, starting from 0V
            float gate = 0.f;
            float retrigger = 0.f;
            for (unsigned op = 0; op < NUM_OPERATORS; op++) {
                const float* const* bus = busses[op];
                voice.set_attack_rate(op, getParam(
                    v[opParam(op, kOpAttack)],
                    NTPotatoChips::voltage(bus[kBusAttack], frame, 0.f), 1, 31
                ));
                // the register attenuates rather than amplifies, so invert it
                voice.set_total_level(op, 100 - getParam(
                    v[opParam(op, kOpTotalLevel)],
                    NTPotatoChips::voltage(bus[kBusTotalLevel], frame, 0.f), 0, 100
                ));
                voice.set_decay_rate(op, getParam(
                    v[opParam(op, kOpDecay)],
                    NTPotatoChips::voltage(bus[kBusDecay], frame, 0.f), 0, 31
                ));
                voice.set_sustain_level(op, 15 - getParam(
                    v[opParam(op, kOpSustainLevel)],
                    NTPotatoChips::voltage(bus[kBusSustainLevel], frame, 0.f), 0, 15
                ));
                voice.set_sustain_rate(op, getParam(
                    v[opParam(op, kOpSustainRate)],
                    NTPotatoChips::voltage(bus[kBusSustainRate], frame, 0.f), 0, 31
                ));
                voice.set_release_rate(op, getParam(
                    v[opParam(op, kOpRelease)],
                    NTPotatoChips::voltage(bus[kBusRelease], frame, 0.f), 0, 15
                ));
                voice.set_multiplier(op, getParam(
                    v[opParam(op, kOpMultiplier)],
                    NTPotatoChips::voltage(bus[kBusMultiplier], frame, 0.f), 0, 15
                ));
                voice.set_fm_sensitivity(op, getParam(
                    v[opParam(op, kOpFmSensitivity)],
                    NTPotatoChips::voltage(bus[kBusFmSensitivity], frame, 0.f), 0, 7
                ));
                voice.set_am_sensitivity(op, getParam(
                    v[opParam(op, kOpAmSensitivity)],
                    NTPotatoChips::voltage(bus[kBusAmSensitivity], frame, 0.f), 0, 4
                ));
                voice.set_ssg_enabled(op, v[opParam(op, kOpLoopingEnvelope)]);
                voice.set_rate_scale(op, v[opParam(op, kOpRateScale)]);

                gate = NTPotatoChips::voltage(bus[kBusGate], frame, gate);
                pThis->gateTriggers[op].process(
                    NTPotatoChips::triggerSignal(gate)
                );
                retrigger = NTPotatoChips::voltage(bus[kBusRetrigger], frame, retrigger);
                const bool trigger = pThis->retriggerTriggers[op].process(
                    NTPotatoChips::triggerSignal(retrigger)
                );
                // the exclusive or of the gate and the re-trigger holds the
                // gate open when either alone is high and closes it when both
                // or neither are, which shuts the gate for one sample when
                // re-triggering an already gated voice
                voice.set_gate(
                    op,
                    trigger ^ pThis->gateTriggers[op].isHigh(),
                    preventClicks
                );
            }
        }

        // audio rate: the pitch of each operator, whose input normals forward
        // from the previous operator
        float pitch = 0.f;
        for (unsigned op = 0; op < NUM_OPERATORS; op++) {
            const float octaves = NTPotatoChips::pitch(
                v[opParam(op, kOpCoarse)], v[opParam(op, kOpFine)]
            );
            pitch = NTPotatoChips::voltage(busses[op][kBusPitch], frame, pitch);
            voice.set_frequency(
                op,
                Math::Eurorack::voct2freq(Math::clip(octaves + pitch, -6.5f, 6.5f))
            );
        }

        // the voice renders a 14-bit signed sample
        const int32_t saturation = getSaturation(
            pThis, NTPotatoChips::voltage(saturationBus, frame, 0.f)
        );
        const int16_t audio = (voice.step() * saturation) >> 7;
        const float sample = YamahaYM2612::Operator::clip(audio) /
            static_cast<float>(1 << 13);
        NTPotatoChips::write(
            out, frame, Math::Eurorack::toAC(sample), replace
        );
    }
}

static const _NT_factory factory = {
    .guid = NT_MULTICHAR( 'P', 'C', 'b', 'f' ),
    .name = "Boss Fight",
    .description = "Yamaha YM2612 4-operator FM voice (Sega Mega Drive)",
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
