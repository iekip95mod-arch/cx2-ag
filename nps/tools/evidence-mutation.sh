#!/usr/bin/env bash
# PRD section 19.9's last sentence: the report must show whether each accepted claim path has tests
# that fail when its required evidence is removed or corrupted. A golden fixture nothing actually
# compares would pass whatever the code did, and would read as coverage while proving nothing. So
# each fixture in turn loses one line and the suite has to notice.
#
# The line taken is the one carrying the claim: a verification record where there is one, and the
# outcome where there is not, which is what a refusal fixture has instead. Nothing in the checkout is
# touched: the fixtures are copied first and the suite is pointed at the copy by NPS_GOLDEN_DIR.
set -u

if [ "$#" -lt 2 ]; then
    echo "usage: evidence-mutation.sh <nps_host> <fixtures dir> [--selftest]"
    exit 2
fi

host="$1"
fixtures="$2"
mode="${3:-}"
here=$(cd "$(dirname "$0")" && pwd)
project=$(cd "$here/.." && pwd)

if [ ! -x "$host" ]; then
    echo "evidence mutation: skipped, no test binary at $host"
    exit 0
fi
if [ ! -d "$fixtures" ]; then
    echo "evidence mutation: no fixtures at $fixtures"
    exit 1
fi

work=$(mktemp -d) || exit 1
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/live" "$work/pristine"
cp "$fixtures"/*.txt "$work/live/" || exit 1
cp "$fixtures"/*.txt "$work/pristine/" || exit 1

cd "$project" || exit 1

suite_fails() {
    NPS_GOLDEN_DIR="$work/live" "$host" >/dev/null 2>&1
    [ "$?" -ne 0 ]
}

# The copy has to pass before a failure against it means anything.
if suite_fails; then
    echo "evidence mutation: the suite already fails against an untouched copy of the fixtures"
    exit 1
fi

# The line number of this fixture's claim: its first verification record, or its outcome when it
# records a refusal and has none.
claim_line() {
    local file="$1"
    local n
    n=$(grep -n -m 1 -F "verification:" "$file" | cut -d: -f1)
    if [ -z "$n" ]; then
        n=$(grep -n -m 1 -F "outcome:" "$file" | cut -d: -f1)
    fi
    printf '%s' "$n"
}

checked=0
missed=0

# Each named fixture loses its claim line, and the suite is expected to fail while it is missing.
mutate() {
    local file name line
    for file in "$@"; do
        name=$(basename "$file")
        line=$(claim_line "$file")
        if [ -z "$line" ]; then
            echo "evidence mutation: $name carries neither a verification nor an outcome"
            missed=$((missed + 1))
            continue
        fi
        sed "${line}d" "$file" > "$work/live/$name"
        checked=$((checked + 1))
        if ! suite_fails; then
            echo "evidence mutation: $name passed with line $line removed, so nothing compares it"
            missed=$((missed + 1))
        fi
        cp "$file" "$work/live/$name"
    done
}

# A fixture no check reads is exactly what this is looking for, so make one and require it to be
# caught before the real fixtures are trusted to it. Without this the check would report success on a
# suite that compares nothing at all.
selftest() {
    local orphan="$work/pristine/zz_orphan_selftest.txt"
    {
        echo "problem: a fixture no check_golden call names"
        echo "outcome: solved"
        echo "      verification: nothing reads this file"
    } > "$orphan"
    cp "$orphan" "$work/live/"
    # Quiet, because the orphan being caught is what should happen and its message reads like a
    # failure. A redirect on a function call is not a subshell, so the counts still come back.
    mutate "$orphan" >/dev/null
    rm -f "$orphan" "$work/live/zz_orphan_selftest.txt"
    if [ "$missed" -ne 1 ]; then
        echo "evidence mutation: SELFTEST FAILED, an uncompared fixture was not caught"
        return 1
    fi
    checked=0
    missed=0
    return 0
}

selftest || exit 1
if [ "$mode" = "--selftest" ]; then
    echo "evidence mutation: selftest passed, an uncompared fixture is caught"
    exit 0
fi

mutate "$work"/pristine/*.txt
echo "evidence mutation: $checked fixtures, $missed that survived losing their claim line"
[ "$missed" -eq 0 ]
