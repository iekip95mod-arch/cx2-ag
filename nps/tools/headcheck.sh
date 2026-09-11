#!/usr/bin/env bash
# Builds a commit from what git actually holds, in a worktree of its own, and runs the suite there.
#
# The shared checkout cannot answer this. Every lane builds against a working tree carrying every
# other lane's uncommitted headers, so a commit that names a source whose declarations were never
# committed still compiles for everyone and fails for nobody. That exact break has landed twice:
# 9a9b1cc named an untracked source in CORE_SOURCES, and bf6c116 committed numeric_mode.cc while its
# three helpers stayed in the working tree. Both were green in the shared tree at the time.
#
# The vendor directories are untracked on purpose, so a worktree has none. They are passed in from
# the real checkout rather than copied, which is why -DGIAC_ROOT exists.
#
# The worktree is reused rather than recreated: one checkout --detach per run, nothing removed.
set -u

self=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/$(basename "${BASH_SOURCE[0]}") || exit 1
repo=$(cd "$(dirname "$self")/../.." && pwd) || exit 1
worktree="${NPS_HEADCHECK_TREE:-$repo/.headcheck}"
ref="${1:-HEAD}"

if [ ! -d "$repo/vendor/khi-src/src" ]; then
    echo "headcheck: no vendor/khi-src in the checkout, nothing to build against"
    exit 1
fi

# A checker that cannot fail reads as coverage and is worse than no checker, so before it is trusted
# it is pointed at a tree carrying the very defect it exists to find. bf6c116 is that defect in the
# real history, but it predates -DGIAC_ROOT and so dies at configure for an unrelated reason, which
# would have proven nothing. Instead the shape is rebuilt on top of whatever is being checked: a
# header emptied while every source that includes it stays, which is what committing a source
# without its declarations looks like from git's side.
if [ "$ref" = "--selftest" ]; then
    victim="nps/include/nps/core/canonical.h"
    if ! git -C "$repo" cat-file -e "HEAD:$victim" 2>/dev/null; then
        echo "headcheck selftest: $victim is not in HEAD, so the selftest cannot build its break"
        exit 2
    fi
    blank=$(printf '#pragma once\n' | git -C "$repo" hash-object -w --stdin) || exit 1
    # Its own index file, so the shared one is never touched: other lanes have work staged in it.
    index="$repo/.headcheck-selftest-index"
    GIT_INDEX_FILE="$index" git -C "$repo" read-tree HEAD || exit 1
    GIT_INDEX_FILE="$index" git -C "$repo" update-index --cacheinfo "100644,$blank,$victim" || exit 1
    snapshot=$(GIT_INDEX_FILE="$index" git -C "$repo" write-tree) || exit 1
    broken=$(echo "headcheck selftest" | git -C "$repo" commit-tree "$snapshot" -p HEAD) || exit 1
    # The reason is asserted, not just the exit code. A non-zero exit is the same weak proxy that let
    # the first version of this selftest pass while dying at configure over an unrelated missing
    # vendor path, catching nothing it was written to catch.
    saw=$("$self" "$broken" 2>&1)
    if ! printf '%s' "$saw" | grep -qF 'does not build from a clean checkout'; then
        # Which of the two it is decides who is at fault, and the first version of this message did
        # not say. "Reached compile and missed the break" is the checker's defect. "Never reached
        # compile" is the environment's, and a stale cache from another worktree produced exactly
        # that while reading as the former.
        if printf '%s' "$saw" | grep -qF 'does not configure from a clean checkout'; then
            echo "headcheck selftest: INCONCLUSIVE, the run never reached the compile step"
        else
            echo "headcheck selftest: FAILED, a tree missing $victim reached compile and was not caught"
        fi
        printf '%s\n' "$saw" | head -5
        exit 1
    fi
    if ! printf '%s' "$saw" | grep -qF 'error:'; then
        echo "headcheck selftest: FAILED, the break was reported without a compiler error behind it"
        exit 1
    fi
    echo "headcheck selftest: a tree missing $victim is caught, at the compile step, with its errors"
    exit 0
fi

commit=$(git -C "$repo" rev-parse --verify "$ref^{commit}" 2>/dev/null)
if [ -z "$commit" ]; then
    echo "headcheck: $ref does not name a commit"
    exit 2
fi

# Without this, two lanes checkout --detach the same worktree out from under each other and the loser
# silently gets a verdict for a commit it never asked about. Every lane was told to run this after
# every commit, so that collision is a matter of time.
#
# The lock holds the owner's pid and is never deleted. noclobber makes taking it atomic, and a waiter
# that finds a pid nobody is running takes it over. Releasing by removal would need this run to exit
# cleanly to free it, which is exactly what a killed run does not do.
lock="$worktree.lock"
held=0
for _ in $(seq 1 600); do
    if (set -o noclobber; echo "$$" >"$lock") 2>/dev/null; then
        held=1
        break
    fi
    owner=$(cat "$lock" 2>/dev/null)
    if [ -z "$owner" ] || ! kill -0 "$owner" 2>/dev/null; then
        echo "$$" >"$lock"
        held=1
        break
    fi
    sleep 1
done
if [ "$held" -eq 0 ]; then
    echo "headcheck: $lock is held by a live pid $(cat "$lock" 2>/dev/null), which has run for ten minutes"
    exit 1
fi

if [ -d "$worktree/.git" ] || [ -f "$worktree/.git" ]; then
    git -C "$worktree" checkout --detach --force "$commit" >/dev/null 2>&1 || exit 1
else
    git -C "$repo" worktree add --detach "$worktree" "$commit" >/dev/null 2>&1 || exit 1
fi

# Derived from the worktree rather than fixed, so NPS_HEADCHECK_TREE isolates the build too. When it
# did not, pointing the checker at a private worktree reused the shared one's CMake cache, and the
# refusal that followed read as the checker being broken.
build="$worktree-build"

cmake -S "$worktree/nps" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Debug \
      -DGIAC_ROOT="$repo/vendor/khi-src" -DNDL_SDK="$repo/vendor/ndl-src/ndl-sdk" >/dev/null 2>&1
if [ "$?" -ne 0 ]; then
    echo "headcheck: $(git -C "$repo" rev-parse --short "$commit") does not configure from a clean checkout"
    cmake -S "$worktree/nps" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Debug \
          -DGIAC_ROOT="$repo/vendor/khi-src" -DNDL_SDK="$repo/vendor/ndl-src/ndl-sdk" 2>&1 | tail -20
    exit 1
fi

if ! cmake --build "$build" >"$build/headcheck-build.log" 2>&1; then
    echo "headcheck: $(git -C "$repo" rev-parse --short "$commit") does not build from a clean checkout"
    grep -F 'error:' "$build/headcheck-build.log" | head -20
    exit 1
fi

if ! ctest --test-dir "$build" --output-on-failure >"$build/headcheck-test.log" 2>&1; then
    echo "headcheck: $(git -C "$repo" rev-parse --short "$commit") builds but does not pass"
    tail -30 "$build/headcheck-test.log"
    exit 1
fi

# From the worktree's nps directory, the way ctest runs it: the golden fixture paths are relative and
# resolve against nothing anywhere else.
checks=$(cd "$worktree/nps" && "$build/nps_host" 2>&1 | grep -F 'nps: ' | tail -1)
passed=$(grep -cE '^ *[0-9]+/[0-9]+ Test .* Passed' "$build/headcheck-test.log")
echo "headcheck: $(git -C "$repo" rev-parse --short "$commit") is clean, $passed ctest cases, $checks"
