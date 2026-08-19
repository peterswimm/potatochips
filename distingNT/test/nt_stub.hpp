// Configuration for the host-side stand-in for the disting NT firmware.
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

#ifndef NT_STUB_HPP_
#define NT_STUB_HPP_

/// the sample rate the module runs its algorithms at
#define NT_STUB_SAMPLE_RATE 48000

/// the largest number of frames the module renders in one step() call
#define NT_STUB_MAX_FRAMES_PER_STEP 24

/// the size of the scratch buffer the module offers during step()
#define NT_STUB_WORK_BUFFER_BYTES 8192

#endif  // NT_STUB_HPP_
