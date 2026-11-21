**Table-of-Contents**
- [Usage](#usage)
- [Header Only Implementation](#header-only-implementation)
- [Build](#build)
- [XMI Specifications](#xmi-specifications)
  - [Multiple XMI Songs](#multiple-xmi-songs)
- [References](#references)
- [Changelog](ChangeLog.md)

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

Convert a whole folder of XMI files, including its subfolders.

```sh
./xmi2mid --all music/ midi/
```

XMI2MID creates the destination as needed and keeps the input folder layout.
For example, `music/level1/theme.xmi` produces `midi/level1/theme_00.mid` and one
additional MIDI file for each remaining sequence.

List sequences across files and directories. Add `-r` to include subdirectories.

```sh
./xmi2mid -rl Reference/AIL2/
./xmi2mid --list Reference/AIL2/*.XMI
```

Convert a specific sequence from each input, or export every sequence with `-s -1`.
The destination directory is created as needed and defaults to the current directory.

```sh
./xmi2mid -s 0 Reference/AIL2/DEMO.XMI Reference/AIL2/SPKRDEMO.XMI -d mid
```

For batch conversion, use `-s N` or `--sequence=N`. Omit the index after `-s` to
export every sequence. Batch output uses the input stem and a zero-padded
sequence number, such as `DEMO_00.mid`. If two inputs would write to the same
output path, the command reports an error.

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

Run `./build.sh` from the repository root to build for the host, or select a
target explicitly. On Windows, run the script in Git Bash, MSYS2, or Cygwin.

| Command | Output |
| --- | --- |
| `./build.sh linux` | `xmi2mid`, Linux ELF executable |
| `./build.sh windows` | `xmi2mid.exe`, Windows x64 PE executable |
| `./build.sh macos` | `xmi2mid-macos`, macOS Mach-O executable |
| `./build.sh all` | All three executables, built in parallel |
| `./build.sh clean` | Removes generated executables |
| `./build.sh rebuild` | Cleans and builds for the host |

Set `XMI2MID_OUTPUT_DIR` to write the executables outside the repository root.
Set `CXXFLAGS` and `LDFLAGS` to add compiler and linker flags for the selected
toolchain.

- **Linux** uses a C++23 compiler. Set `CXX_LINUX` or `CXX` to select a compiler.
  From another host, set `CXX_LINUX` to a configured Linux cross compiler and
  `LINUX_SYSROOT` if it needs a separate sysroot.
- **Windows** uses `clang-cl`, `lld-link`, and the Microsoft C++ SDK and static
  CRT. On Windows, use an x64 Visual Studio developer environment. For
  cross-builds, the script checks `WINSDK_BASE`, `/opt/winsdk`, the v64tng cache,
  and the XMI2MID cache. Use `xwin splat --output <directory>` to provision the
  SDK if needed, and set `WINSDK_BASE` to that directory. Set `MSVC_INCLUDE` if
  the Microsoft STL headers are installed separately.
- **macOS** uses Apple Clang from Xcode Command Line Tools on a Mac. To
  cross-build, use osxcross, set `CXX_MACOS` to a configured macOS cross compiler,
  or set `SDKROOT` to an Apple SDK with Clang, libc++, and `ld64.lld` available.
  With automatic toolchain selection, set `MACOS_ARCH` to `arm64` or `x86_64`.
  Native builds default to the host architecture, and automatic cross-builds
  default to `x86_64`. An explicit `CXX_MACOS` supplies its own target. Set
  `MACOSX_DEPLOYMENT_TARGET` to choose the minimum supported macOS version.

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
