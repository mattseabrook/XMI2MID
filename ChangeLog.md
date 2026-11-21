**Table-of-Contents**
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

- [2015 Refactor for Visual Studio 2013 by Kimio Ito][4]

## 1994

| Creator     | Released   | Platform |
| ----------- | ---------- | -------- |
| Markus Hein | 1994-02-04 | DOS      |

- [Original XMI2MID for DOS from VGMPF][5]

[^1]: [XMI Formart][1]
[^2]: [XMI][2]
[^3]: [John Miles, KE5FX][3]
[^4]: [2015 Refactor for Visual Studio 2013 by Kimio Ito][4]
[^5]: [Original XMI2MID for DOS from VGMPF][5]

[1]: <https://moddingwiki.shikadi.net/wiki/XMI_Format> "XMI Format"
[2]: <https://vgmpf.com/Wiki/index.php?title=XMI> "XMI"
[3]: <http://www.ke5fx.com> "John Miles, KE5FX"
[4]: <https://sourceforge.net/projects/midi-converter/files/xmi-to-midi/151216/> "2015 Refactor for Visual Studio 2013 by Kimio Ito"
[5]: <http://www.vgmpf.com/Wiki/index.php?title=XMI_to_MIDI> "Original XMI2MID for DOS from VGMPF"
