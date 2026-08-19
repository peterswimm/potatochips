// Compatibility shim that lets the PotatoChips DSP headers compile for the
// disting NT (bare-metal Cortex-M7, -fno-exceptions -fno-rtti).
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

#ifndef NT_POTATOCHIPS_COMPAT_HPP_
#define NT_POTATOCHIPS_COMPAT_HPP_

// The DSP headers pull these in themselves. Include them *before* the `throw`
// neutralisation below so that nothing in the standard library is parsed with
// the macro in effect.
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <complex>
#include <limits>
#include <algorithm>
#include <new>

// newlib hides the POSIX math constants under a strict-ANSI dialect, which
// -std=c++11 selects. `dsp/yamaha_ym2612/tables.hpp` needs M_PI.
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ---------------------------------------------------------------------------
// MARK: Rack stand-ins
// ---------------------------------------------------------------------------

// `dsp/math/eurorack.hpp` and `dsp/mi_edges/wavetable.hpp` reference
// `rack::dsp::FREQ_C4`. That single constant is the only piece of VCV Rack the
// DSP layer touches, so define it rather than carry a Rack dependency.
namespace rack {
namespace dsp {
/// the frequency of the note C4 in Hz
static constexpr float FREQ_C4 = 261.6256f;
}  // namespace dsp
}  // namespace rack

// ---------------------------------------------------------------------------
// MARK: Exceptions
// ---------------------------------------------------------------------------

// Claim `dsp/exceptions.hpp`'s include guard so that the real header (which
// needs std::string and a base `Exception` supplied by rack.hpp) is skipped,
// and provide inert stand-ins for the three exception types the chip emulators
// name. The arguments are accepted and discarded.
#define DSP_EXCEPTIONS_HPP_

/// @brief An inert stand-in for the exceptions thrown by the DSP layer.
/// @details
/// Exceptions are unavailable on the disting NT. The chip emulators only throw
/// on out-of-range arguments, which the algorithm wrappers in this port are
/// responsible for never producing; constructing one of these is a no-op.
struct Exception {
    explicit Exception(const char*) { }
};

/// @brief An inert stand-in for an out-of-bounds channel index.
struct ChannelOutOfBoundsException : public Exception {
    ChannelOutOfBoundsException(unsigned, unsigned) : Exception("channel") { }
};

/// @brief An inert stand-in for an out-of-bounds address.
template<typename Address>
struct AddressSpaceException : public Exception {
    AddressSpaceException(Address, Address, Address) : Exception("address") { }
};

// Turn `throw SomeException(...)` in the DSP headers into an evaluated-and-
// discarded expression so those headers compile unmodified under
// -fno-exceptions. Constructing the stand-ins above has no side effects, so
// the statement becomes dead code the optimiser removes.
#define throw (void)

#endif  // NT_POTATOCHIPS_COMPAT_HPP_
