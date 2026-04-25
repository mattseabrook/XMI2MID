#!/usr/bin/env bash
set -euo pipefail

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
source_file="$root/xmi2mid.cpp"
output="$root/xmi2mid"

usage() {
    echo "Usage: ./build.sh [clean|build|rebuild]"
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

    for compiler in c++ g++ clang++; do
        if command -v "$compiler" >/dev/null 2>&1; then
            command -v "$compiler"
            return
        fi
    done

    return 1
}

build() {
    if [[ ! -f "$source_file" ]]; then
        echo "Missing source file: $source_file" >&2
        return 1
    fi

    local compiler
    if ! compiler="$(find_compiler)"; then
        echo "No C++ compiler found. Install g++ or clang++, or set CXX." >&2
        return 1
    fi

    local build_dir
    build_dir="$(mktemp -d "${TMPDIR:-/tmp}/xmi2mid-build.XXXXXX")"
    trap 'rm -rf "$build_dir"' RETURN

    local temp_output="$build_dir/xmi2mid"
    local flags=(
        -std=c++23
        -O3
        -DNDEBUG
        -Wall
        -Wextra
        -Wpedantic
    )

    "$compiler" "${flags[@]}" ${CXXFLAGS:-} "$source_file" -o "$temp_output" ${LDFLAGS:-}
    cp -f "$temp_output" "$output"
    chmod +x "$output"
    echo "Built $output"
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
