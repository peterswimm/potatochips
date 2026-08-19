// A Sony S-DSP low-pass gate algorithm for the disting NT.
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
#include "dsp/sony_s_dsp/gaussian_interpolation_filter.hpp"

/// the number of processing lanes in the algorithm
static constexpr unsigned LANES = 2;

/// the filter this algorithm hosts
typedef SonyS_DSP::GaussianInterpolationFilter Filter;

// ---------------------------------------------------------------------------
// MARK: Parameters
// ---------------------------------------------------------------------------

/// the global parameters, which precede the lane blocks
enum {
    kParamFilter,
    kParamBypass,
    kNumGlobalParams
};

/// the parameters of one lane, in the order they are declared
enum {
    kLaneInput,
    kLaneGain,
    kLaneCoarse,
    kLaneFine,
    kLaneVOct,
    kLaneVolume,
    kLaneVolumeInput,
    kLaneOutput,
    kLaneOutputMode,
    kNumLaneParams
};

/// @brief Return the index of a parameter within a lane's block.
///
/// @param lane the index of the lane
/// @param param the index of the parameter within the lane's block
/// @returns the index of the parameter within the algorithm
///
static constexpr uint8_t laneParam(unsigned lane, unsigned param) {
    return kNumGlobalParams + lane * kNumLaneParams + param;
}

enum { kNumParameters = kNumGlobalParams + LANES * kNumLaneParams };

static char const * const enumStringsOffOn[] = { "Off", "On" };

/// the four sets of filter coefficients, named for how loud they are
static char const * const enumStringsFilter[] = {
    "Loud",
    "Weird",
    "Quiet",
    "Barely audible",
};

/// @brief Declare the parameters of one lane.
/// @param NAME the display prefix for the lane, e.g., "Left"
/// @param OUT the default output bus for the lane
#define LANE_PARAMETERS( NAME, OUT ) \
    NT_PARAMETER_AUDIO_INPUT( NAME " input", 0, 0 ) \
    { .name = NAME " gain", .min = 0, .max = 200, .def = 100, .unit = kNT_unitNone, .scaling = kNT_scaling100, .enumStrings = NULL }, \
    { .name = NAME " coarse", .min = -60, .max = 60, .def = 0, .unit = kNT_unitSemitones, .scaling = 0, .enumStrings = NULL }, \
    { .name = NAME " fine", .min = -100, .max = 100, .def = 0, .unit = kNT_unitCents, .scaling = 0, .enumStrings = NULL }, \
    NT_PARAMETER_CV_INPUT( NAME " V/Oct", 0, 0 ) \
    { .name = NAME " level", .min = -128, .max = 127, .def = 60, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL }, \
    NT_PARAMETER_CV_INPUT( NAME " level input", 0, 0 ) \
    NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE( NAME " output", 0, OUT )

static const _NT_parameter parameters[] = {
    { .name = "Filter", .min = 0, .max = 3, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = enumStringsFilter },
    { .name = "Bypass", .min = 0, .max = 1, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = enumStringsOffOn },
    LANE_PARAMETERS( "Left", 13 )
    LANE_PARAMETERS( "Right", 14 )
};

static_assert(
    ARRAY_SIZE(parameters) == kNumParameters,
    "the parameter table and the parameter enum disagree"
);

/// @brief Declare the page contents of one lane.
/// @param LANE the index of the lane
#define LANE_PAGE( LANE ) { \
    laneParam(LANE, kLaneInput), \
    laneParam(LANE, kLaneGain), \
    laneParam(LANE, kLaneCoarse), \
    laneParam(LANE, kLaneFine), \
    laneParam(LANE, kLaneVOct), \
    laneParam(LANE, kLaneVolume), \
    laneParam(LANE, kLaneVolumeInput), \
    laneParam(LANE, kLaneOutput), \
    laneParam(LANE, kLaneOutputMode), \
}

static const uint8_t pageGlobal[] = { kParamFilter, kParamBypass };
static const uint8_t pageLeft[] = LANE_PAGE( 0 );
static const uint8_t pageRight[] = LANE_PAGE( 1 );

static const _NT_parameterPage pages[] = {
    { .name = "Filter", .numParams = ARRAY_SIZE(pageGlobal), .params = pageGlobal },
    { .name = "Left", .numParams = ARRAY_SIZE(pageLeft), .params = pageLeft },
    { .name = "Right", .numParams = ARRAY_SIZE(pageRight), .params = pageRight },
};

static const _NT_parameterPages parameterPages = {
    .numPages = ARRAY_SIZE(pages),
    .pages = pages,
};

// ---------------------------------------------------------------------------
// MARK: Algorithm
// ---------------------------------------------------------------------------

/// @brief The state of a Super VCA instance.
struct _superVCAAlgorithm : public _NT_algorithm {
    /// @brief Initialize a new instance.
    _superVCAAlgorithm() { }

    /// the filters, one per lane
    Filter apu[LANES];
};

/// @brief Return the level register value for a lane.
///
/// @param level the level parameter for the lane
/// @param cv the level CV in volts
/// @returns the 8-bit signed level
///
static inline int8_t getVolume(int16_t level, float cv) {
    return Math::clip(level * Math::Eurorack::fromDC(cv), -128.f, 127.f);
}

void calculateRequirements(
    _NT_algorithmRequirements& req,
    const int32_t* specifications
) {
    req.numParameters = ARRAY_SIZE(parameters);
    req.sram = sizeof(_superVCAAlgorithm);
    req.dram = 0;
    req.dtc = 0;
    req.itc = 0;
}

_NT_algorithm* construct(
    const _NT_algorithmMemoryPtrs& ptrs,
    const _NT_algorithmRequirements& req,
    const int32_t* specifications
) {
    _superVCAAlgorithm* alg = new (ptrs.sram) _superVCAAlgorithm();
    alg->parameters = parameters;
    alg->parameterPages = &parameterPages;
    return alg;
}

void step(_NT_algorithm* self, float* busFrames, int numFramesBy4) {
    _superVCAAlgorithm* pThis = (_superVCAAlgorithm*)self;
    const int16_t* v = pThis->v;
    const unsigned numFrames = numFramesBy4 * 4;

    const unsigned filterMode = v[kParamFilter];
    // the emulator uses the reciprocal of the mode, so the quiet modes get
    // less compensation than the loud ones
    const float loudnessCompensation = 1 << filterMode;
    const bool bypass = v[kParamBypass];

    for (unsigned lane = 0; lane < LANES; lane++) {
        const float* in = NTPotatoChips::inputBus(busFrames, numFrames, v[laneParam(lane, kLaneInput)]);
        const float* voct = NTPotatoChips::inputBus(busFrames, numFrames, v[laneParam(lane, kLaneVOct)]);
        const float* volume = NTPotatoChips::inputBus(busFrames, numFrames, v[laneParam(lane, kLaneVolumeInput)]);
        float* out = NTPotatoChips::outputBus(busFrames, numFrames, v[laneParam(lane, kLaneOutput)]);
        const bool replace = v[laneParam(lane, kLaneOutputMode)];
        const float gain = v[laneParam(lane, kLaneGain)] / 100.f;
        const float octaves = NTPotatoChips::pitch(
            v[laneParam(lane, kLaneCoarse)], v[laneParam(lane, kLaneFine)]
        );

        for (unsigned frame = 0; frame < numFrames; frame++) {
            const float input = gain * Math::Eurorack::fromAC(
                NTPotatoChips::voltage(in, frame, 0.f)
            );
            if (bypass) {
                NTPotatoChips::write(
                    out, frame, Math::Eurorack::toAC(input), replace
                );
                continue;
            }
            // NOTE: this differs from the Rack build, which passes an
            // already-converted pitch register value to setFrequency() and so
            // has SonyS_DSP::get_pitch() applied to it twice. That leaves the
            // filter running about eight times too slow and quantised far too
            // coarsely to track V/Oct. This port passes the frequency in Hz,
            // which is what setFrequency() documents and what the other S-DSP
            // algorithms do.
            pThis->apu[lane].setFrequency(
                Math::Eurorack::voct2freq(
                    octaves + NTPotatoChips::voltage(voct, frame, 0.f)
                )
            );
            pThis->apu[lane].setFilter(3 - filterMode);
            pThis->apu[lane].setVolume(
                getVolume(
                    v[laneParam(lane, kLaneVolume)],
                    NTPotatoChips::voltage(volume, frame, 10.f)
                )
            );
            // the filter takes an 8-bit sample and returns a 14-bit one
            const int8_t fixed = 127 * Math::clip(input, -1.f, 1.f);
            const float sample = loudnessCompensation *
                pThis->apu[lane].run(fixed) / static_cast<float>(1 << 14);
            NTPotatoChips::write(
                out, frame, Math::Eurorack::toAC(sample), replace
            );
        }
    }
}

static const _NT_factory factory = {
    .guid = NT_MULTICHAR( 'P', 'C', 'v', 'c' ),
    .name = "Super VCA",
    .description = "Sony S-DSP Gaussian low-pass gate (Nintendo SNES)",
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
    .tags = kNT_tagFilterEQ,
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
