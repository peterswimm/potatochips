// A minimal stand-in for the parts of the disting NT firmware that the ported
// algorithms reach for, so that they can be exercised on a host machine.
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

#include <distingnt/api.h>
#include "nt_stub.hpp"

/// the buffer backing NT_globals.workBuffer
static float workBuffer[NT_STUB_WORK_BUFFER_BYTES / sizeof(float)];

/// The globals the algorithms read at construction and render time.
const _NT_globals NT_globals = {
    .sampleRate = NT_STUB_SAMPLE_RATE,
    .maxFramesPerStep = NT_STUB_MAX_FRAMES_PER_STEP,
    .workBuffer = workBuffer,
    .workBufferSizeBytes = NT_STUB_WORK_BUFFER_BYTES,
    .streamSizeBytes = 0,
    .streamBufferSizeBytes = 0,
};

/// The screen the algorithms would draw into. None of the ported algorithms
/// define draw(), but the symbol keeps the stub complete.
uint8_t NT_screen[128 * 64];
