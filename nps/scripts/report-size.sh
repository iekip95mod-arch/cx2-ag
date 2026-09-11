#!/usr/bin/env bash
set -u

fail() {
    printf 'report-size: %s\n' "$1" >&2
    exit 1
}

if [ "$#" -ne 1 ]; then
    printf 'usage: report-size.sh <ELF>\n' >&2
    exit 2
fi

input=$1
case "$input" in
    /*) elf=$input ;;
    */*)
        input_dir=${input%/*}
        input_name=${input##*/}
        absolute_dir=$(cd -P -- "$input_dir" 2>/dev/null && pwd) ||
            fail "input directory does not exist: $input_dir"
        elf=$absolute_dir/$input_name
        ;;
    *) elf=$PWD/$input ;;
esac

[ -f "$elf" ] || fail "input is not a regular file: $input"
[ -r "$elf" ] || fail "input is not readable: $input"

for utility in od awk wc; do
    command -v "$utility" >/dev/null 2>&1 || fail "required utility is unavailable: $utility"
done

magic=$(LC_ALL=C od -An -tx1 -N4 "$elf" 2>/dev/null | awk '{$1=$1; print}')
[ "$magic" = "7f 45 4c 46" ] || fail "input is not an ELF file: $input"

script_dir=$(cd -P -- "${0%/*}" 2>/dev/null && pwd) || fail "cannot locate the script directory"
project_dir=$(cd -P -- "$script_dir/.." 2>/dev/null && pwd) || fail "cannot locate the project directory"

declare -a candidates=()
add_candidate() {
    [ -n "$1" ] && candidates[${#candidates[@]}]=$1
}

add_candidate "${NPS_SIZE_TOOL:-}"
add_candidate "${SIZE:-}"

cache=${elf%/*}/CMakeCache.txt
if [ -r "$cache" ]; then
    configured_ar=$(awk -F= '$1 == "CMAKE_AR:FILEPATH" { print substr($0, index($0, "=") + 1); exit }' "$cache")
    case "${configured_ar##*/}" in
        *-ar) add_candidate "${configured_ar%-ar}-size" ;;
        ar) add_candidate "${configured_ar%/*}/size" ;;
    esac
fi

[ -z "${NDL_TOOLCHAIN:-}" ] || add_candidate "$NDL_TOOLCHAIN/bin/arm-none-eabi-size"
[ -z "${NDL_SDK:-}" ] || add_candidate "$NDL_SDK/toolchain/install/bin/arm-none-eabi-size"
add_candidate "$project_dir/../vendor/ndl-src/ndl-sdk/toolchain/install/bin/arm-none-eabi-size"
add_candidate arm-none-eabi-size
add_candidate llvm-size
add_candidate size

resolve_candidate() {
    case "$1" in
        */*) [ -x "$1" ] && printf '%s\n' "$1" ;;
        *) command -v "$1" 2>/dev/null || true ;;
    esac
}

size_tool=
section_rows=
totals=
available_tools=0
for requested_tool in "${candidates[@]}"; do
    candidate=$(resolve_candidate "$requested_tool")
    [ -n "$candidate" ] || continue
    available_tools=$((available_tools + 1))

    section_report=$(LC_ALL=C "$candidate" -A -d "$elf" 2>/dev/null) || continue
    parsed_sections=$(printf '%s\n' "$section_report" | awk '
        NF >= 3 && $2 ~ /^[0-9]+$/ {
            print "section\t" $1 "\t" $2
            count++
        }
        END { if (count == 0) exit 1 }
    ') || continue

    totals_report=$(LC_ALL=C "$candidate" -d "$elf" 2>/dev/null) || continue
    parsed_totals=$(printf '%s\n' "$totals_report" | awk '
        $1 ~ /^[0-9]+$/ && $2 ~ /^[0-9]+$/ && $3 ~ /^[0-9]+$/ && $4 ~ /^[0-9]+$/ {
            print $1, $2, $3, $4
            exit
        }
    ')
    [ -n "$parsed_totals" ] || continue

    size_tool=$candidate
    section_rows=$parsed_sections
    totals=$parsed_totals
    break
done

if [ -z "$size_tool" ]; then
    [ "$available_tools" -ne 0 ] ||
        fail "no compatible size tool is available (need arm-none-eabi-size, llvm-size, or size)"
    fail "available size tools could not read this ELF"
fi

read -r text_bytes data_bytes bss_bytes memory_bytes <<< "$totals"
elf_bytes=$(LC_ALL=C wc -c < "$elf" | awk '{print $1}')
section_count=$(printf '%s\n' "$section_rows" | awk 'END { print NR }')
tool_name=${size_tool##*/}
elf_name=${elf##*/}

printf 'schema\tnps-size-v1\n'
printf 'tool\t%s\n' "$tool_name"
printf 'metric\telf_bytes\t%s\n' "$elf_bytes"
printf 'metric\ttext_bytes\t%s\n' "$text_bytes"
printf 'metric\tdata_bytes\t%s\n' "$data_bytes"
printf 'metric\tbss_bytes\t%s\n' "$bss_bytes"
printf 'metric\tmemory_bytes\t%s\n' "$memory_bytes"
printf 'metric\tsection_count\t%s\n' "$section_count"
printf '%s\n' "$section_rows"

printf 'report-size: %s: file %s B, memory %s B (text %s, data %s, bss %s), %s sections via %s\n' \
    "$elf_name" "$elf_bytes" "$memory_bytes" "$text_bytes" "$data_bytes" "$bss_bytes" \
    "$section_count" "$tool_name" >&2
