// A Sony S-DSP envelope generator algorithm for the disting NT.
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
#include "dsp/sony_s_dsp/adsr.hpp"
#include "dsp/trigger/threshold.hpp"

/// the number of processing lanes in the algorithm
static constexpr unsigned LANES = 2;

// ---------------------------------------------------------------------------
// MARK: Parameters
// ---------------------------------------------------------------------------

/// the parameters of one lane, in the order they are declared
enum {
    kLaneAmplitude,
    kLaneAttack,
    kLaneDecay,
    kLaneSustainLevel,
    kLaneSustainRate,
    kLaneGate,
    kLaneTrigger,
    kLaneOutput,
    kLaneOutputMode,
    kLaneInverted,
    kLaneInvertedMode,
    kNumLaneParams
};

/// @brief Return the index of a parameter within a lane's block.
///
/// @param lane the index of the lane
/// @param param the index of the parameter within the lane's block
/// @returns the index of the parameter within the algorithm
///
static constexpr uint8_t laneParam(unsigned lane, unsigned param) {
    return lane * kNumLaneParams + param;
}

enum { kNumParameters = LANES * kNumLaneParams };

/// @brief Declare the parameters of one lane.
/// @param NAME the display prefix for the lane, e.g., "1"
/// @param OUT the default output bus for the lane
/// @param INV the default inverted output bus for the lane
#define LANE_PARAMETERS( NAME, OUT, INV ) \
    { .name = NAME " amplitude", .min = -128, .max = 127, .def = 127, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL }, \
    { .name = NAME " attack", .min = 0, .max = 15, .def = 10, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL }, \
    { .name = NAME " decay", .min = 0, .max = 7, .def = 7, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL }, \
    { .name = NAME " sustain", .min = 0, .max = 7, .def = 5, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL }, \
    { .name = NAME " sustain rate", .min = 0, .max = 31, .def = 20, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL }, \
    NT_PARAMETER_CV_INPUT( NAME " gate", 0, 0 ) \
    NT_PARAMETER_CV_INPUT( NAME " trigger", 0, 0 ) \
    NT_PARAMETER_CV_OUTPUT_WITH_MODE( NAME " output", 0, OUT ) \
    NT_PARAMETER_CV_OUTPUT_WITH_MODE( NAME " inverted", 0, INV )

static const _NT_parameter parameters[] = {
    LANE_PARAMETERS( "1", 13, 0 )
    LANE_PARAMETERS( "2", 14, 0 )
};

static_assert(
    ARRAY_SIZE(parameters) == kNumParameters,
    "the parameter table and the parameter enum disagree"
);

/// @brief Declare the page contents of one lane.
/// @param LANE the index of the lane
#define LANE_PAGE( LANE ) { \
    laneParam(LANE, kLaneAmplitude), \
    laneParam(LANE, kLaneAttack), \
    laneParam(LANE, kLaneDecay), \
    laneParam(LANE, kLaneSustainLevel), \
    laneParam(LANE, kLaneSustainRate), \
    laneParam(LANE, kLaneGate), \
    laneParam(LANE, kLaneTrigger), \
    laneParam(LANE, kLaneOutput), \
    laneParam(LANE, kLaneOutputMode), \
    laneParam(LANE, kLaneInverted), \
    laneParam(LANE, kLaneInvertedMode), \
}

static const uint8_t pageLane1[] = LANE_PAGE( 0 );
static const uint8_t pageLane2[] = LANE_PAGE( 1 );

static const _NT_parameterPage pages[] = {
    { .name = "Envelope 1", .numParams = ARRAY_SIZE(pageLane1), .params = pageLane1 },
    { .name = "Envelope 2", .numParams = ARRAY_SIZE(pageLane2), .params = pageLane2 },
};

static const _NT_parameterPages parameterPages = {
    .numPages = ARRAY_SIZE(pages),
    .pages = pages,
};

// ---------------------------------------------------------------------------
// MARK: Algorithm
// ---------------------------------------------------------------------------

/// @brief The state of a Super ADSR instance.
struct _superADSRAlgorithm : public _NT_algorithm {
    /// @brief Initialize a new instance.
    _superADSRAlgorithm() { }

    /// the envelope generators
    SonyS_DSP::ADSR apu[LANES];

    /// triggers for the gate inputs
    Trigger::Threshold gateTriggers[LANES];

    /// triggers for the re-trigger inputs
    Trigger::Threshold retriggerTriggers[LANES];
};

void calculateRequirements(
    _NT_algorithmRequirements& req,
    const int32_t* specifications
) {
    req.numParameters = ARRAY_SIZE(parameters);
    req.sram = sizeof(_superADSRAlgorithm);
    req.dram = 0;
    req.dtc = 0;
    req.itc = 0;
}

_NT_algorithm* construct(
    const _NT_algorithmMemoryPtrs& ptrs,
    const _NT_algorithmRequirements& req,
    const int32_t* specifications
) {
    _superADSRAlgorithm* alg = new (ptrs.sram) _superADSRAlgorithm();
    alg->parameters = parameters;
    alg->parameterPages = &parameterPages;
    return alg;
}

void step(_NT_algorithm* self, float* busFrames, int numFramesBy4) {
    _superADSRAlgorithm* pThis = (_superADSRAlgorithm*)self;
    const int16_t* v = pThis->v;
    const unsigned numFrames = numFramesBy4 * 4;

    // resolve the busses once per step rather than once per frame
    const float* gate[LANES];
    const float* trigger[LANES];
    float* out[LANES];
    float* inverted[LANES];
    for (unsigned lane = 0; lane < LANES; lane++) {
        gate[lane] = NTPotatoChips::inputBus(busFrames, numFrames, v[laneParam(lane, kLaneGate)]);
        trigger[lane] = NTPotatoChips::inputBus(busFrames, numFrames, v[laneParam(lane, kLaneTrigger)]);
        out[lane] = NTPotatoChips::outputBus(busFrames, numFrames, v[laneParam(lane, kLaneOutput)]);
        inverted[lane] = NTPotatoChips::outputBus(busFrames, numFrames, v[laneParam(lane, kLaneInverted)]);
    }

    for (unsigned lane = 0; lane < LANES; lane++) {
        SonyS_DSP::ADSR& apu = pThis->apu[lane];
        // the envelope rates count down, so invert the parameters
        apu.setAttack(15 - v[laneParam(lane, kLaneAttack)]);
        apu.setDecay(7 - v[laneParam(lane, kLaneDecay)]);
        apu.setSustainRate(31 - v[laneParam(lane, kLaneSustainRate)]);
        apu.setSustainLevel(v[laneParam(lane, kLaneSustainLevel)]);
        apu.setAmplitude(v[laneParam(lane, kLaneAmplitude)]);
        const bool replace = v[laneParam(lane, kLaneOutputMode)];
        const bool replaceInverted = v[laneParam(lane, kLaneInvertedMode)];
        for (unsigned frame = 0; frame < numFrames; frame++) {
            // the envelope restarts on a rising edge of either input
            const bool gateEdge = pThis->gateTriggers[lane].process(
                NTPotatoChips::triggerSignal(
                    NTPotatoChips::voltage(gate[lane], frame, 0.f)
                )
            );
            const bool triggerEdge = pThis->retriggerTriggers[lane].process(
                NTPotatoChips::triggerSignal(
                    NTPotatoChips::voltage(trigger[lane], frame, 0.f)
                )
            );
            const float sample = apu.run(
                gateEdge || triggerEdge, pThis->gateTriggers[lane].isHigh()
            );
            const float voltage = Math::Eurorack::toDC(sample / 128.f);
            NTPotatoChips::write(out[lane], frame, voltage, replace);
            NTPotatoChips::write(inverted[lane], frame, -voltage, replaceInverted);
        }
    }
}

static const _NT_factory factory = {
    .guid = NT_MULTICHAR( 'P', 'C', 'a', 'd' ),
    .name = "Super ADSR",
    .description = "Sony S-DSP envelope generator (Nintendo SNES)",
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
    .tags = kNT_tagUtility,
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
