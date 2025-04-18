2023 Refactor of XMI2MID.EXE by Markus Hein / Kimio Ito

**Table-of-Contents**
- [Build](#build)
- [Usage](#usage)
- [Notes](#notes)
  - [2025](#2025)
  - [2023](#2023)
  - [2015](#2015)
    - [Links](#links)
  - [1994](#1994)
    - [Links](#links-1)

# Build

Use the Command Prompt for Visual Studio 2022 to run `build.cmd` included in this repository.

```cmd
build.cmd
```

# Usage

Supply the path to the XMI file as the first argument.

```cmd
xmi2mid.exe "C:\Path\To\XMI\File.xmi"
```

# Notes

## 2025

Even though it worked flawlessly, I was never happy with the [2023](#2023) version of `xmiConverter()`. With the help of `Grok3` and `ChatGPT 4.1 (Preview)` I was able to safely reduce the code:

- **🔧 Modern C++ Swagger**: Packed with `constexpr`, `auto`, and lambdas for code that’s crisp and clean. The old 2023 version leaned on verbose C++ with manual bit-twiddling—now it’s smooth like a freshly tuned synth. 🎹
- **🏗️ Modular Core**: The new `xmiConverter` function handles the heavy lifting, leaving `main` to deal with file I/O. Back in 2023, everything was jammed into one massive `main` function—talk about a mess! 🧹
- **🎵 Streamlined Event Handling**: Note-off events now rock a sharp `NoteOffEvent` struct, cutting the cruft from 2023’s clunky `NOEVENTS` array. Same killer MIDI output, less code. 🎼
- **⏱️ Delta Time Done Right**: Helper lambdas (`read_varlen`, `write_varlen`) make delta time math a breeze, unlike 2023’s wordy bit-shifting dance. Precision meets elegance! 🕺
- **📜 Tidy Constants**: `constexpr` arrays like `midiHeader` keep things organized and fast at compile time. The 2023 version had static arrays tweaked at runtime—not bad, but not this slick. ✨
- **💻 Readable Code FTW**: Modern syntax and clear logic mean you can actually follow what’s happening. The 2023 code was functional but felt like deciphering ancient runes. 📜
- **🧠 Bloat Begone**: Slashed unnecessary overhead for a lighter footprint. The 2023 version was a heavyweight with extra checks and allocations—now we’re nimble! 🏃
- **🔍 Bit-Level Brilliance**: Bit manipulations are abstracted into helper functions, making 2023’s explicit bit-twiddling look like a relic. Same accuracy, way clearer. ⚙️
- **📏 Compact and Mighty**: Less code, same flawless MIDI output. The 2023 version was a chunky beast—2025 is a ninja, small but deadly. 🥷
- **📂 Flexible Input Handling**: `xmiConverter` takes a `std::vector<uint8_t>`, letting the CLI handle file reads. The 2023 version did it all itself, but 2025’s ready for bigger stages. 🎤
- **📏 Compact and Mighty**: Less code, same flawless MIDI output.

## 2023

I needed an XMI converter for my [7th Guest Game Engine Re-Creation](https://github.com/mattseabrook/v64tng) project, and decided to isolate the converter for others to freely use!

## 2015

### Links

- [2015 Refactor for Visual Studio 2013 by Kimio Ito](https://sourceforge.net/projects/midi-converter/files/xmi-to-midi/151216/)

## 1994

| Creator     | Released   | Platform |
| ----------- | ---------- | -------- |
| Markus Hein | 1994-02-04 | DOS      |

### Links

- [Original XMI2MID for DOS from VGMPF](http://www.vgmpf.com/Wiki/index.php?title=XMI_to_MIDI)