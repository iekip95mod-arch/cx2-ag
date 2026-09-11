#!/usr/bin/env bash
set -u

here=$(cd -P -- "${0%/*}" && pwd) || exit 2
project=$(cd -P -- "$here/../.." && pwd) || exit 2
report=$project/scripts/report-size.sh
elf=${1:-$project/build/device/nps_split.elf}
# The script under test reads NDL_SDK, so the test that judges its answer has to read the same
# one or it grades a checkout it is not looking at.
sdk=${NDL_SDK:-$project/../vendor/ndl-src/ndl-sdk}
arm_size=$sdk/toolchain/install/bin/arm-none-eabi-size
arm_readelf=$sdk/toolchain/install/bin/arm-none-eabi-readelf

failures=0
checks=0
check_equal() {
    checks=$((checks + 1))
    if [ "$1" != "$2" ]; then
        printf 'FAIL: %s\n  expected: %s\n  actual:   %s\n' "$3" "$2" "$1" >&2
        failures=$((failures + 1))
    fi
}

check_contains() {
    checks=$((checks + 1))
    case "$1" in
        *"$2"*) ;;
        *)
            printf 'FAIL: %s\n  missing: %s\n' "$3" "$2" >&2
            failures=$((failures + 1))
            ;;
    esac
}

metric() {
    printf '%s\n' "$1" | awk -F '\t' -v name="$2" '
        $1 == "metric" && $2 == name { print $3; found = 1; exit }
        END { if (!found) exit 1 }
    '
}

expect_failure() {
    expected_status=$1
    expected_text=$2
    label=$3
    shift 3
    output=$("$@" 2>&1)
    status=$?
    check_equal "$status" "$expected_status" "$label exit status"
    check_contains "$output" "$expected_text" "$label diagnostic"
}

[ -x "$report" ] || { printf 'FAIL: report is not executable: %s\n' "$report" >&2; exit 1; }
[ -f "$elf" ] || { printf 'FAIL: ARM ELF is missing: %s\n' "$elf" >&2; exit 1; }
[ -x "$arm_size" ] || { printf 'FAIL: ARM size tool is missing: %s\n' "$arm_size" >&2; exit 1; }
[ -x "$arm_readelf" ] || { printf 'FAIL: ARM readelf is missing: %s\n' "$arm_readelf" >&2; exit 1; }

elf_header=$(LC_ALL=C "$arm_readelf" -h "$elf" 2>&1)
check_contains "$elf_header" "Machine:                           ARM" "fixture is an ARM ELF"

work=$(mktemp -d) || exit 1
cleanup() {
    printf 'report-size test cleanup target: %s\n' "$work" >&2
    rm -rf -- "$work"
}
trap cleanup EXIT

machine_output=$("$report" "$elf" 2>"$work/human")
status=$?
human_output=$(awk '1' "$work/human")
check_equal "$status" "0" "real ARM ELF succeeds"
check_contains "$machine_output" $'schema\tnps-size-v1' "schema is versioned"
check_contains "$machine_output" $'tool\tarm-none-eabi-size' "configured ARM size tool is selected"
check_contains "$machine_output" $'section\t.text\t' "text section is reported"
check_contains "$machine_output" $'section\t.data\t' "data section is reported"
check_contains "$machine_output" $'section\t.bss\t' "bss section is reported"

expected_file_bytes=$(LC_ALL=C wc -c < "$elf" | awk '{print $1}')
check_equal "$(metric "$machine_output" elf_bytes)" "$expected_file_bytes" "ELF byte count is exact"

expected_totals=$(LC_ALL=C "$arm_size" -d "$elf" | awk '
    $1 ~ /^[0-9]+$/ && $2 ~ /^[0-9]+$/ && $3 ~ /^[0-9]+$/ && $4 ~ /^[0-9]+$/ {
        print $1, $2, $3, $4
        exit
    }
')
read -r expected_text expected_data expected_bss expected_memory <<< "$expected_totals"
check_equal "$(metric "$machine_output" text_bytes)" "$expected_text" "text total matches binutils"
check_equal "$(metric "$machine_output" data_bytes)" "$expected_data" "data total matches binutils"
check_equal "$(metric "$machine_output" bss_bytes)" "$expected_bss" "bss total matches binutils"
check_equal "$(metric "$machine_output" memory_bytes)" "$expected_memory" "memory total matches binutils"

reported_sections=$(printf '%s\n' "$machine_output" | awk -F '\t' '$1 == "section" { count++ } END { print count + 0 }')
check_equal "$(metric "$machine_output" section_count)" "$reported_sections" "section count matches records"
check_contains "$human_output" "file $expected_file_bytes B, memory $expected_memory B" "human summary has binary and memory sizes"
check_contains "$human_output" "via arm-none-eabi-size" "human summary names the selected tool"

second_output=$("$report" "$elf" 2>"$work/human-second")
check_equal "$second_output" "$machine_output" "machine output is stable across runs"

cp "$elf" "$work/ARM target.elf"
spaced_output=$("$report" "$work/ARM target.elf" 2>"$work/human-spaced")
check_equal "$(metric "$spaced_output" memory_bytes)" "$expected_memory" "paths containing spaces are handled"

printf 'not an ELF\n' > "$work/not-elf"
printf '\177ELF' > "$work/truncated-elf"
expect_failure 2 "usage: report-size.sh <ELF>" "missing argument" "$report"
expect_failure 1 "input is not a regular file" "missing input" "$report" "$work/missing.elf"
expect_failure 1 "input is not an ELF file" "non-ELF input" "$report" "$work/not-elf"
expect_failure 1 "available size tools could not read this ELF" "truncated ELF" "$report" "$work/truncated-elf"

mkdir -p "$work/isolated/scripts" "$work/no-size-bin"
cp "$report" "$work/isolated/scripts/report-size.sh"
cp "$elf" "$work/isolated/target.elf"
for utility in od awk wc; do
    utility_path=$(command -v "$utility")
    ln -s "$utility_path" "$work/no-size-bin/$utility"
done
bash_path=$(command -v bash)
output=$(PATH="$work/no-size-bin" NPS_SIZE_TOOL='' SIZE='' NDL_TOOLCHAIN='' NDL_SDK='' \
    "$bash_path" "$work/isolated/scripts/report-size.sh" "$work/isolated/target.elf" 2>&1)
status=$?
check_equal "$status" "1" "missing size tool exit status"
check_contains "$output" "no compatible size tool is available" "missing size tool diagnostic"

cleanup
trap - EXIT

if [ "$failures" -ne 0 ]; then
    printf 'report-size: %s checks, %s failed\n' "$checks" "$failures" >&2
    exit 1
fi
printf 'report-size: %s checks, 0 failed\n' "$checks"
