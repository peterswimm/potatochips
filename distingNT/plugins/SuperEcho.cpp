// A Sony S-DSP echo algorithm for the disting NT.
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
#include "dsp/sony_s_dsp/echo.hpp"

/// the echo emulator this algorithm hosts
typedef SonyS_DSP::Echo Echo;

/// the number of stereo channels the echo processes
static constexpr unsigned CHANNELS = SonyS_DSP::StereoSample::CHANNELS;

/// the number of coefficients in the echo's FIR filter
static constexpr unsigned FIR_COEFFICIENT_COUNT = Echo::FIR_COEFFICIENT_COUNT;

// ---------------------------------------------------------------------------
// MARK: Parameters
// ---------------------------------------------------------------------------

/// the global parameters, which precede the channel and coefficient blocks
enum {
    kParamDelay,
    kParamDelayInput,
    kParamFeedback,
    kParamFeedbackInput,
    kParamBypass,
    kNumGlobalParams
};

/// the parameters of one stereo channel, in the order they are declared
enum {
    kChannelInput,
    kChannelGain,
    kChannelMix,
    kChannelMixInput,
    kChannelOutput,
    kChannelOutputMode,
    kNumChannelParams
};

/// @brief Return the index of a parameter within a channel's block.
///
/// @param channel the index of the stereo channel
/// @param param the index of the parameter within the channel's block
/// @returns the index of the parameter within the algorithm
///
static constexpr uint8_t channelParam(unsigned channel, unsigned param) {
    return kNumGlobalParams + channel * kNumChannelParams + param;
}

/// the parameters of one FIR coefficient, in the order they are declared
enum {
    kCoefficientValue,
    kCoefficientAtt,
    kCoefficientInput,
    kNumCoefficientParams
};

/// @brief Return the index of a parameter within a coefficient's block.
///
/// @param index the index of the FIR coefficient
/// @param param the index of the parameter within the coefficient's block
/// @returns the index of the parameter within the algorithm
///
static constexpr uint8_t coefficientParam(unsigned index, unsigned param) {
    return kNumGlobalParams + CHANNELS * kNumChannelParams +
        index * kNumCoefficientParams + param;
}

enum {
    kNumParameters = kNumGlobalParams + CHANNELS * kNumChannelParams +
        FIR_COEFFICIENT_COUNT * kNumCoefficientParams
};

static char const * const enumStringsOffOn[] = { "Off", "On" };

/// @brief Declare the parameters of one stereo channel.
/// @param NAME the display prefix for the channel, e.g., "Left"
/// @param OUT the default output bus for the channel
#define CHANNEL_PARAMETERS( NAME, OUT ) \
    NT_PARAMETER_AUDIO_INPUT( NAME " input", 0, 0 ) \
    { .name = NAME " gain", .min = 0, .max = 200, .def = 100, .unit = kNT_unitNone, .scaling = kNT_scaling100, .enumStrings = NULL }, \
    { .name = NAME " mix", .min = -128, .max = 127, .def = 0, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL }, \
    NT_PARAMETER_CV_INPUT( NAME " mix input", 0, 0 ) \
    NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE( NAME " output", 0, OUT )

/// @brief Declare the parameters of one FIR coefficient.
/// @param NAME the display prefix for the coefficient, e.g., "FIR 1"
/// @param DEF the coefficient's default value
#define COEFFICIENT_PARAMETERS( NAME, DEF ) \
    { .name = NAME, .min = -128, .max = 127, .def = DEF, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL }, \
    { .name = NAME " att", .min = -100, .max = 100, .def = 0, .unit = kNT_unitNone, .scaling = kNT_scaling100, .enumStrings = NULL }, \
    NT_PARAMETER_CV_INPUT( NAME " input", 0, 0 )

static const _NT_parameter parameters[] = {
    { .name = "Delay", .min = 0, .max = Echo::DELAY_LEVELS, .def = 0, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL },
    NT_PARAMETER_CV_INPUT( "Delay input", 0, 0 )
    { .name = "Feedback", .min = -128, .max = 127, .def = 0, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL },
    NT_PARAMETER_CV_INPUT( "Feedback input", 0, 0 )
    { .name = "Bypass", .min = 0, .max = 1, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = enumStringsOffOn },
    CHANNEL_PARAMETERS( "Left", 13 )
    CHANNEL_PARAMETERS( "Right", 14 )
    // the emulator's own defaults: an impulse, i.e., a transparent filter
    COEFFICIENT_PARAMETERS( "FIR 1", 127 )
    COEFFICIENT_PARAMETERS( "FIR 2", 0 )
    COEFFICIENT_PARAMETERS( "FIR 3", 0 )
    COEFFICIENT_PARAMETERS( "FIR 4", 0 )
    COEFFICIENT_PARAMETERS( "FIR 5", 0 )
    COEFFICIENT_PARAMETERS( "FIR 6", 0 )
    COEFFICIENT_PARAMETERS( "FIR 7", 0 )
    COEFFICIENT_PARAMETERS( "FIR 8", 0 )
};

static_assert(
    ARRAY_SIZE(parameters) == kNumParameters,
    "the parameter table and the parameter enum disagree"
);

static const uint8_t pageEcho[] = {
    kParamDelay, kParamDelayInput,
    kParamFeedback, kParamFeedbackInput,
    kParamBypass,
};

/// @brief Declare the page contents of one stereo channel.
/// @param CHANNEL the index of the channel
#define CHANNEL_PAGE( CHANNEL ) { \
    channelParam(CHANNEL, kChannelInput), \
    channelParam(CHANNEL, kChannelGain), \
    channelParam(CHANNEL, kChannelMix), \
    channelParam(CHANNEL, kChannelMixInput), \
    channelParam(CHANNEL, kChannelOutput), \
    channelParam(CHANNEL, kChannelOutputMode), \
}

static const uint8_t pageLeft[] = CHANNEL_PAGE( 0 );
static const uint8_t pageRight[] = CHANNEL_PAGE( 1 );

static const uint8_t pageFIR1[] = {
    coefficientParam(0, kCoefficientValue), coefficientParam(0, kCoefficientAtt), coefficientParam(0, kCoefficientInput),
    coefficientParam(1, kCoefficientValue), coefficientParam(1, kCoefficientAtt), coefficientParam(1, kCoefficientInput),
    coefficientParam(2, kCoefficientValue), coefficientParam(2, kCoefficientAtt), coefficientParam(2, kCoefficientInput),
    coefficientParam(3, kCoefficientValue), coefficientParam(3, kCoefficientAtt), coefficientParam(3, kCoefficientInput),
};

static const uint8_t pageFIR2[] = {
    coefficientParam(4, kCoefficientValue), coefficientParam(4, kCoefficientAtt), coefficientParam(4, kCoefficientInput),
    coefficientParam(5, kCoefficientValue), coefficientParam(5, kCoefficientAtt), coefficientParam(5, kCoefficientInput),
    coefficientParam(6, kCoefficientValue), coefficientParam(6, kCoefficientAtt), coefficientParam(6, kCoefficientInput),
    coefficientParam(7, kCoefficientValue), coefficientParam(7, kCoefficientAtt), coefficientParam(7, kCoefficientInput),
};

static const _NT_parameterPage pages[] = {
    { .name = "Echo", .numParams = ARRAY_SIZE(pageEcho), .params = pageEcho },
    { .name = "Left", .numParams = ARRAY_SIZE(pageLeft), .params = pageLeft },
    { .name = "Right", .numParams = ARRAY_SIZE(pageRight), .params = pageRight },
    { .name = "FIR 1-4", .numParams = ARRAY_SIZE(pageFIR1), .params = pageFIR1 },
    { .name = "FIR 5-8", .numParams = ARRAY_SIZE(pageFIR2), .params = pageFIR2 },
};

static const _NT_parameterPages parameterPages = {
    .numPages = ARRAY_SIZE(pages),
    .pages = pages,
};

// ---------------------------------------------------------------------------
// MARK: Algorithm
// ---------------------------------------------------------------------------

/// @brief The state of a Super Echo instance.
struct _superEchoAlgorithm : public _NT_algorithm {
    /// @brief Initialize a new instance.
    ///
    /// @param apu_ the echo emulator, allocated in DRAM
    ///
    explicit _superEchoAlgorithm(Echo* apu_) : apu(apu_) { }

    /// the echo emulator
    /// @details
    /// The emulator owns the chip's 64KB echo buffer, which is far too large
    /// for the algorithm's SRAM allocation.
    Echo* apu;
};

/// @brief Return the delay register value.
///
/// @param self the algorithm to read parameters from
/// @param cv the delay CV in volts
/// @returns the delay in units of Echo::MILLISECONDS_PER_DELAY_LEVEL
///
static inline uint8_t getDelay(const _superEchoAlgorithm* self, float cv) {
    static constexpr float MAX = Echo::DELAY_LEVELS;
    const float mod = MAX * Math::Eurorack::fromDC(cv);
    return Math::clip(self->v[kParamDelay] + mod, 0.f, MAX);
}

/// @brief Return an 8-bit signed register value from a parameter and its CV.
///
/// @param param the value of the parameter
/// @param cv the voltage on the parameter's CV input
/// @param att the attenuation to apply to the CV, where 1 is unity
/// @returns the register value
///
static inline int8_t getSigned(int16_t param, float cv, float att = 1.f) {
    const float mod = att * 127.f * Math::Eurorack::fromDC(cv);
    return Math::clip(param + mod, -128.f, 127.f);
}

void calculateRequirements(
    _NT_algorithmRequirements& req,
    const int32_t* specifications
) {
    req.numParameters = ARRAY_SIZE(parameters);
    req.sram = sizeof(_superEchoAlgorithm);
    // the emulator owns the chip's echo buffer
    req.dram = sizeof(Echo);
    req.dtc = 0;
    req.itc = 0;
}

_NT_algorithm* construct(
    const _NT_algorithmMemoryPtrs& ptrs,
    const _NT_algorithmRequirements& req,
    const int32_t* specifications
) {
    Echo* apu = new (ptrs.dram) Echo();
    _superEchoAlgorithm* alg = new (ptrs.sram) _superEchoAlgorithm(apu);
    alg->parameters = parameters;
    alg->parameterPages = &parameterPages;
    return alg;
}

void step(_NT_algorithm* self, float* busFrames, int numFramesBy4) {
    _superEchoAlgorithm* pThis = (_superEchoAlgorithm*)self;
    const int16_t* v = pThis->v;
    const unsigned numFrames = numFramesBy4 * 4;
    Echo& apu = *pThis->apu;

    // resolve the busses once per step rather than once per frame
    const float* in[CHANNELS];
    const float* mix[CHANNELS];
    float* out[CHANNELS];
    bool replace[CHANNELS];
    float gain[CHANNELS];
    for (unsigned channel = 0; channel < CHANNELS; channel++) {
        in[channel] = NTPotatoChips::inputBus(busFrames, numFrames, v[channelParam(channel, kChannelInput)]);
        mix[channel] = NTPotatoChips::inputBus(busFrames, numFrames, v[channelParam(channel, kChannelMixInput)]);
        out[channel] = NTPotatoChips::outputBus(busFrames, numFrames, v[channelParam(channel, kChannelOutput)]);
        replace[channel] = v[channelParam(channel, kChannelOutputMode)];
        gain[channel] = v[channelParam(channel, kChannelGain)] / 100.f;
    }
    const float* delay = NTPotatoChips::inputBus(busFrames, numFrames, v[kParamDelayInput]);
    const float* feedback = NTPotatoChips::inputBus(busFrames, numFrames, v[kParamFeedbackInput]);
    const float* coefficient[FIR_COEFFICIENT_COUNT];
    for (unsigned i = 0; i < FIR_COEFFICIENT_COUNT; i++)
        coefficient[i] = NTPotatoChips::inputBus(busFrames, numFrames, v[coefficientParam(i, kCoefficientInput)]);

    const bool bypass = v[kParamBypass];

    for (unsigned frame = 0; frame < numFrames; frame++) {
        // the FIR coefficients are updated even in bypass, as in the Rack
        // build, so that the filter is primed when bypass is released
        for (unsigned i = 0; i < FIR_COEFFICIENT_COUNT; i++) {
            apu.setFIR(i, getSigned(
                v[coefficientParam(i, kCoefficientValue)],
                NTPotatoChips::voltage(coefficient[i], frame, 0.f),
                v[coefficientParam(i, kCoefficientAtt)] / 100.f
            ));
        }
        if (bypass) {
            for (unsigned channel = 0; channel < CHANNELS; channel++) {
                NTPotatoChips::write(
                    out[channel],
                    frame,
                    gain[channel] * NTPotatoChips::voltage(in[channel], frame, 0.f),
                    replace[channel]
                );
            }
            continue;
        }
        apu.setDelay(getDelay(
            pThis, NTPotatoChips::voltage(delay, frame, 0.f)
        ));
        apu.setFeedback(getSigned(
            v[kParamFeedback], NTPotatoChips::voltage(feedback, frame, 0.f)
        ));
        apu.setMixLeft(getSigned(
            v[channelParam(SonyS_DSP::StereoSample::LEFT, kChannelMix)],
            NTPotatoChips::voltage(mix[SonyS_DSP::StereoSample::LEFT], frame, 0.f)
        ));
        apu.setMixRight(getSigned(
            v[channelParam(SonyS_DSP::StereoSample::RIGHT, kChannelMix)],
            NTPotatoChips::voltage(mix[SonyS_DSP::StereoSample::RIGHT], frame, 0.f)
        ));
        // the emulator works in 16-bit fixed point
        static constexpr float MAX = 32767.f;
        int16_t input[CHANNELS];
        for (unsigned channel = 0; channel < CHANNELS; channel++) {
            const float sample = gain[channel] * Math::Eurorack::fromAC(
                NTPotatoChips::voltage(in[channel], frame, 0.f)
            );
            input[channel] = MAX * Math::clip(sample, -1.f, 1.f);
        }
        const SonyS_DSP::StereoSample output = apu.run(
            input[SonyS_DSP::StereoSample::LEFT],
            input[SonyS_DSP::StereoSample::RIGHT]
        );
        for (unsigned channel = 0; channel < CHANNELS; channel++) {
            NTPotatoChips::write(
                out[channel],
                frame,
                Math::Eurorack::toAC(output.samples[channel] / MAX),
                replace[channel]
            );
        }
    }
}

static const _NT_factory factory = {
    .guid = NT_MULTICHAR( 'P', 'C', 'e', 'c' ),
    .name = "Super Echo",
    .description = "Sony S-DSP echo with an 8-tap FIR filter (Nintendo SNES)",
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
    .tags = kNT_tagEffect | kNT_tagDelay,
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
