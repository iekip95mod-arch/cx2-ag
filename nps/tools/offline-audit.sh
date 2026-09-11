#!/usr/bin/env bash
# PLAT-008 and the negative half of PLAT-003. ndl syscalls compile to a swi with a literal number
# inside a named stub, and the device link uses --gc-sections, so a stub survives only if something
# can reach it. That makes "can this image talk to a host at all" a property of the linked ELF rather
# than a claim about one run. tools/offline_audit.cc does the reading. This wrapper exists because the
# audit is a host program and the device tree cross-compiles, so cmake there cannot build it.
#
# Usage: offline-audit.sh <workdir> <elf>...
set -u

if [ "$#" -lt 2 ]; then
    echo "offline audit: usage: offline-audit.sh <workdir> <elf>..."
    exit 2
fi

workdir="$1"
shift
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
# The build passes the paths it resolved, so -DNDL_SDK and -DGIAC_ROOT reach this step too. Left
# to the relative guess, a worktree build found no SDK and skipped the audit while the rest of the
# build compiled happily against the override, which is a check reading as coverage.
sdk="${NPS_NDL_SDK:-$root/vendor/ndl-src/ndl-sdk}"
source="$here/offline_audit.cc"
fixture="$here/testdata/offline_dirty.cc"
audit="$workdir/nps_offline_audit"
# The record carries the packaged artifact's digest, so the audit needs the same sha256 the runtime
# and the sidecar step use rather than one of its own.
sha256="${NPS_GIAC_SRC:-$root/vendor/khi-src/src}/sha256.c"

mkdir -p "$workdir" || exit 1

if [ ! -d "$sdk" ]; then
    echo "offline audit: skipped, no ndl sdk at $sdk"
    if [ -z "${NPS_NDL_SDK:-}" ]; then
        echo "offline audit: no path was passed, so that was a guess from $here"
    fi
    exit 0
fi

if [ ! -f "$sha256" ]; then
    echo "offline audit: no sha256 implementation at $sha256"
    exit 1
fi

if [ ! -x "$audit" ] || [ "$source" -nt "$audit" ] || [ "$sha256" -nt "$audit" ]; then
    if ! "${CXX:-c++}" -std=c++17 -O1 -I "$(dirname "$sha256")" -o "$audit" "$source" \
            -x c++ "$sha256"; then
        echo "offline audit: the audit tool did not build"
        exit 1
    fi
fi

# Rebuilt every run rather than cached, because the fixture is what proves the audit still rejects,
# and a stale one would prove it about a tool that no longer exists.
dirty="$workdir/offline_dirty.elf"
export PATH="$sdk/bin:$sdk/toolchain/install/bin:$PATH"
if ! nspire-g++ -c "$fixture" -o "$workdir/offline_dirty.o" -I "$sdk/include" 2>"$workdir/fixture.log"; then
    echo "offline audit: the fixture did not compile"
    cat "$workdir/fixture.log"
    exit 1
fi
if ! nspire-ld "$workdir/offline_dirty.o" -o "$dirty" -Wl,--gc-sections 2>>"$workdir/fixture.log"; then
    echo "offline audit: the fixture did not link"
    cat "$workdir/fixture.log"
    exit 1
fi

exec "$audit" --syscall-list "$sdk/include/syscall-list.h" --dirty "$dirty" "$@"
