// A host-side harness that exercises one ported algorithm.
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
// Each ported algorithm is linked into its own copy of this harness, which
// stands in for the module: it asks the plug-in for its factories, honours the
// memory each one declares, feeds its parameter defaults and its busses, and
// renders. It checks what the module itself would otherwise discover the hard
// way:
//
// -   the parameter tables are self-consistent, and the parameter pages only
//     name parameters that exist
// -   construction stays inside the memory the algorithm asked for, which
//     guard regions around every allocation verify
// -   rendering produces audio that is finite and has not run away, from the
//     defaults, from saturated inputs, and from pseudo-random parameters; the
//     peak is reported, and noted when it passes the nominal output range
// -   an algorithm that claims to be an instrument actually makes a sound
//
// It cannot check that an algorithm sounds like its Rack counterpart; that
// needs the module.
//

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <distingnt/api.h>
#include "nt_stub.hpp"

// ---------------------------------------------------------------------------
// MARK: Guarded allocation
// ---------------------------------------------------------------------------

/// the number of bytes of guard to place either side of an allocation
static constexpr unsigned GUARD_BYTES = 64;

/// the byte to fill guard regions with
static constexpr uint8_t GUARD_BYTE = 0xA5;

/// the byte to fill allocations with, so that algorithms cannot rely on the
/// module handing them zeroed memory
static constexpr uint8_t POISON_BYTE = 0x5A;

/// @brief An allocation with a guard region either side of it.
struct GuardedAllocation {
    /// the whole block, guards included
    uint8_t* block = NULL;
    /// the number of bytes the algorithm asked for
    unsigned size = 0;

    /// @brief Allocate a block of the given size, with guards around it.
    ///
    /// @param size_ the number of bytes to make available to the algorithm
    ///
    void allocate(unsigned size_) {
        size = size_;
        if (!size) return;
        // over-allocate so that the pointer handed out is 8-byte aligned, as
        // the module's own allocations are
        block = static_cast<uint8_t*>(malloc(size + 2 * GUARD_BYTES));
        memset(block, GUARD_BYTE, size + 2 * GUARD_BYTES);
        memset(block + GUARD_BYTES, POISON_BYTE, size);
    }

    /// @brief Return the pointer to hand to the algorithm.
    uint8_t* pointer() const { return size ? block + GUARD_BYTES : NULL; }

    /// @brief Return true if both guard regions are still intact.
    bool intact() const {
        if (!size) return true;
        for (unsigned i = 0; i < GUARD_BYTES; i++) {
            if (block[i] != GUARD_BYTE) return false;
            if (block[GUARD_BYTES + size + i] != GUARD_BYTE) return false;
        }
        return true;
    }
};

// ---------------------------------------------------------------------------
// MARK: Reporting
// ---------------------------------------------------------------------------

/// the number of checks that have failed
static unsigned failures = 0;

/// @brief Report the result of one check.
///
/// @param condition the result of the check
/// @param context the algorithm the check applies to
/// @param message a description of what was checked
///
static void check(bool condition, const char* context, const char* message) {
    if (condition) return;
    printf("  FAIL  %s: %s\n", context, message);
    failures++;
}

// ---------------------------------------------------------------------------
// MARK: Parameter definitions
// ---------------------------------------------------------------------------

/// @brief Return true if a parameter names an input or output bus.
///
/// @param parameter the parameter to inspect
///
static bool isBusParameter(const _NT_parameter& parameter) {
    switch (parameter.unit) {
    case kNT_unitAudioInput:
    case kNT_unitCvInput:
    case kNT_unitAudioOutput:
    case kNT_unitCvOutput:
        return true;
    default:
        return false;
    }
}

/// @brief Return true if a parameter names an input bus.
///
/// @param parameter the parameter to inspect
///
static bool isInputParameter(const _NT_parameter& parameter) {
    return parameter.unit == kNT_unitAudioInput ||
        parameter.unit == kNT_unitCvInput;
}

/// @brief Check that an algorithm's parameters and pages are consistent.
///
/// @param algorithm the constructed algorithm to check
/// @param numParameters the number of parameters the algorithm declared
/// @param name the algorithm's name, for reporting
///
static void checkParameters(
    const _NT_algorithm* algorithm,
    unsigned numParameters,
    const char* name
) {
    for (unsigned i = 0; i < numParameters; i++) {
        const _NT_parameter& parameter = algorithm->parameters[i];
        char message[128];
        snprintf(message, sizeof message,
            "parameter %u (%s)", i, parameter.name ? parameter.name : "unnamed");
        check(parameter.name != NULL, name, message);
        check(parameter.min <= parameter.max, name, message);
        check(
            parameter.def >= parameter.min && parameter.def <= parameter.max,
            name, message
        );
        if (parameter.unit == kNT_unitEnum) {
            check(parameter.enumStrings != NULL, name, message);
            if (parameter.enumStrings) {
                // the host indexes the strings by value, so every value in
                // range needs one
                bool populated = true;
                for (int16_t v = parameter.min; v <= parameter.max; v++)
                    populated = populated && parameter.enumStrings[v] != NULL;
                check(populated, name, message);
            }
        }
        if (isBusParameter(parameter)) {
            check(parameter.min >= 0, name, message);
            check(parameter.max == kNT_lastBus, name, message);
        }
    }
    // every page must name parameters that exist
    const _NT_parameterPages* pages = algorithm->parameterPages;
    check(pages != NULL, name, "parameter pages");
    if (!pages) return;
    for (unsigned p = 0; p < pages->numPages; p++) {
        const _NT_parameterPage& page = pages->pages[p];
        char message[128];
        snprintf(message, sizeof message,
            "page %u (%s)", p, page.name ? page.name : "unnamed");
        check(page.name != NULL, name, message);
        check(page.numParams > 0, name, message);
        bool inRange = true;
        for (unsigned i = 0; i < page.numParams; i++)
            inRange = inRange && page.params[i] < numParameters;
        check(inRange, name, message);
    }
}

// ---------------------------------------------------------------------------
// MARK: Rendering
// ---------------------------------------------------------------------------

/// the number of step() calls to make in each rendering pass
static constexpr unsigned NUM_STEPS = 256;

/// the largest voltage the module's outputs can reach
/// @details
/// Nothing in the API caps what an algorithm may write to a bus; the module
/// clips at its converters. A peak past this is worth seeing but is not a
/// defect, so it is reported rather than failed.
static constexpr float NOMINAL_VOLTS = 12.f;

/// the magnitude past which an output is a runaway rather than a loud signal
static constexpr float RUNAWAY_VOLTS = 1000.f;

/// @brief The result of one rendering pass.
struct RenderResult {
    /// the largest absolute voltage any output bus reached
    float peak = 0.f;
    /// whether every sample written was finite
    bool finite = true;
};

/// @brief Render an algorithm for NUM_STEPS steps and measure its output.
///
/// @param factory the algorithm's factory
/// @param algorithm the constructed algorithm
/// @param values the parameter values the algorithm reads
/// @param numParameters the number of parameters
/// @param inputVolts the voltage to hold every input bus at
/// @returns what the algorithm wrote to the busses
///
static RenderResult render(
    const _NT_factory* factory,
    _NT_algorithm* algorithm,
    const int16_t* values,
    unsigned numParameters,
    float inputVolts
) {
    const unsigned numFrames = NT_globals.maxFramesPerStep;
    // the busses the module hands to step(), laid out one after another
    static float busFrames[kNT_lastBus * NT_STUB_MAX_FRAMES_PER_STEP];
    RenderResult result;
    for (unsigned step = 0; step < NUM_STEPS; step++) {
        // the module clears the output busses and fills the input busses
        // before each step
        for (unsigned bus = 0; bus < kNT_lastBus; bus++) {
            const bool isInput = bus < kNT_numInputBusses;
            for (unsigned frame = 0; frame < numFrames; frame++)
                busFrames[bus * numFrames + frame] = isInput ? inputVolts : 0.f;
        }
        factory->step(algorithm, busFrames, numFrames / 4);
        // measure whatever the algorithm wrote to the busses it was pointed at
        for (unsigned i = 0; i < numParameters; i++) {
            const _NT_parameter& parameter = algorithm->parameters[i];
            if (isBusParameter(parameter) && !isInputParameter(parameter)) {
                const int16_t bus = values[i];
                if (!bus) continue;
                for (unsigned frame = 0; frame < numFrames; frame++) {
                    const float sample =
                        busFrames[(bus - 1) * numFrames + frame];
                    if (!std::isfinite(sample)) {
                        result.finite = false;
                        continue;
                    }
                    const float magnitude = std::fabs(sample);
                    if (magnitude > result.peak) result.peak = magnitude;
                }
            }
        }
    }
    return result;
}

/// @brief Report and check the result of a rendering pass.
///
/// @param result the result to check
/// @param name the algorithm's name, for reporting
/// @param pass a description of the pass, for reporting
///
static void checkRender(
    const RenderResult& result,
    const char* name,
    const char* pass
) {
    char message[128];
    snprintf(message, sizeof message, "%s produced a non-finite sample", pass);
    check(result.finite, name, message);
    snprintf(message, sizeof message,
        "%s ran away to %.2fV", pass, result.peak);
    check(result.peak <= RUNAWAY_VOLTS, name, message);
    printf("  %-24s peak %6.2fV%s\n", pass, result.peak,
        result.peak > NOMINAL_VOLTS ? "  (past the nominal output range)" : "");
}

// ---------------------------------------------------------------------------
// MARK: Main
// ---------------------------------------------------------------------------

/// the pseudo-random state, seeded so that runs are reproducible by default
static uint32_t randomState = 0x13375EED;

/// @brief Return the next pseudo-random number.
static uint32_t nextRandom() {
    // xorshift32
    randomState ^= randomState << 13;
    randomState ^= randomState >> 17;
    randomState ^= randomState << 5;
    return randomState;
}

int main(int argc, char** argv) {
    // an optional seed widens the pseudo-random pass; the default keeps runs
    // reproducible
    if (argc > 1) randomState = strtoul(argv[1], NULL, 0) | 1;
    check(
        pluginEntry(kNT_selector_version, 0) == kNT_apiVersionCurrent,
        "plug-in", "reports the API version it was built against"
    );
    const unsigned numFactories = pluginEntry(kNT_selector_numFactories, 0);
    check(numFactories > 0, "plug-in", "defines at least one factory");

    for (unsigned f = 0; f < numFactories; f++) {
        const _NT_factory* factory = reinterpret_cast<const _NT_factory*>(
            pluginEntry(kNT_selector_factoryInfo, f)
        );
        check(factory != NULL, "plug-in", "returns each factory it declares");
        if (!factory) continue;
        const char* name = factory->name ? factory->name : "unnamed";
        printf("%s (%c%c%c%c)\n", name,
            static_cast<char>(factory->guid & 0xff),
            static_cast<char>((factory->guid >> 8) & 0xff),
            static_cast<char>((factory->guid >> 16) & 0xff),
            static_cast<char>((factory->guid >> 24) & 0xff));
        check(factory->name != NULL, name, "declares a name");
        check(factory->description != NULL, name, "declares a description");
        check(factory->construct != NULL, name, "declares construct()");
        check(factory->step != NULL, name, "declares step()");
        if (!factory->construct || !factory->step) continue;

        // the module asks how much memory the algorithm needs, then hands it
        // exactly that much
        _NT_algorithmRequirements req;
        memset(&req, 0, sizeof req);
        factory->calculateRequirements(req, NULL);
        check(req.numParameters > 0, name, "declares parameters");
        check(req.sram >= sizeof(_NT_algorithm), name, "declares enough SRAM");
        printf("  memory                   sram %u dram %u dtc %u itc %u\n",
            req.sram, req.dram, req.dtc, req.itc);

        GuardedAllocation sram, dram, dtc, itc;
        sram.allocate(req.sram);
        dram.allocate(req.dram);
        dtc.allocate(req.dtc);
        itc.allocate(req.itc);
        _NT_algorithmMemoryPtrs ptrs;
        ptrs.sram = sram.pointer();
        ptrs.dram = dram.pointer();
        ptrs.dtc = dtc.pointer();
        ptrs.itc = itc.pointer();

        _NT_algorithm* algorithm = factory->construct(ptrs, req, NULL);
        check(algorithm != NULL, name, "constructs an algorithm");
        if (!algorithm) continue;
        check(
            reinterpret_cast<uint8_t*>(algorithm) == ptrs.sram,
            name, "places the algorithm in the memory it was given"
        );
        check(algorithm->parameters != NULL, name, "populates parameters");
        checkParameters(algorithm, req.numParameters, name);

        // the module owns the parameter values; stand in for it
        int16_t* values = static_cast<int16_t*>(
            calloc(req.numParameters, sizeof(int16_t))
        );
        algorithm->v = values;

        // pass one: the defaults, with every input bus at 0V
        for (unsigned i = 0; i < req.numParameters; i++)
            values[i] = algorithm->parameters[i].def;
        const RenderResult defaults =
            render(factory, algorithm, values, req.numParameters, 0.f);
        checkRender(defaults, name, "defaults, 0V in");

        // pass two: every input routed to a bus held at 5V, which reads as a
        // high gate, five octaves of pitch, or a saturated level
        for (unsigned i = 0; i < req.numParameters; i++) {
            if (isInputParameter(algorithm->parameters[i]))
                values[i] = 1;
        }
        const RenderResult saturated =
            render(factory, algorithm, values, req.numParameters, 5.f);
        checkRender(saturated, name, "inputs routed, 5V in");

        // pass three: pseudo-random values for everything that is not a bus,
        // to sweep the register ranges the algorithm writes
        float randomPeak = 0.f;
        bool randomFinite = true;
        for (unsigned round = 0; round < 8; round++) {
            for (unsigned i = 0; i < req.numParameters; i++) {
                const _NT_parameter& parameter = algorithm->parameters[i];
                if (isBusParameter(parameter)) continue;
                const int32_t span = parameter.max - parameter.min + 1;
                values[i] = parameter.min + nextRandom() % span;
            }
            const RenderResult result = render(
                factory, algorithm, values, req.numParameters,
                round & 1 ? 5.f : -5.f
            );
            randomFinite = randomFinite && result.finite;
            if (result.peak > randomPeak) randomPeak = result.peak;
        }
        RenderResult randomised;
        randomised.peak = randomPeak;
        randomised.finite = randomFinite;
        checkRender(randomised, name, "random parameters");

        // an instrument that never makes a sound is a bug
        if (factory->tags & kNT_tagInstrument) {
            check(
                defaults.peak > 0.f || saturated.peak > 0.f || randomPeak > 0.f,
                name, "makes a sound"
            );
        }

        // the algorithm must have stayed inside the memory it asked for
        check(sram.intact(), name, "stays within its SRAM allocation");
        check(dram.intact(), name, "stays within its DRAM allocation");
        check(dtc.intact(), name, "stays within its DTC allocation");
        check(itc.intact(), name, "stays within its ITC allocation");

        free(values);
        free(sram.block);
        free(dram.block);
        free(dtc.block);
        free(itc.block);
    }

    if (failures) {
        printf("%u check(s) failed\n", failures);
        return 1;
    }
    printf("  all checks passed\n");
    return 0;
}
