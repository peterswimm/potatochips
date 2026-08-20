# Potato Chips for the disting NT

A native port of the Potato Chips chip emulators to the
[Expert Sleepers disting NT](https://www.expert-sleepers.co.uk/distingNT.html)
plug-in API. Each of the sixteen VCV Rack modules becomes one NT algorithm.

The emulators themselves are the same code the Rack plug-in runs: everything
under [`../src/dsp`](../src/dsp) is header-only and independent of Rack, so this
port reuses it and re-implements only the module layer — the part that reads a
panel and drives the chip's registers. The only changes to the shared DSP are
the memory-safety fixes listed under
[Differences from the Rack build](#bugs-the-port-fixed-in-shared-code), which
building for a bare-metal target brought to light.

## Building

You need the [GNU Arm Embedded
Toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads)
(`arm-none-eabi-c++`) and the disting NT API headers, which ship as a submodule
of this repository:

```shell
git submodule update --init dep/distingNT_API
make -C distingNT
```

That writes one `.o` per algorithm into `distingNT/build`. Set `NT_API_PATH` to
build against a different checkout of the API:

```shell
make -C distingNT NT_API_PATH=/path/to/distingNT_API
```

## Installing

Copy the `.o` files you want onto the module's MicroSD card, under
`/programs/plug-ins/`, and either reboot the module or reload the plug-ins from
its menu. The algorithms then appear in the `Add algorithm` menu under the names
in the table below. Each file is independent, so you can install only the chips
you use.

## Algorithms

| Algorithm | GUID | Chip | Voices |
|:--|:--|:--|:--|
| Blocks | `PCbl` | Mutable Instruments Edges digital oscillator | 4 |
| Boss Fight | `PCbf` | Yamaha YM2612 (Sega Mega Drive) | 1 (4 operators) |
| Infinite Stairs | `PC2A` | Ricoh 2A03 (Nintendo Entertainment System) | 4 |
| Jairasullator | `PCay` | General Instrument AY-3-8910 (MSX, ZX Spectrum) | 3 |
| Mega Tone | `PCsn` | Texas Instruments SN76489 (Sega Master System) | 4 |
| Mini Boss | `PCmb` | Yamaha YM2612, single operator | 1 |
| Name Corp Octal Wave Generator | `PC63` | Namco 163 (NES cartridges) | 8 |
| Pallet Town Waves System | `PCgb` | Nintendo GameBoy Sound System | 4 |
| Pot Keys | `PCpk` | Atari POKEY (Atari 8-bit, Atari 5200) | 4 |
| Pulses | `PCf7` | SunSoft FME7 (NES cartridges) | 3 |
| Step Saw | `PCV6` | Konami VRC6 (Akumajou Densetsu) | 3 |
| Super ADSR | `PCad` | Sony S-DSP envelope generator (SNES) | 2 |
| Super Echo | `PCec` | Sony S-DSP echo with an 8-tap FIR filter | stereo |
| Super Sampler | `PCsp` | Sony S-DSP BRR sample player | 8 |
| Super Synth | `PCsy` | Sony S-DSP with echo | 8 |
| Super VCA | `PCvc` | Sony S-DSP Gaussian low-pass gate | 2 |

## How the panel maps onto parameters

The Rack modules are knobs and jacks; NT algorithms are parameters and busses.
The port follows the same conventions throughout, so once you have read one
algorithm's parameter pages the rest are predictable.

**Pitch.** A Rack frequency knob spans a few octaves around C4. Each becomes a
`coarse` parameter in semitones and a `fine` parameter in cents, plus a `V/Oct`
parameter naming an input bus. The three sum, so `coarse` and `fine` set the
note and the bus tracks it at 1V per octave.

**CV inputs.** Every Rack input port becomes a bus-selector parameter, named
after the thing it modulates and suffixed `input` where the parameter itself
also exists (`level` and `level input`). `None` leaves the parameter to act
alone. Where the Rack module normals an unpatched port to a fixed voltage — 10V
for a level, 5V for an FM attenuverter — the port uses the same voltage, so an
unrouted input behaves as it does with an unpatched jack.

**Attenuverters.** Rack's ±1 attenuverter knobs become ±1.00 parameters, shown
with two decimal places.

**Outputs.** Every Rack output port becomes a bus-selector parameter and an
output mode (`add` or `replace`). Where a Rack module mixes an unpatched output
into the next patched one — most of the chip oscillators do, so that a single
cable gives you the whole chip — an unrouted output is summed into the next
routed one in the same way.

**Registers.** Parameters that write a chip register directly keep the chip's
own range and units rather than being rescaled: a 4-bit level reads 0-15, the
YM2612's total level reads 0-100, the S-DSP's signed levels read -128 to 127.

**Buttons.** Rack's mode buttons cycle through a list on each press. On the NT
those are ordinary enumerated parameters — the AY-3-8910's eight envelope
shapes, the GameBoy's duty cycles, the S-DSP's four filter modes — which you can
also address from the mapping menu.

## Memory

Nothing in the port allocates: the emulators are constructed in the memory the
module hands to `construct()`, and the built plug-ins reference neither
`operator new` nor `malloc`. Instances stay under 3KB of SRAM. The emulators
that own large tables or buffers take DRAM instead:

| Algorithm | DRAM | What it holds |
|:--|--:|:--|
| Pot Keys | 17,244 | the POKEY's polynomial tables and synthesizers |
| Boss Fight | 17,816 | the YM2612's sine and envelope tables |
| Mini Boss | 17,544 | the YM2612's sine and envelope tables |
| Super Echo | 63,580 | the echo delay line |
| Super Sampler | 65,536 | the chip's sample RAM |
| Super Synth | 66,028 | the chip's RAM, and the emulator itself |

The other ten algorithms need no DRAM at all.

Beyond `NT_globals`, the plug-ins reference only standard library symbols the
firmware provides: `memcpy`, `memmove`, `memset`, and `cos`, `sin`, `pow`,
`powf`, `log`, `log2f` for the emulators' table generation.

## Testing

The algorithms can be exercised on a host machine, away from the module:

```shell
make -C distingNT/test
```

Each algorithm is linked into its own executable together with a stand-in for
the firmware, built with the address and undefined-behaviour sanitizers. The
harness plays the part of the module: it asks the plug-in for its factories,
allocates exactly the memory each one declares with guard regions around it,
feeds parameter defaults and busses, and renders 256 steps in three passes —
the defaults with quiet inputs, every input routed to a bus held at 5V, and
eight rounds of pseudo-random parameter values. It checks that

-   the parameter tables are self-consistent and the pages only name
    parameters that exist,
-   nothing writes outside the memory it asked for,
-   every sample written to a bus is finite and not a runaway, and
-   an algorithm tagged as an instrument actually makes a sound.

Pass a seed to widen the random pass, e.g. `./build/SuperEcho 0xBEEF`; the
default seed keeps runs reproducible.

What it cannot check is whether an algorithm *sounds* like its Rack
counterpart. That needs the module.

Both builds and both test suites run in CI on every pull request — see
[`.github/workflows/build.yml`](../.github/workflows/build.yml). The disting NT
job also asserts that the Arm build stays warning-free and that no object
references an allocator.

## Differences from the Rack build

The port reproduces the Rack modules' register math as it stands, including
where that math is idiosyncratic. These are the places it does not.

**Polyphony.** Rack modules run up to sixteen polyphonic channels per module.
The NT has one signal per bus, so each algorithm hosts a single emulator. Add
the algorithm more than once for more voices.

**No sample rate tracking of the envelopes.** As in the Rack build, the S-DSP
envelope generators are clocked once per output sample rather than at the chip's
own 32kHz, so envelope times follow the module's sample rate.

**Super VCA's loudness compensation is not a bug.** An earlier version of this
document claimed the compensation ran the wrong way. It does not:
`GaussianInterpolationFilter::getFilterLabel()` names the *emulator's* filter
value, where 0 is "Barely Audible" and 3 is "Loud", so `setFilter(3 - mode)`
pairs the quietest filter with the largest `2^mode` boost. Measured, the raw
output halves as the filter weakens and the compensation equalises the first
three modes to within 5%. The port keeps the mapping unchanged.

**Two upstream bugs are not carried over.** Super VCA passes its frequency to
the filter in Hz; the Rack build converts to a pitch register value first and
then hands that to `setFrequency()`, which converts it again, leaving the filter
roughly eight times too slow and quantised too coarsely to track V/Oct. Super
Sampler ignores the phase modulation switch on its first voice, which has no
preceding voice to modulate it; the Rack build reads one voice below its array
there.

### Bugs the port fixed in shared code

Running the algorithms under the sanitizers turned up memory errors in the
emulators that the Rack build shares. All of these are fixed on this branch, and
all of them affect the Rack build too:

-   `BLIPBuffer::read_sample()` shifted one element too many, and cleared one
    element past the end of its buffer — on every sample, for every buffer. In
    the Rack build that write lands in the next buffer's first field; here it
    landed in the chip emulator that follows the buffers and zeroed an
    oscillator's output pointer.
-   `BLIPSynthesizer::adjust_impulse()` ran its phase loop one step wide at both
    ends, reading before and writing past its impulse table — and, because of
    the same off-by-one, never reached the phase-0.5 case that halves the error
    correction.
-   `Ricoh2A03::Oscillator::reset()` was missing its `= 0`, so it copied the
    uninitialised fourth register across the other three instead of clearing
    them, and left the `reg_written` flags holding whatever was in memory.
-   `NintendoGBS`'s register count was the difference of its address bounds
    rather than the span, one short of the inclusive range its own `write()`
    accepts, so writing the last wave-RAM byte wrote past the array.
-   `DigitalOscillator` took the note that selects its band-limited wave-table
    straight from the pitch, unbounded. A frequency of zero or a large control
    voltage indexed past the end of the table; a table lookup on the resulting
    pointer segfaulted.
-   The S-DSP, the echo and the YM2612 shifted signed values left and overflowed
    signed products in their filter and modulation paths. The shifts are now
    multiplies by the same powers of two, which are exact at these magnitudes,
    and the products are computed wide before being clamped.

### The built-in sample

Super Sampler's built-in wave was packed into its BRR nibbles wrongly, in two
ways that compounded: neither nibble was masked to four bits, so a negative
sample's sign extension filled its neighbour, and the pair was written in the
opposite order to the one the decoder reads — it takes a byte's high nibble
before its low one, so the earlier sample belongs in the high nibble. Decoding
the shipped bytes back reproduces 42.5% of the 13,083 samples incorrectly.
Masking alone makes it slightly worse (44.7%); both together give an exact
reproduction. Fixed in the Rack module and the algorithm alike, so the sample
plays as recorded — it does not sound as it did before.

Some things the Rack panels offer have no equivalent here. The GameBoy and
Namco 163 modules let you draw their wave-tables; this port morphs between the
five built-in tables those modules start with, which is what the `Waveform`
parameter selects. The VU meters and stage LEDs are not drawn — the algorithms
use the standard parameter display rather than a custom one.

## Layout

```
distingNT/
├── Makefile                            build every plug-in for the Cortex-M7
├── include/nt_potatochips/
│   ├── compat.hpp                      lets the DSP headers build bare-metal
│   └── chip.hpp                        busses, pitch, and emulator hosting
├── plugins/                            one algorithm per file
└── test/                               the host-side harness
    ├── harness.cpp                     stands in for the module
    └── nt_stub.cpp                     stands in for the firmware
```

`compat.hpp` closes the two gaps between the DSP layer and a bare-metal target.
The emulators name three exception types on their out-of-range paths and the NT
builds with `-fno-exceptions`, so it supplies inert stand-ins and neutralises
the `throw` statements; and it defines `rack::dsp::FREQ_C4`, the one Rack
constant the DSP layer reads.

`chip.hpp` stands in for the Rack build's `ChipModule` base class: resolving bus
parameters to frames, converting coarse and fine parameters to frequencies,
rendering the Blargg-style emulators through one `BLIPBuffer` per oscillator at
the 768kHz chip clock, dividing control-rate register updates down by sixteen as
the Rack build does, and writing oscillator samples out with the normalling
behaviour described above.

## License

Same as the rest of the repository: GPL-3.0-or-later. The disting NT API headers
in `dep/distingNT_API` are MIT licensed by Expert Sleepers Ltd.
