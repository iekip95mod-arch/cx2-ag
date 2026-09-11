#!/usr/bin/env bash
# Guards against the resident-module teardown bug: a .luax entry point whose main returns instead of
# calling _exit. crt0.S runs __cpp_fini and newlib exit after main returns, which destroys every
# static object in an image that stays loaded and callable. luagiac.c had exactly this and reset the
# calculator on the first caseval. See nucleus.h lines 127 to 129 and resident-exit.query.
#
# The check is a clang-query AST matcher, not a compiled clang-tidy plugin, so it needs no LLVM
# libraries and does not break when the toolchain is upgraded. With no clang-query on PATH it skips,
# matching how the uitest target skips without a host lua.
set -u

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
query="$here/resident-exit.query"
control="$here/resident-exit-parsed.query"
# NDL_SDK is honoured the way the build honours it, so this runs from a worktree rather than only
# from a checkout with ndl-src beside it.
sdk_root=${NDL_SDK:-$root/vendor/ndl-src/ndl-sdk}
sdk="$sdk_root/include"
toolchain="$sdk_root/toolchain/install/include"

clang_query=$(command -v clang-query 2>/dev/null || true)
if [ -z "$clang_query" ]; then
    echo "resident-exit lint: skipped, no clang-query found"
    exit 0
fi

# One clang-query run answers both of the questions asked below, so its output is captured whole and
# read twice rather than the file being parsed again. Warnings are silenced, which leaves the
# diagnostics that matter and the fixed markers clang-query prints per hit.
run_query() {
    local file="$1"
    local commands="$2"
    shift 2
    "$clang_query" -f "$commands" "$file" -- "$@" 2>&1
}

# binds here is printed once per match. error covers clang's fatal errors too.
binds_here() { printf '%s\n' "$1" | grep -F 'binds here' || true; }
compile_errors() { printf '%s\n' "$1" | grep -F ' error: ' || true; }

# The offending main locations in one translation unit, one per line, empty when clean.
offenders() {
    local file="$1"; shift
    binds_here "$(run_query "$file" "$query" "$@")"
}

# Prove the matcher still works before trusting it on the real files. An untested guard reads as
# coverage and is worse than none.
selftest() {
    local bad good partial good_probe
    bad=$(offenders "$here/testdata/resident_bad.c" -std=gnu11 -w)
    good=$(offenders "$here/testdata/resident_good.c" -std=gnu11 -w)
    partial=$(offenders "$here/testdata/resident_partial_exit.c" -std=gnu11 -w)
    if [ -z "$bad" ]; then
        echo "resident-exit lint: SELFTEST FAILED, the bad fixture was not flagged"
        return 1
    fi
    # The good fixture is the one whose empty result could be a lie, so it is asked to prove it
    # parsed before its silence is read as innocence.
    good_probe=$(run_query "$here/testdata/resident_good.c" "$control" -std=gnu11 -w)
    if [ -z "$(binds_here "$good_probe")" ]; then
        echo "resident-exit lint: SELFTEST FAILED, the good fixture did not parse, so it was never judged"
        return 1
    fi
    # The other direction of the same gate, and the one that matters most: a file that does not
    # compile has to be refused rather than reported clean. That silence is what let this lint pass
    # lua_module.cc for its whole life, so the refusal is asserted rather than assumed.
    check_tu "$here/testdata/resident_unparseable.c" -std=gnu11 -w >/dev/null 2>&1
    if [ "$?" -ne 2 ]; then
        echo "resident-exit lint: SELFTEST FAILED, a file that could not compile was not refused"
        return 1
    fi
    if [ -n "$good" ]; then
        echo "resident-exit lint: SELFTEST FAILED, the good fixture was flagged"
        return 1
    fi
    if [ -z "$partial" ]; then
        echo "resident-exit lint: SELFTEST FAILED, the fixture that exits on one branch and returns on the other was not flagged"
        return 1
    fi
    return 0
}

# A missing file is skipped, so a checkout without khi-src still runs.
check_tu() {
    local file="$1"; shift
    [ -f "$file" ] || return 0
    # clang-query says "0 matches" and exits 0 whether a file is clean, broken or never parsed, so a
    # clean verdict is worth nothing until the file is shown to have compiled and to have parsed into
    # the entry point both matchers need.
    local probe errors
    probe=$(run_query "$file" "$control" "$@")
    errors=$(compile_errors "$probe")
    if [ -n "$errors" ]; then
        printf '%s\n' "$errors"
        echo "resident-exit lint: $file did not compile as the lint invokes it, so it was not judged"
        return 2
    fi
    if [ -z "$(binds_here "$probe")" ]; then
        echo "resident-exit lint: $file did not parse into a resident entry point, so nothing judged it"
        return 2
    fi
    local hits
    hits=$(offenders "$file" "$@")
    if [ -n "$hits" ]; then
        printf '%s\n' "$hits"
        return 1
    fi
    return 0
}

if [ "${1:-}" = "--selftest" ]; then
    selftest || exit 1
    echo "resident-exit lint: selftest passed"
    exit 0
fi

selftest || exit 1

fail=0
offender=0
# A file that could not be judged and a file that was judged and found guilty both stop the run, but
# they are different facts, so only the second gets the teardown explanation.
run_tu() {
    check_tu "$@"
    case $? in
        0) ;;
        1) fail=1; offender=1 ;;
        *) fail=1 ;;
    esac
}
# NPS_RELEASE_MANIFEST is defined by every build that compiles this file, and the integrity refusal
# surface names a function that only exists under it, so without it the file does not compile at all.
run_tu "$root/nps/src/platform/nspire/lua_module.cc" -std=c++20 -fno-exceptions -fno-rtti -w -DNPS_RELEASE_MANIFEST=1 -I "$root/nps/include" -I "$sdk" -I "$toolchain"
run_tu "$root/vendor/khi-src/src/luagiac.c" -std=gnu11 -w -I "$sdk"

if [ "$offender" -ne 0 ]; then
    echo ""
    echo "resident-exit lint: a resident Lua module main returns instead of calling _exit(0)."
    echo "Returning runs static destructors on an image that stays loaded, freeing its globals under"
    echo "later calls. End main with _exit(0) instead. See nucleus.h lines 127 to 129."
fi
if [ "$fail" -ne 0 ]; then
    exit 1
fi

echo "resident-exit lint: ok"
exit 0
