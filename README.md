**Table-of-Contents**
- [Usage](#usage)
- [Header Only Implementation](#header-only-implementation)
- [Build](#build)
- [XMI Specifications](#xmi-specifications)
  - [Multiple XMI Songs](#multiple-xmi-songs)
- [References](#references)
- [CHANGELOG](#changelog)
  - [2026-04-28](#2026-04-28)
  - [2026-04-24](#2026-04-24)
    - [Source](#source)
    - [Windows](#windows)
    - [Linux](#linux)
    - [Mac](#mac)
  - [2025](#2025)
  - [2023](#2023)
  - [2015](#2015)
  - [1994](#1994)

# Usage

Convert sequence 0 from `Reference/AIL2/DEMO.XMI`:

```cmd
xmi2mid.exe Reference\AIL2\DEMO.XMI demo.mid
```

```sh
./xmi2mid Reference/AIL2/DEMO.XMI demo.mid
```

List the XMI sequences:

```cmd
xmi2mid.exe --list Reference\AIL2\DEMO.XMI
```

```sh
./xmi2mid --list Reference/AIL2/DEMO.XMI
```

Convert one zero-based sequence explicitly:

```cmd
xmi2mid.exe --sequence 0 Reference\AIL2\DEMO.XMI demo.mid
```

```sh
./xmi2mid --sequence 0 Reference/AIL2/DEMO.XMI demo.mid
```

Convert every sequence to separate files:

```cmd
xmi2mid.exe --all Reference\AIL2\DEMO.XMI demo
```

```sh
./xmi2mid --all Reference/AIL2/DEMO.XMI demo
```

# Header Only Implementation

[xmi2mid.hpp](xmi2mid.hpp) provides the converter as a single-header C++20 API with no command-line handling, file I/O, or console output. Include it, pass a byte span containing an XMI file, and it returns a complete MIDI Format 0 file as bytes.

```cpp
#include "xmi2mid.hpp"

#include <cstdint>
#include <span>
#include <vector>

std::vector<std::uint8_t> xmiBytes = read_binary("Reference/AIL2/DEMO.XMI");

std::vector<std::uint8_t> midiBytes =
    xmi2mid::convert(std::span<const std::uint8_t>{xmiBytes.data(), xmiBytes.size()});

std::vector<std::uint8_t> firstSequence =
    xmi2mid::convert(std::span<const std::uint8_t>{xmiBytes.data(), xmiBytes.size()}, 0);

std::vector<std::vector<std::uint8_t>> allSequences =
    xmi2mid::convert_all(std::span<const std::uint8_t>{xmiBytes.data(), xmiBytes.size()});
```

The conversion functions throw `std::runtime_error` for invalid or truncated XMI data. Returned vectors are ready to write directly to `.mid` files, embed in another asset pipeline, or hand to a MIDI playback library.

# Build

Windows, from a normal, non-Administrator Developer PowerShell for Visual Studio 2022:

```cmd
.\build.cmd
.\build.cmd clean
.\build.cmd rebuild
```

`.\build.cmd` builds in your local `%TEMP%` directory, then copies `xmi2mid.exe` to the repository root. `.\build.cmd clean` removes build outputs from the repository root.

Linux:

```sh
./build.sh
./build.sh clean
./build.sh rebuild
```

`./build.sh` builds in your local temporary directory, then copies `xmi2mid` to the repository root. Set `CXX`, `CXXFLAGS`, or `LDFLAGS` to override the default compiler or flags.

macOS:

```sh
./build.command
./build.command clean
./build.command rebuild
```

`./build.command` asks Apple's toolchain for the default C++ compiler with `xcrun --find c++`, then falls back to `c++` if needed. If the compiler is missing, install Apple's Command Line Tools with `xcode-select --install`.

# XMI Specifications

XMIDI is the preprocessed MIDI sequence format used by the IBM Audio Interface Library 2.x and later Miles Sound System lineage. The primary source in this repository is John Miles' AIL2 release under [Reference/AIL2](Reference/AIL2), especially [XMIDI.TXT](Reference/AIL2/DOC/XMIDI.TXT), [TOOLS.TXT](Reference/AIL2/DOC/TOOLS.TXT), [API.TXT](Reference/AIL2/DOC/API.TXT), [MIDIFORM.C](Reference/AIL2/MIDIFORM.C), [XPLAY.C](Reference/AIL2/XPLAY.C), and [XMIDI.ASM](Reference/AIL2/XMIDI.ASM). External format summaries agree with the same overall structure [1][2].

XMIDI files are EA IFF-style containers. Chunk tags are four ASCII bytes, chunk lengths are 32-bit big-endian values, and chunk payloads are padded to an even byte boundary when another chunk follows. The AIL-local payload fields inside `INFO`, `TIMB`, and `RBRN` are DOS-native little-endian values in the original MIDIFORM output.

```text
[ FORM <len> XDIR
    INFO <len>
      uint16_le sequence_count
]

CAT  <len> XMID
  FORM <len> XMID          ; sequence 0
    [ TIMB <len> ]         ; required timbres: count, then patch/bank pairs
    [ RBRN <len> ]         ; branch index table for controller 120
    EVNT <len>             ; quantized event stream
  FORM <len> XMID          ; sequence 1, optional
    ...
```

`XDIR/INFO` is an application directory, not a playback requirement. The original AIL driver can find sequences by scanning the `CAT XMID` catalog directly, and it also accepts a bare `FORM XMID` image for a one-sequence file.

`EVNT` is the only mandatory local chunk in each `FORM XMID`, and it is written last by MIDIFORM. Its stream is MIDI-like, but not a Standard MIDI `MTrk` stream:

- Delay bytes have the high bit clear. Long XMI delays are a sum of repeated `0x7F` bytes plus the final byte. A zero delay is omitted entirely.
- MIDI running status is not used, because the high bit distinguishes delay bytes from event status bytes.
- Channel voice, System Exclusive, and meta events may appear. Standard Note Off events are removed.
- Note On events carry an extra MIDI variable-length duration after their normal status, note, and velocity bytes. Converters synthesize the matching MIDI Note Off from that duration.
- MIDIFORM's default quantization is 120 intervals per second. In this project, the emitted MIDI uses 960 PPQN, so one default XMI interval becomes 16 MIDI ticks at 500,000 microseconds per quarter note.
- Tempo meta-events are preserved. The converter rescales later MIDI deltas against the current tempo so the fixed 120 Hz XMI timing still plays with the same wall-clock duration in a Standard MIDI player.

AIL-specific controllers occupy MIDI Control Change numbers 110 through 120. They are meaningful to an AIL runtime, but most are just normal CC events to a Standard MIDI player.

| Controller | XMIDI meaning              |
| ---------- | -------------------------- |
| 110        | Channel Lock               |
| 111        | Channel Lock Protect       |
| 112        | Voice Protect              |
| 113        | Timbre Protect             |
| 114        | Patch Bank Select          |
| 115        | Indirect Controller Prefix |
| 116        | For Loop                   |
| 117        | Next/Break Loop            |
| 118        | Clear Beat/Bar Count       |
| 119        | Callback Trigger           |
| 120        | Sequence Branch Index      |

## Multiple XMI Songs

An XMI file can contain multiple songs because `CAT XMID` is a catalog of repeated `FORM XMID` sequence chunks. MIDIFORM creates this directly: it writes `FORM XDIR/INFO`, opens one `CAT XMID`, then appends one `FORM XMID` for each input MIDI file. The AIL API exposes this as a zero-based `sequence_num`; `XPLAY` passes the optional command-line sequence number to `AIL_register_sequence()`, and `XMIDI.ASM::find_seq` scans to the requested Nth `FORM XMID`.

# References

1. ["XMI Format."](https://moddingwiki.shikadi.net/wiki/XMI_Format) ModdingWiki. Synopsis: community-maintained technical notes on the XMI IFF layout, `XDIR`, `CAT XMID`, `TIMB`, `RBRN`, `EVNT`, note durations, and XMI delay encoding.
2. ["XMI."](https://vgmpf.com/Wiki/index.php?title=XMI) Video Game Music Preservation Foundation Wiki. Synopsis: preservation-oriented overview of XMI history, game usage, players/converters, IFF tree structure, 120 Hz timing, and multi-subsong layout.
3. [John Miles, KE5FX.](http://www.ke5fx.com/) Synopsis: John Miles' homepage and software archive, used here as the author/source reference point for the Audio Interface Library and Miles Sound System materials.

# CHANGELOG

## 2026-04-28

- Added root `xmi2mid.hpp` as a single-header C++20 conversion API.
- Exposed `xmi2mid::convert(std::span<const std::uint8_t>)` as the sequence-0 convenience conversion function.
- Exposed `xmi2mid::convert(std::span<const std::uint8_t>, std::size_t)` for explicit zero-based XMI sequence conversion.
- Exposed `xmi2mid::convert_all(std::span<const std::uint8_t>)` to return one MIDI byte vector per XMI sequence.
- Exposed `xmi2mid::sequence_count(std::span<const std::uint8_t>)` for lightweight sequence counting.
- Exposed `xmi2mid::sequence_infos(std::span<const std::uint8_t>)` for sequence metadata discovery.
- Added `xmi2mid::sequence_info` with sequence index, `FORM XMID` offset/size, `EVNT` offset/size, and `TIMB`/`RBRN` presence flags.
- Moved the reusable conversion boundary to the header-only API while keeping the CLI as a thin file-I/O and argument-parsing wrapper.
- Replaced the CLI's earlier fixed single-sequence chunk walk with the shared IFF sequence discovery logic from the header API.
- Added CLI `--list` support to print every discovered XMI sequence with `FORM` and `EVNT` offsets and sizes.
- Added CLI `--sequence N` support to convert a specific zero-based sequence from a multi-song XMI file.
- Added CLI `--all` support to convert every sequence in an XMI file to separate MIDI files.
- Kept the existing two-path CLI form as the default sequence-0 conversion path.
- Changed CLI success messages to report the converted sequence index.
- Added checked parsing for decimal sequence indexes, including empty, non-numeric, and overflow cases.
- Added out-of-range sequence index errors that report both the requested index and discovered sequence count.
- Added deterministic multi-sequence output naming with zero-padded suffixes such as `_00`, `_01`, and `_02`.
- Made `--all` write into an existing output directory when the output target is a directory.
- Made `--all` treat a non-directory output target as a filename stem while preserving a provided extension when present.
- Added support for bare `FORM XMID` inputs in addition to the common `FORM XDIR` plus `CAT XMID` container layout.
- Added support for optional or absent `TIMB` and `RBRN` chunks during sequence discovery.
- Preserved IFF odd-length chunk padding handling when scanning root chunks, catalog children, and sequence-local chunks.
- Added validation for truncated root IFF chunk headers, `CAT` child chunk headers, `FORM XMID` local chunk headers, and chunk payload lengths.
- Preserved MIDI Format 0 output, 960 PPQN timing, XMI note-duration handling, tempo rescaling, SysEx pass-through, meta-event pass-through, and pending note-off flushing in the header API.
- Updated README usage examples to use `Reference/AIL2/DEMO.XMI` consistently.
- Updated README usage documentation for default conversion, `--list`, `--sequence`, and `--all`.
- Added README documentation for the header-only API, including indexed conversion and all-sequence conversion examples.
- Added README XMI specification notes covering the EA IFF-style layout, `XDIR/INFO`, `CAT XMID`, repeated `FORM XMID` sequence chunks, `TIMB`, `RBRN`, and `EVNT`.
- Documented XMI event-stream behavior: summed delay bytes, omitted zero delays, no running status, Note On duration payloads, removed Note Off events, 120 Hz quantization, and MIDI delta scaling.
- Documented the AIL/XMIDI controller range from 110 through 120.
- Documented how multi-song XMI files are created and selected in the original AIL2 tooling and runtime.
- Added numbered README references with synopses and direct links for ModdingWiki, VGMPF, and John Miles' KE5FX site.
- Confirmed `Reference/AIL2/DEMO.XMI` contains three `FORM XMID` sequences.
- Verified header multi-song conversion against a synthetic two-sequence XMI file.
- Verified header sequence discovery, sequence count, indexed conversion, and all-sequence conversion against `Reference/AIL2/DEMO.XMI`.
- Verified CLI `--list`, default sequence-0 conversion, `--sequence`, and `--all` against `Reference/AIL2/DEMO.XMI`.
- Verified CLI `--list`, `--sequence 1`, and `--all` against a synthetic two-sequence XMI file.
- Verified default sequence-0 output matches explicit `--sequence 0` output for `Reference/AIL2/DEMO.XMI`.
- Verified `--all` sequence-0 output matches default sequence-0 output for `Reference/AIL2/DEMO.XMI`.
- Rebuilt successfully with `./build.sh`.
- Cleaned generated build outputs with `./build.sh clean`.

## 2026-04-24

### Source

- Refactored `xmi2mid.cpp` around a single direct conversion pass from XMI input bytes to MIDI output bytes.
- Removed the previous full-size `midiDecode` temporary buffer.
- Removed the previous full-size `midiWrite` temporary buffer.
- Replaced the old decode-then-rescale pipeline with immediate scaled delta-time emission.
- Kept the converter output as one contiguous `std::vector<uint8_t>` and patched the MIDI track length after writing the track data.
- Changed `xmiConverter` to accept `std::span<const uint8_t>` so callers can pass existing byte storage without copying.
- Added explicit bounds checks for XMI tags, chunks, event payloads, meta payloads, SysEx payloads, and variable-length integers.
- Added an explicit empty-input error instead of letting an empty file fall into pointer arithmetic.
- Replaced unaligned `reinterpret_cast` integer reads with byte-wise big-endian parsing.
- Replaced `_byteswap_ulong` and `_byteswap_ushort` dependencies with portable endian write helpers.
- Replaced floating-point delta scaling with integer arithmetic and rounded division.
- Preserved MIDI Format 0 output with a 960 tick timebase.
- Preserved XMI note-duration behavior by queuing pending note-off events from note-on durations.
- Changed pending note-off insertion from full resorting on each insert to ordered insertion with `std::upper_bound`.
- Kept pending note-off storage fixed-size and stack allocated.
- Added overflow and capacity checks for too many pending note-off events and oversized MIDI tracks.
- Added support for variable-length meta and SysEx lengths instead of assuming one-byte lengths.
- Preserved tempo meta-event handling and updates to the current quarter-note length.
- Preserved end-of-track flushing of pending note-off events.
- Consolidated channel event size handling into a small helper.
- Added `read_file` and `write_file` helpers with checked binary I/O.
- Kept the command-line interface as two explicit paths.

### Windows

- Added root `build.cmd` for Visual Studio 2022 builds.
- Made `build.cmd` support `build`, `clean`, and `rebuild` one-liners.
- Made `build.cmd` compile with MSVC C++23 preview mode.
- Made `build.cmd` auto-load the VS 2022 C++ environment when possible.
- Made `build.cmd` build intermediates in `%TEMP%` to avoid Samba/network-share write and link permission failures.
- Made `build.cmd` copy only the finished `xmi2mid.exe` back to the repository root.
- Made `build.cmd` remove read-only attributes from an existing `xmi2mid.exe` before replacing it.
- Added clearer `build.cmd` errors for missing source files, missing compilers, temporary directory failures, and denied final copies.
- Removed reliance on Visual Studio solution and project files for building.
- Removed remaining `.sln`, `.vcxproj`, `.vcxproj.filters`, and `.vcxproj.user` files from the active tree.

### Linux

- Added root `build.sh` for Linux builds.
- Made `build.sh` support `build`, `clean`, and `rebuild` one-liners.
- Made `build.sh` build with `c++`, `g++`, `clang++`, or the compiler named by `CXX`.
- Made `build.sh` build intermediates in a temporary directory and copy the finished Linux executable to `./xmi2mid`.
- Made `build.sh` compile with C++23, optimization, warnings, and `DNDEBUG` by default.
- Made `build.sh` respect extra `CXXFLAGS` and `LDFLAGS`.

### Mac

- Added root `build.command` for macOS builds.
- Made `build.command` support `build`, `clean`, and `rebuild` one-liners.
- Made `build.command` use Apple's default C++ compiler through `xcrun --find c++` when available.
- Made `build.command` fall back to the default `c++` command when `xcrun` is unavailable.
- Made `build.command` select the newest supported language mode from C++23, C++2b, and C++20.
- Made `build.command` build intermediates in a temporary directory and copy the finished macOS executable to `./xmi2mid`.

## 2025

The 2025 version was the first real architectural cleanup after the 2023 lift. Where 2023 mostly translated the 2015 C program into safer C++ without changing the shape of the tool, 2025 pulled the conversion logic into a reusable function, made the command line more explicit, and reduced the amount of noisy diagnostic behavior inherited from the older extractor-style program.

- Split the converter into a dedicated `xmiConverter` function instead of keeping the whole program inside `main`.
- Changed the converter boundary from "open a file and write a sibling `.mid`" to "accept XMI bytes and return MIDI bytes."
- Let `main` own the command-line contract, input file read, output file write, and user-facing error messages.
- Changed the CLI from the 2023 one-argument form to an explicit two-path form.
- Made the output path user-controlled instead of always deriving it from the input filename.
- Kept the core XMI event algorithm from 2023 intact: header skip, optional branch skip, event decode, pending note-off queue, timing rescale, tempo tracking, and MIDI track writing.
- Kept the two-buffer conversion model from 2023: one intermediate decoded event buffer and one final MIDI track buffer.
- Replaced the verbose 2023 extraction/debug output with quieter success and failure messages suitable for a standalone converter.
- Moved the note-off event representation closer to the converter by defining `NoteOffEvent` inside `xmiConverter`.
- Replaced the older global note-off storage with converter-local state, making repeated calls safer and easier to reason about.
- Replaced global mutable timing state with converter-local `timebase` and `qnlen` variables.
- Kept the fixed-size pending note-off storage, but made its ownership local to one conversion.
- Consolidated repeated bit and timing operations into local helper lambdas.
- Added helper lambdas for parsing note-off durations, reading SysEx lengths, reading MIDI variable-length values, and writing MIDI variable-length values.
- Replaced hand-written repeated variable-length write blocks with `write_varlen`.
- Replaced hand-written repeated variable-length read blocks with `read_varlen`.
- Preserved the 2023 `std::sort` ordering of pending note-off events after insertions and removals.
- Used `std::copy_n` for short event copies instead of manually copying each byte in every branch.
- Kept MIDI event handling readable by grouping branches by status nibble: note off, note on, key pressure, control change, program change, channel pressure, and pitch bend.
- Kept meta-event handling explicit, including tempo meta events and end-of-track handling.
- Kept SysEx pass-through support in the final MIDI write pass.
- Built the MIDI header in memory and patched the timebase and track length before returning the final byte vector.
- Returned an empty-output failure signal to `main` through the returned vector instead of relying only on console diagnostics.
- Added C++ standard library includes around the now-modular converter surface, especially `std::vector`, `std::array`, algorithms, file streams, and fixed-width integer types.
- Retained the MSVC-oriented byte-swap intrinsics and unaligned integer reads from the 2023 lineage; portability cleanup was left for the later 2026 pass.
- Retained the two large temporary allocations from the 2023 lineage; zero-copy and single-pass output were left for the later 2026 pass.
- Retained the original single-threaded event processing model because XMI event timing depends on strict stream order and shared pending note-off state.
- Updated the project presentation around the root `xmi2mid.cpp` converter instead of the older monolithic `main.cpp` shape.
- Updated the README to describe the 2025 refactor as the modern standalone converter generation.
- Preserved the original 1994/2015/2023 lineage while making the converter easier to reuse from other programs.

## 2023

The 2023 version was a conservative C-to-C++ lift of Kimio Ito's 2015 Visual Studio 2013 refactor. The goal was not to redesign the converter yet; it was to preserve the known-good XMIDI parsing and MIDI output behavior while making the code easier to own, build, and keep alive in a modern C++ codebase.

- Kept the 2015 converter's core algorithm intact: parse the XMI header, skip `TIMB`, optionally skip `RBRN`, read `EVNT`, decode XMI events, queue note-off events, rescale timing, and write a Standard MIDI file.
- Kept the two-stage conversion model from 2015: first expand XMI events into an intermediate decoded MIDI-like stream, then walk that stream again to apply timing conversion and emit final MIDI bytes.
- Kept the original timing constants and behavior: 120 BPM default tempo, 120 Hz XMI timing, 960 MIDI timebase, and 500000 microseconds per quarter note as the default quarter-note length.
- Kept the original XMI note-duration handling: `Note On` events include an embedded duration, so the converter schedules synthetic note-off events and inserts them into the MIDI stream at the correct delta time.
- Kept the original end-of-track behavior: pending note-off events are flushed before writing the final `FF 2F 00` MIDI end marker.
- Replaced C heap ownership with C++ containers: `malloc` buffers became `std::vector<unsigned char>`, and the fixed note-off array became `std::array`.
- Replaced C file I/O with C++ streams: `FILE*`, `fopen_s`, `fread_s`, and `fwrite` became `std::ifstream` and `std::ofstream`.
- Replaced `qsort` and a C comparator with `std::sort` and a typed C++ lambda.
- Replaced preprocessor timing macros with typed `constexpr` constants.
- Replaced the C `NOEVENTS` array declaration with a small C++ struct using `std::array<unsigned char, 3>` for the stored note-off bytes.
- Replaced Windows path helpers such as `_splitpath_s` and `_makepath_s` with `std::filesystem::path` to derive the `.mid` output path.
- Replaced C `printf`/`fprintf` diagnostics with `std::cout` and `std::cerr`, plus C++ formatting helpers such as `std::setw`.
- Preserved the 2015 one-argument tool shape: pass an input XMI file, and the converter writes a `.mid` next to it using the same base name.
- Preserved the 2015 binary assumptions, including little-endian structure reads for local fields and `_byteswap_ulong` for big-endian chunk lengths.
- Preserved the 2015 debug/info-oriented parsing output, including sequence count, patch/bank data, branch data, decoded length, and final track length.
- Improved readability by using named standard-library types and scoped variables instead of a larger pile of raw C pointers and manual lifetime management.
- Reduced failure risk around memory ownership by letting vectors and streams clean themselves up automatically when exiting early.
- Left deeper architectural work for later versions: the 2023 lift did not yet remove the intermediate buffers, did not yet make the converter portable away from MSVC byte-swap intrinsics, and did not yet split file I/O from conversion logic.

## 2015

- [2015 Refactor for Visual Studio 2013 by Kimio Ito](https://sourceforge.net/projects/midi-converter/files/xmi-to-midi/151216/)

## 1994

| Creator     | Released   | Platform |
| ----------- | ---------- | -------- |
| Markus Hein | 1994-02-04 | DOS      |

- [Original XMI2MID for DOS from VGMPF](http://www.vgmpf.com/Wiki/index.php?title=XMI_to_MIDI)
