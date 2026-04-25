#!/usr/bin/env bash
set -euo pipefail

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
source_file="$root/xmi2mid.cpp"
output="$root/xmi2mid"

usage() {
    echo "Usage: ./build.command [clean|build|rebuild]"
}

clean() {
    rm -f "$output" "$root/xmi2mid.o"
    echo "Cleaned build outputs."
}

find_compiler() {
    if [[ -n "${CXX:-}" ]]; then
        command -v "$CXX"
        return
    fi

    if command -v xcrun >/dev/null 2>&1; then
        xcrun --find c++ 2>/dev/null && return
    fi

    command -v c++
}

choose_standard() {
    local compiler="$1"
    local test_dir
    test_dir="$(mktemp -d "${TMPDIR:-/tmp}/xmi2mid-std.XXXXXX")"
    local test_file="$test_dir/test.cpp"
    printf 'int main() { return 0; }\n' > "$test_file"

    for standard in -std=c++23 -std=c++2b -std=c++20; do
        if "$compiler" "$standard" "$test_file" -o "$test_dir/test" >/dev/null 2>&1; then
            rm -rf "$test_dir"
            echo "$standard"
            return
        fi
    done

    rm -rf "$test_dir"
    return 1
}

build() {
    if [[ ! -f "$source_file" ]]; then
        echo "Missing source file: $source_file" >&2
        return 1
    fi

    local compiler
    if ! compiler="$(find_compiler)"; then
        echo "No default C++ compiler found. Install Apple's Command Line Tools with: xcode-select --install" >&2
        return 1
    fi

    local standard
    if ! standard="$(choose_standard "$compiler")"; then
        echo "The default compiler does not support a new enough C++ mode. Install current Xcode Command Line Tools." >&2
        return 1
    fi

    local build_dir
    build_dir="$(mktemp -d "${TMPDIR:-/tmp}/xmi2mid-build.XXXXXX")"
    trap 'rm -rf "$build_dir"' RETURN

    local temp_output="$build_dir/xmi2mid"
    local flags=(
        "$standard"
        -O3
        -DNDEBUG
        -Wall
        -Wextra
        -Wpedantic
    )

    "$compiler" "${flags[@]}" ${CXXFLAGS:-} "$source_file" -o "$temp_output" ${LDFLAGS:-}
    cp -f "$temp_output" "$output"
    chmod +x "$output"
    echo "Built $output using $compiler $standard"
}

case "${1:-build}" in
    clean)
        clean
        ;;
    build)
        build
        ;;
    rebuild)
        clean
        build
        ;;
    *)
        usage >&2
        exit 1
        ;;
esac
