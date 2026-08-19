// A Yamaha YM2612 single-operator FM voice algorithm for the disting NT.
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
#include "dsp/yamaha_ym2612/feedback_operator.hpp"
#include "dsp/trigger/threshold.hpp"

using NTPotatoChips::ControlRateDivider;

/// the operator this algorithm hosts
typedef YamahaYM2612::FeedbackOperator Voice;

// ---------------------------------------------------------------------------
// MARK: Parameters
// ---------------------------------------------------------------------------

enum {
    kParamCoarse,
    kParamFine,
    kParamVOct,
    kParamFmDepth,
    kParamFmInput,
    kParamMultiplier,
    kParamFeedback,
    kParamLfo,
    kParamFmSensitivity,
    kParamAmSensitivity,
    kParamAttack,
    kParamAttackInput,
    kParamTotalLevel,
    kParamTotalLevelInput,
    kParamDecay,
    kParamDecayInput,
    kParamSustainLevel,
    kParamSustainLevelInput,
    kParamSustainRate,
    kParamSustainRateInput,
    kParamRelease,
    kParamReleaseInput,
    kParamRateScale,
    kParamLoopingEnvelope,
    kParamGate,
    kParamTrigger,
    kParamPreventClicks,
    kParamVolume,
    kParamVolumeInput,
    kParamOutput,
    kParamOutputMode,
    kNumParameters
};

static char const * const enumStringsOffOn[] = { "Off", "On" };

static const _NT_parameter parameters[] = {
    { .name = "Coarse", .min = -60, .max = 60, .def = 0, .unit = kNT_unitSemitones, .scaling = 0, .enumStrings = NULL },
    { .name = "Fine", .min = -100, .max = 100, .def = 0, .unit = kNT_unitCents, .scaling = 0, .enumStrings = NULL },
    NT_PARAMETER_CV_INPUT( "V/Oct", 0, 0 )
    // unlike the pitch input, this one modulates the operator's phase at audio
    // rate rather than its frequency register
    { .name = "FM depth", .min = -100, .max = 100, .def = 0, .unit = kNT_unitNone, .scaling = kNT_scaling100, .enumStrings = NULL },
    NT_PARAMETER_AUDIO_INPUT( "FM input", 0, 0 )
    { .name = "Multiplier", .min = 0, .max = 15, .def = 1, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL },
    { .name = "Feedback", .min = 0, .max = 7, .def = 0, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL },
    { .name = "LFO", .min = 0, .max = 7, .def = 0, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL },
    { .name = "FM sensitivity", .min = 0, .max = 7, .def = 0, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL },
    { .name = "AM sensitivity", .min = 0, .max = 3, .def = 0, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL },
    { .name = "Attack", .min = 1, .max = 31, .def = 31, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL },
    NT_PARAMETER_CV_INPUT( "Attack input", 0, 0 )
    { .name = "Level", .min = 0, .max = 100, .def = 100, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL },
    NT_PARAMETER_CV_INPUT( "Level input", 0, 0 )
    { .name = "Decay", .min = 0, .max = 31, .def = 0, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL },
    NT_PARAMETER_CV_INPUT( "Decay input", 0, 0 )
    { .name = "Sustain", .min = 0, .max = 15, .def = 15, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL },
    NT_PARAMETER_CV_INPUT( "Sustain input", 0, 0 )
    { .name = "Sustain rate", .min = 0, .max = 31, .def = 0, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL },
    NT_PARAMETER_CV_INPUT( "Sustain rate input", 0, 0 )
    { .name = "Release", .min = 0, .max = 15, .def = 15, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL },
    NT_PARAMETER_CV_INPUT( "Release input", 0, 0 )
    { .name = "Rate scale", .min = 0, .max = 3, .def = 0, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL },
    { .name = "Looping env", .min = 0, .max = 1, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = enumStringsOffOn },
    NT_PARAMETER_CV_INPUT( "Gate", 0, 0 )
    NT_PARAMETER_CV_INPUT( "Trigger", 0, 0 )
    { .name = "Prevent clicks", .min = 0, .max = 1, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = enumStringsOffOn },
    { .name = "Volume", .min = 0, .max = 127, .def = 127, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL },
    NT_PARAMETER_CV_INPUT( "Volume input", 0, 0 )
    NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE( "Output", 0, 13 )
};

static_assert(
    ARRAY_SIZE(parameters) == kNumParameters,
    "the parameter table and the parameter enum disagree"
);

static const uint8_t pageOscillator[] = {
    kParamCoarse, kParamFine, kParamVOct,
    kParamFmDepth, kParamFmInput,
    kParamMultiplier, kParamFeedback,
    kParamLfo, kParamFmSensitivity, kParamAmSensitivity,
};

static const uint8_t pageEnvelope[] = {
    kParamAttack, kParamAttackInput,
    kParamTotalLevel, kParamTotalLevelInput,
    kParamDecay, kParamDecayInput,
    kParamSustainLevel, kParamSustainLevelInput,
    kParamSustainRate, kParamSustainRateInput,
    kParamRelease, kParamReleaseInput,
    kParamRateScale, kParamLoopingEnvelope,
    kParamGate, kParamTrigger, kParamPreventClicks,
};

static const uint8_t pageRouting[] = {
    kParamVolume, kParamVolumeInput,
    kParamOutput, kParamOutputMode,
};

static const _NT_parameterPage pages[] = {
    { .name = "Oscillator", .numParams = ARRAY_SIZE(pageOscillator), .params = pageOscillator },
    { .name = "Envelope", .numParams = ARRAY_SIZE(pageEnvelope), .params = pageEnvelope },
    { .name = "Routing", .numParams = ARRAY_SIZE(pageRouting), .params = pageRouting },
};

static const _NT_parameterPages parameterPages = {
    .numPages = ARRAY_SIZE(pages),
    .pages = pages,
};

// ---------------------------------------------------------------------------
// MARK: Algorithm
// ---------------------------------------------------------------------------

/// @brief The state of a Mini Boss instance.
struct _miniBossAlgorithm : public _NT_algorithm {
    /// @brief Initialize a new instance.
    ///
    /// @param voice_ the operator to render with, allocated in DRAM
    ///
    explicit _miniBossAlgorithm(Voice* voice_) : voice(voice_) { }

    /// the operator to render with
    /// @details
    /// The operator carries the chip's sine and envelope tables, which are too
    /// large to hold in the algorithm's SRAM allocation.
    Voice* voice;

    /// the divider that gates the control-rate register updates
    ControlRateDivider cvDivider;

    /// a trigger for opening and closing the operator's gate
    Trigger::Threshold gateTrigger;

    /// a trigger for handling the re-trigger input
    Trigger::Threshold retriggerTrigger;
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

/// @brief Return the output volume level.
///
/// @param self the algorithm to read parameters from
/// @param cv the volume CV in volts
/// @returns the volume level in [0, 127]
///
static inline int32_t getVolume(const _miniBossAlgorithm* self, float cv) {
    static constexpr float MAX = 127;
    const float mod = MAX * Math::Eurorack::fromDC(cv);
    return Math::clip(self->v[kParamVolume] + mod, 0.f, MAX);
}

/// @brief Return the phase modulation to apply to the operator.
///
/// @param self the algorithm to read parameters from
/// @param cv the voltage on the FM input
/// @returns the 14-bit signed modulation signal
///
static inline int16_t getFM(const _miniBossAlgorithm* self, float cv) {
    const float depth = self->v[kParamFmDepth] / 100.f;
    return (1 << 13) * Math::clip(depth * cv / 5.f, -1.f, 1.f);
}

void calculateRequirements(
    _NT_algorithmRequirements& req,
    const int32_t* specifications
) {
    req.numParameters = ARRAY_SIZE(parameters);
    req.sram = sizeof(_miniBossAlgorithm);
    // the operator holds the chip's sine and envelope tables
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
    _miniBossAlgorithm* alg = new (ptrs.sram) _miniBossAlgorithm(voice);
    alg->parameters = parameters;
    alg->parameterPages = &parameterPages;
    return alg;
}

void step(_NT_algorithm* self, float* busFrames, int numFramesBy4) {
    _miniBossAlgorithm* pThis = (_miniBossAlgorithm*)self;
    const int16_t* v = pThis->v;
    const unsigned numFrames = numFramesBy4 * 4;
    Voice& voice = *pThis->voice;

    // resolve the busses once per step rather than once per frame
    const float* voct = NTPotatoChips::inputBus(busFrames, numFrames, v[kParamVOct]);
    const float* fm = NTPotatoChips::inputBus(busFrames, numFrames, v[kParamFmInput]);
    const float* attack = NTPotatoChips::inputBus(busFrames, numFrames, v[kParamAttackInput]);
    const float* totalLevel = NTPotatoChips::inputBus(busFrames, numFrames, v[kParamTotalLevelInput]);
    const float* decay = NTPotatoChips::inputBus(busFrames, numFrames, v[kParamDecayInput]);
    const float* sustainLevel = NTPotatoChips::inputBus(busFrames, numFrames, v[kParamSustainLevelInput]);
    const float* sustainRate = NTPotatoChips::inputBus(busFrames, numFrames, v[kParamSustainRateInput]);
    const float* release = NTPotatoChips::inputBus(busFrames, numFrames, v[kParamReleaseInput]);
    const float* gate = NTPotatoChips::inputBus(busFrames, numFrames, v[kParamGate]);
    const float* trigger = NTPotatoChips::inputBus(busFrames, numFrames, v[kParamTrigger]);
    const float* volume = NTPotatoChips::inputBus(busFrames, numFrames, v[kParamVolumeInput]);
    float* out = NTPotatoChips::outputBus(busFrames, numFrames, v[kParamOutput]);
    const bool replace = v[kParamOutputMode];
    const bool preventClicks = v[kParamPreventClicks];

    for (unsigned frame = 0; frame < numFrames; frame++) {
        // control rate: the envelope and tone registers
        if (pThis->cvDivider.process()) {
            voice.set_attack_rate(getParam(
                v[kParamAttack],
                NTPotatoChips::voltage(attack, frame, 0.f), 1, 31
            ));
            // the register attenuates rather than amplifies, so invert it
            voice.set_total_level(100 - getParam(
                v[kParamTotalLevel],
                NTPotatoChips::voltage(totalLevel, frame, 0.f), 0, 100
            ));
            voice.set_decay_rate(getParam(
                v[kParamDecay],
                NTPotatoChips::voltage(decay, frame, 0.f), 0, 31
            ));
            voice.set_sustain_level(15 - getParam(
                v[kParamSustainLevel],
                NTPotatoChips::voltage(sustainLevel, frame, 0.f), 0, 15
            ));
            voice.set_sustain_rate(getParam(
                v[kParamSustainRate],
                NTPotatoChips::voltage(sustainRate, frame, 0.f), 0, 31
            ));
            voice.set_release_rate(getParam(
                v[kParamRelease],
                NTPotatoChips::voltage(release, frame, 0.f), 0, 15
            ));
            voice.set_multiplier(v[kParamMultiplier]);
            voice.set_feedback(v[kParamFeedback]);
            voice.set_lfo(v[kParamLfo]);
            voice.set_fm_sensitivity(v[kParamFmSensitivity]);
            voice.set_am_sensitivity(v[kParamAmSensitivity]);
            voice.set_ssg_enabled(v[kParamLoopingEnvelope]);
            voice.set_rate_scale(v[kParamRateScale]);
            pThis->gateTrigger.process(
                NTPotatoChips::triggerSignal(
                    NTPotatoChips::voltage(gate, frame, 0.f)
                )
            );
            const bool retrigger = pThis->retriggerTrigger.process(
                NTPotatoChips::triggerSignal(
                    NTPotatoChips::voltage(trigger, frame, 0.f)
                )
            );
            // the exclusive or of the gate and the re-trigger holds the gate
            // open when either alone is high and closes it when both or
            // neither are, which shuts the gate for one sample when
            // re-triggering an already gated voice
            voice.set_gate(
                pThis->gateTrigger.isHigh() ^ retrigger, preventClicks
            );
        }

        // audio rate: the pitch and the phase modulation
        const float octaves = NTPotatoChips::pitch(
            v[kParamCoarse], v[kParamFine]
        ) + NTPotatoChips::voltage(voct, frame, 0.f);
        voice.set_frequency(
            Math::Eurorack::voct2freq(Math::clip(octaves, -6.5f, 6.5f))
        );
        // the operator renders a 14-bit signed sample
        const int16_t audio = (
            voice.step(getFM(pThis, NTPotatoChips::voltage(fm, frame, 0.f))) *
            getVolume(pThis, NTPotatoChips::voltage(volume, frame, 0.f))
        ) >> 7;
        const float sample = YamahaYM2612::Operator::clip(audio) /
            static_cast<float>(1 << 13);
        NTPotatoChips::write(
            out, frame, Math::Eurorack::toAC(sample), replace
        );
    }
}

static const _NT_factory factory = {
    .guid = NT_MULTICHAR( 'P', 'C', 'm', 'b' ),
    .name = "Mini Boss",
    .description = "Yamaha YM2612 single-operator FM voice (Sega Mega Drive)",
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
