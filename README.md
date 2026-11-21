**Table-of-Contents**
- [Usage](#usage)
- [Header Only Implementation](#header-only-implementation)
- [Build](#build)
- [XMI Specifications](#xmi-specifications)
  - [Multiple XMI Songs](#multiple-xmi-songs)
- [References](#references)

# Usage

List all XMI files from `Reference/AIL2/` and recurse subdirectories:
```sh
./xmi2mid -rl Reference/AIL2/
```

Convert sequence 0 from `Reference/AIL2/DEMO.XMI` to currant path:
```sh
./xmi2mid -s 0 Reference/AIL2/DEMO.XMI
```

Convert every sequence from files in directory `Reference/AIL2/` to separate files in `mid`:
```sh
./xmi2mid -s -1 Reference/AIL2 -d mid
```

Convert every sequence from files in directory `Reference/AIL2/` including subdirectories to separate files in `mid`:
```sh
./xmi2mid -rls -1 Reference/AIL2 -d mid
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

XMIDI is the preprocessed MIDI sequence format used by the IBM Audio Interface Library 2.x and later Miles Sound System lineage.
The primary source in this repository is John Miles' AIL2 release under

- [Reference/AIL2](Reference/AIL2)
- [XMIDI.TXT](Reference/AIL2/DOC/XMIDI.TXT)
- [TOOLS.TXT](Reference/AIL2/DOC/TOOLS.TXT)
- [API.TXT](Reference/AIL2/DOC/API.TXT)
- [MIDIFORM.C](Reference/AIL2/MIDIFORM.C)
- [XPLAY.C](Reference/AIL2/XPLAY.C)
- [XMIDI.ASM](Reference/AIL2/XMIDI.ASM)

External format summaries agree with the same overall structure [^1][^2].

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

1. [XMI Format][1] ModdingWiki. Synopsis: community-maintained technical notes on the XMI IFF layout, `XDIR`, `CAT XMID`, `TIMB`, `RBRN`, `EVNT`, note durations, and XMI delay encoding.
2. [XMI][2] Video Game Music Preservation Foundation Wiki. Synopsis: preservation-oriented overview of XMI history, game usage, players/converters, IFF tree structure, 120 Hz timing, and multi-subsong layout.
3. [KE5FX][3] Synopsis: John Miles' homepage and software archive, used here as the author/source reference point for the Audio Interface Library and Miles Sound System materials.

# Footnotes

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
