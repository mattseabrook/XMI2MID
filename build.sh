#!/usr/bin/env bash
set -euo pipefail

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
source_file="$root/xmi2mid.cpp"
output_dir="${XMI2MID_OUTPUT_DIR:-$root}"

die() {
    printf '%s\n' "$*" >&2
    exit 1
}

require_tool() {
    command -v "$1" >/dev/null 2>&1 || die "Missing $1. $2"
}

usage() {
    cat <<'HELP'
Usage: ./build.sh [linux|windows|macos|all|clean|rebuild]

  linux    Build xmi2mid for Linux
  windows  Build xmi2mid.exe for Windows x64
  macos    Build xmi2mid-macos for macOS
  all      Build all three targets in parallel
  clean    Remove generated executables
  rebuild  Clean and build for this host

With no argument, or with build, build for this host.
Set XMI2MID_OUTPUT_DIR to change the executable output directory.
See README.md for toolchain and SDK setup.
HELP
}

host="$(uname -s)"
case "$host" in
    Linux) native_target=linux ;;
    Darwin) native_target=macos ;;
    MINGW*|MSYS*|CYGWIN*) native_target=windows ;;
    *) die "Unsupported host operating system: $host" ;;
esac

clean() {
    rm -f -- "$output_dir/xmi2mid" "$output_dir/xmi2mid.exe" "$output_dir/xmi2mid-macos" "$output_dir/xmi2mid.o"
}

publish() {
    local name=$1
    [[ ! -d "$output_dir/$name" ]] || die "Cannot replace directory $output_dir/$name."
    mv -f -- "$build_dir/$name" "$output_dir/$name"
    printf 'Built %s\n' "$output_dir/$name"
}

build_linux() {
    local compiler="${CXX_LINUX:-${CXX:-}}"
    local flags=(-std=c++20 -O3 -DNDEBUG -Wall -Wextra -Wpedantic)
    [[ "$native_target" == linux || -n "${CXX_LINUX:-}" ]] ||
        die 'Cross-Linux builds require CXX_LINUX and a Linux toolchain/sysroot.'
    if [[ -z "$compiler" ]]; then
        compiler="$(command -v c++ || command -v g++ || command -v clang++)" ||
            die 'Install a C++20 compiler or set CXX_LINUX.'
    fi
    require_tool "$compiler" 'Install a C++20 compiler and Linux development headers.'
    if [[ -n "${LINUX_SYSROOT:-}" ]]; then flags+=("--sysroot=$LINUX_SYSROOT"); fi

    "$compiler" "${flags[@]}" ${extra_cxxflags[@]+"${extra_cxxflags[@]}"} \
        "$source_file" -o "$build_dir/xmi2mid" ${extra_ldflags[@]+"${extra_ldflags[@]}"}
    publish xmi2mid
}

windows_path() {
    if [[ "$native_target" == windows ]]; then cygpath -m "$1"; else printf '%s\n' "$1"; fi
}

find_windows_sdk() {
    local cache="${XDG_CACHE_HOME:-$HOME/.cache}"
    local base="${WINSDK_BASE:-}" candidate
    if [[ -z "$base" ]]; then
        for candidate in /opt/winsdk "${V64TNG_DEPS_CACHE:-$cache/v64tng}/winsdk" "$cache/xmi2mid/winsdk"; do
            if [[ -d "$candidate/crt/include" ]]; then
                base="$candidate"
                break
            fi
        done
    fi
    [[ -d "$base/crt/include" ]] ||
        die 'Set WINSDK_BASE to a Microsoft SDK/CRT installed with xwin, or use an x64 Visual Studio developer shell on Windows.'
    base="$(cd -- "$base" && pwd)"

    local cpp_include="${MSVC_INCLUDE:-$base/crt/include}"
    if [[ -z "${MSVC_INCLUDE:-}" && "$base" == /opt/winsdk && -f /opt/msvc/include/vector ]]; then
        cpp_include=/opt/msvc/include
    fi
    [[ -f "$cpp_include/vector" ]] || die 'Cannot find the Microsoft C++ standard library. Set MSVC_INCLUDE.'
    cpp_include="$(cd -- "$cpp_include" && pwd)"

    local include lib arch crt="$base/crt"
    for include in "$base/sdk/include" "$base/sdk/include"/10.*; do
        [[ -d "$include/ucrt" && -d "$include/um" && -d "$include/shared" ]] || continue
        for lib in "$base/sdk/lib" "$base/sdk/lib/${include##*/}"; do
            for arch in x86_64 x64; do
                if [[ -f "$lib/um/$arch/kernel32.lib" && -f "$lib/ucrt/$arch/libucrt.lib" &&
                      -f "$crt/lib/$arch/libcpmt.lib" && -f "$crt/lib/$arch/libcmt.lib" ]]; then
                    windows_includes=(
                        "-imsvc$(windows_path "$cpp_include")" "-imsvc$(windows_path "$crt/include")"
                        "-imsvc$(windows_path "$include/ucrt")" "-imsvc$(windows_path "$include/um")"
                        "-imsvc$(windows_path "$include/shared")"
                    )
                    windows_libpaths=(
                        "/libpath:$(windows_path "$crt/lib/$arch")"
                        "/libpath:$(windows_path "$lib/um/$arch")"
                        "/libpath:$(windows_path "$lib/ucrt/$arch")"
                    )
                    return
                fi
            done
        done
    done
    die "Cannot find the x64 Microsoft SDK and CRT under $base."
}

build_windows() {
    require_tool clang-cl 'Install LLVM Clang.'
    require_tool lld-link 'Install the LLVM linker.'
    local windows_includes=() windows_libpaths=()
    if [[ "$native_target" == windows ]]; then
        require_tool cygpath 'Use Git Bash, MSYS2 or Cygwin.'
        export MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL='*'
    fi
    if [[ "$native_target" != windows || -n "${WINSDK_BASE:-}" ]]; then
        find_windows_sdk
    fi

    (
        cd -- "$build_dir"
        local target=--target=x86_64-pc-windows-msvc
        clang-cl "$target" /c /std:c++20 /EHsc /MT /O2 /DNDEBUG /W4 /utf-8 \
            ${windows_includes[@]+"${windows_includes[@]}"} ${extra_cxxflags[@]+"${extra_cxxflags[@]}"} \
            -o xmi2mid.obj -- "$(windows_path "$source_file")"
        clang-cl "$target" -fuse-ld=lld-link xmi2mid.obj -o xmi2mid.exe /link \
            /machine:x64 /subsystem:console /incremental:no /opt:ref /opt:icf /Brepro \
            /defaultlib:libcmt /defaultlib:libcpmt /defaultlib:libucrt \
            /nodefaultlib:msvcrt.lib /nodefaultlib:ucrt.lib \
            ${windows_libpaths[@]+"${windows_libpaths[@]}"} ${extra_ldflags[@]+"${extra_ldflags[@]}"} kernel32.lib
    )
    publish xmi2mid.exe
}

build_macos() {
    local compiler="${CXX_MACOS:-}" sdk="${SDKROOT:-}" arch="${MACOS_ARCH:-}"
    local minimum="${MACOSX_DEPLOYMENT_TARGET:-}"
    local flags=(-std=c++20 -stdlib=libc++ -O3 -DNDEBUG -Wall -Wextra -Wpedantic)
    if [[ -n "$arch" && "$arch" != x86_64 && "$arch" != arm64 ]]; then
        die 'MACOS_ARCH must be x86_64 or arm64.'
    fi
    if [[ "$native_target" == macos ]]; then
        require_tool xcrun 'Install Xcode Command Line Tools with xcode-select --install.'
        compiler="${compiler:-${CXX:-$(xcrun --find clang++)}}"
        sdk="${sdk:-$(xcrun --sdk macosx --show-sdk-path)}"
        if [[ -n "$arch" ]]; then flags+=(-arch "$arch"); fi
    elif [[ -z "$compiler" ]]; then
        arch="${arch:-x86_64}"
        case "$arch" in
            x86_64) compiler=o64-clang++ ;;
            arm64) compiler=oa64-clang++ ;;
        esac
        if ! command -v "$compiler" >/dev/null 2>&1; then
            [[ -n "$sdk" ]] || die 'Cross-macOS builds require osxcross, CXX_MACOS, or SDKROOT with Clang and ld64.lld.'
            require_tool ld64.lld 'Install the LLVM Mach-O linker.'
            compiler="${CXX:-clang++}"
            flags+=("--target=$arch-apple-macos${minimum:-14.0}" -fuse-ld=lld)
        fi
    fi
    require_tool "$compiler" 'Install a macOS C++20 Clang toolchain.'
    if [[ -n "$sdk" ]]; then flags+=(-isysroot "$sdk"); fi
    if [[ -n "$minimum" ]]; then flags+=("-mmacosx-version-min=$minimum"); fi

    "$compiler" "${flags[@]}" ${extra_cxxflags[@]+"${extra_cxxflags[@]}"} \
        "$source_file" -o "$build_dir/xmi2mid-macos" ${extra_ldflags[@]+"${extra_ldflags[@]}"}
    publish xmi2mid-macos
}

(( $# <= 1 )) || die 'Pass one target or use --help.'
target="${1:-$native_target}"
case "$target" in
    -h|--help|help) usage; exit 0 ;;
    clean) clean; exit 0 ;;
    rebuild) clean; target="$native_target" ;;
    build) target="$native_target" ;;
    linux|windows|macos|all) ;;
    *) usage >&2; die "Unknown target: $target" ;;
esac

[[ -f "$source_file" ]] || die "Missing source file: $source_file"
read -r -a extra_cxxflags <<< "${CXXFLAGS:-}"
read -r -a extra_ldflags <<< "${LDFLAGS:-}"
mkdir -p -- "$output_dir"
output_dir="$(cd -- "$output_dir" && pwd)"
build_dir="$(mktemp -d "$output_dir/.xmi2mid-build.XXXXXX")"
trap 'rm -rf -- "$build_dir"' EXIT

case "$target" in
    linux) build_linux ;;
    windows) build_windows ;;
    macos) build_macos ;;
    all)
        build_linux & linux_pid=$!
        build_windows & windows_pid=$!
        build_macos & macos_pid=$!
        result=0
        for pid in "$linux_pid" "$windows_pid" "$macos_pid"; do
            wait "$pid" || result=1
        done
        exit "$result"
        ;;
esac
