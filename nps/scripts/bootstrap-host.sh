#!/usr/bin/env bash
set -eu

fail() {
    printf 'bootstrap-host: %s\n' "$1" >&2
    exit 1
}

resolve_executable() {
    case "$1" in
        */*)
            [ -x "$1" ] || return 1
            printf '%s\n' "$1"
            ;;
        *) command -v "$1" 2>/dev/null ;;
    esac
}

version_at_least() {
    local actual=$1
    local required_major=$2
    local required_minor=$3
    local actual_major=${actual%%.*}
    local remainder=${actual#*.}
    local actual_minor=${remainder%%.*}

    case "$actual_major" in '' | *[!0-9]*) return 1 ;; esac
    case "$actual_minor" in '' | *[!0-9]*) return 1 ;; esac
    [ "$actual_major" -gt "$required_major" ] ||
        { [ "$actual_major" -eq "$required_major" ] && [ "$actual_minor" -ge "$required_minor" ]; }
}

script_dir=$(cd -P -- "${0%/*}" 2>/dev/null && pwd) || fail "cannot locate the script directory"
project_dir=$(cd -P -- "$script_dir/.." 2>/dev/null && pwd) || fail "cannot locate the project directory"
[ -r "$project_dir/CMakeLists.txt" ] || fail "CMakeLists.txt is missing from the project root"

# make is here because the host configure reaches for it: with LuaJIT installed it builds the luax
# test library, and hashing that library's ndl inputs runs make in the SDK's system directory.
for utility in cmake ctest make ninja python3; do
    command -v "$utility" >/dev/null 2>&1 ||
        fail "required tool is unavailable: $utility. Install it and put it on PATH."
done

cmake_output=$(cmake --version 2>&1) || fail "cmake could not report its version"
cmake_line=${cmake_output%%$'\n'*}
cmake_version=${cmake_line#cmake version }
[ "$cmake_version" != "$cmake_line" ] || fail "unrecognized cmake version output: $cmake_line"
version_at_least "$cmake_version" 3 20 || fail "CMake 3.20 or newer is required, found $cmake_version"

ctest --version >/dev/null 2>&1 || fail "ctest could not start"
ninja_version=$(ninja --version 2>&1) || fail "ninja could not report its version"
[ -n "$ninja_version" ] || fail "ninja returned an empty version"

python_version=$(python3 -c 'import sys; print(".".join(map(str, sys.version_info[:3])))' 2>/dev/null) ||
    fail "python3 could not report its version"
python3 -c 'import sys; raise SystemExit(0 if sys.version_info >= (3, 10) else 1)' ||
    fail "Python 3.10 or newer is required, found $python_version"

requested_cxx=${CXX:-c++}
host_cxx=$(resolve_executable "$requested_cxx") ||
    fail "host C++ compiler is unavailable: $requested_cxx. Set CXX to a C++20 compiler."
compiler_output=$("$host_cxx" --version 2>&1) || fail "host C++ compiler could not start: $requested_cxx"
compiler_line=${compiler_output%%$'\n'*}

cmake_help=$(cmake --help 2>&1) || fail "cmake could not list its generators"
case "$cmake_help" in
    *Ninja*) ;;
    *) fail "this CMake installation does not provide the Ninja generator" ;;
esac

if ! "$host_cxx" -std=c++20 -fsyntax-only -x c++ - >/dev/null 2>&1 <<'EOF'
#include <cstdint>
#include <span>
#include <string_view>

consteval std::uint32_t checked_value() { return 4U; }
constexpr std::string_view label = "nps";
static_assert(checked_value() == 4U && label.size() == 3U);

int main() {
    std::uint32_t values[] = {checked_value()};
    std::span<std::uint32_t> view(values);
    return view.empty() ? 1 : 0;
}
EOF
then
    fail "the compiler rejected the required C++20 profile: $requested_cxx -std=c++20"
fi

printf 'bootstrap-host: cmake %s\n' "$cmake_version"
printf 'bootstrap-host: ninja %s\n' "$ninja_version"
printf 'bootstrap-host: python %s\n' "$python_version"
printf 'bootstrap-host: compiler %s\n' "$compiler_line"
printf 'bootstrap-host: C++20 profile ready\n'
printf 'bootstrap-host: ready\n'
