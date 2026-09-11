#!/usr/bin/env bash
set -u

script_dir=$(cd -P -- "${0%/*}" 2>/dev/null && pwd) || exit 2
project_dir=$(cd -P -- "$script_dir/../.." 2>/dev/null && pwd) || exit 2
host_script=$project_dir/scripts/bootstrap-host.sh
ndl_script=$project_dir/scripts/bootstrap-ndl.sh
missing_sdk=$script_dir/no-such-ndl-sdk

passed=0
failed=0

pass() {
    passed=$((passed + 1))
    printf 'PASS: %s\n' "$1"
}

fail() {
    failed=$((failed + 1))
    printf 'FAIL: %s\n' "$1" >&2
    [ -z "${2:-}" ] || printf '%s\n' "$2" >&2
}

if host_output=$("$host_script" 2>&1); then
    case "$host_output" in
        *'bootstrap-host: ready'*) pass 'host checkout readiness' ;;
        *) fail 'host checkout readiness omitted its ready record' "$host_output" ;;
    esac
else
    fail 'host checkout readiness' "$host_output"
fi

if ndl_output=$("$ndl_script" 2>&1); then
    case "$ndl_output" in
        *'bootstrap-ndl: ready'*) pass 'ndl checkout readiness' ;;
        *) fail 'ndl checkout readiness omitted its ready record' "$ndl_output" ;;
    esac
else
    fail 'ndl checkout readiness' "$ndl_output"
fi

if generator_output=$(php "${NDL_SDK:-$project_dir/../vendor/ndl-src/ndl-sdk}/libsyscalls/test_generator.php" 2>&1); then
    pass 'ndl syscall generator contracts and checked-in outputs'
else
    fail 'ndl syscall generator contracts and checked-in outputs' "$generator_output"
fi

if open_output=$(python3 "${NDL_SDK:-$project_dir/../vendor/ndl-src/ndl-sdk}/libsyscalls/test_open.py" 2>&1); then
    pass 'ndl file creation, access and preservation contracts'
else
    fail 'ndl file creation, access and preservation contracts' "$open_output"
fi

sdk=$(cd -P -- "${NDL_SDK:-$project_dir/../vendor/ndl-src/ndl-sdk}" && pwd) || exit 2
if naming_output=$(python3 "$sdk/../tests/test_naming.py" 2>&1); then
    pass 'ndl runtime, SDK paths and compatibility contracts'
else
    fail 'ndl runtime, SDK paths and compatibility contracts' "$naming_output"
fi

if abi_names_output=$(python3 "$sdk/../tests/test_zehn_names.py" 2>&1); then
    pass 'ndl source ABI, package flags and compiler query contracts'
else
    fail 'ndl source ABI, package flags and compiler query contracts' "$abi_names_output"
fi

if persistency_output=$(python3 "$sdk/../tests/test_persistency.py" 2>&1); then
    pass 'ndl persistent boot publication and recovery contracts'
else
    fail 'ndl persistent boot publication and recovery contracts' "$persistency_output"
fi

fixture=$(mktemp -d "${TMPDIR:-/tmp}/nps-bootstrap-sdk.XXXXXX") || exit 2
trap 'printf "Removing bootstrap fixture: %s\n" "$fixture"; rm -rf -- "$fixture"' EXIT
mkdir -p "$fixture/bin" "$fixture/lib" "$fixture/toolchain" || exit 2
for entry in include libndls system tools; do
    ln -s "$sdk/$entry" "$fixture/$entry" || exit 2
done
# The tracked marker has to be a real file, because git refuses to add through a symlinked component.
cp "$sdk/toolchain/build_toolchain.sh" "$fixture/toolchain/build_toolchain.sh" || exit 2
ln -s "$sdk/toolchain/install" "$fixture/toolchain/install" || exit 2
for stamp in .built_binutils .built_gcc_step1 .built_newlib .built_gcc_step2; do
    ln -s "$sdk/toolchain/$stamp" "$fixture/toolchain/$stamp" || exit 2
done
git init -q "$fixture" || exit 2
# The enclosing repository needs a commit, or rev-parse fails for its own reasons and the untracked
# case below stops telling the two behaviors apart.
git -C "$fixture" -c user.email=bootstrap@test -c user.name=bootstrap commit -q --allow-empty -m base || exit 2
git -C "$fixture" add toolchain/build_toolchain.sh || exit 2
for executable in "$sdk"/bin/*; do
    if [ "${executable##*/}" = nspire-tools ]; then
        cp "$executable" "$fixture/bin/" || exit 2
    else
        ln -s "$executable" "$fixture/bin/" || exit 2
    fi
done
ln -s "$sdk/lib/libndls.a" "$fixture/lib/libndls.a" || exit 2
cp "$sdk/lib/libsyscalls.a" "$fixture/lib/libsyscalls.a" || exit 2

if fixture_output=$(NDL_SDK="$fixture" "$ndl_script" 2>&1); then
    pass 'isolated ndl fixture readiness'
else
    fail 'isolated ndl fixture readiness' "$fixture_output"
fi

git -C "$fixture" rm --cached -q toolchain/build_toolchain.sh || exit 2
if untracked_output=$(NDL_SDK="$fixture" "$ndl_script" 2>&1); then
    fail 'untracked ndl SDK was accepted' "$untracked_output"
else
    case "$untracked_output" in
        *"ndl SDK is not tracked by a Git repository"*) pass 'untracked ndl SDK refusal' ;;
        *) fail 'untracked ndl SDK refusal was not actionable' "$untracked_output" ;;
    esac
fi
git -C "$fixture" add toolchain/build_toolchain.sh || exit 2

for helper in __aeabi_i2d __aeabi_d2iz; do
    if ! printf 'extern void %s(void); void bootstrap_number_abi(void) { %s(); }\n' "$helper" "$helper" |
        "$sdk/toolchain/install/bin/arm-none-eabi-gcc" -x c -c -o "$fixture/$helper.o" -; then
        fail "could not build $helper archive fixture"
        continue
    fi
    cp "$sdk/lib/libsyscalls.a" "$fixture/lib/libsyscalls.a" || exit 2
    "$sdk/toolchain/install/bin/arm-none-eabi-ar" rcs "$fixture/lib/libsyscalls.a" "$fixture/$helper.o" || exit 2
    if abi_output=$(NDL_SDK="$fixture" "$ndl_script" 2>&1); then
        fail "ndl archive containing $helper was accepted" "$abi_output"
    else
        case "$abi_output" in
            *'ndl Lua number syscalls contain ABI-losing integer conversions'*) pass "$helper archive refusal" ;;
            *) fail "$helper archive refusal was not actionable" "$abi_output" ;;
        esac
    fi
done

if missing_tool_output=$(CXX=nps-bootstrap-no-such-cxx "$host_script" 2>&1); then
    fail 'missing host compiler was accepted' "$missing_tool_output"
else
    case "$missing_tool_output" in
        *'host C++ compiler is unavailable: nps-bootstrap-no-such-cxx'*) pass 'missing host compiler refusal' ;;
        *) fail 'missing host compiler refusal was not actionable' "$missing_tool_output" ;;
    esac
fi

if [ -e "$missing_sdk" ]; then
    fail 'missing-SDK test path unexpectedly exists' "$missing_sdk"
elif missing_sdk_output=$(NDL_SDK="$missing_sdk" "$ndl_script" 2>&1); then
    fail 'missing ndl SDK was accepted' "$missing_sdk_output"
else
    case "$missing_sdk_output" in
        *'ndl SDK directory does not exist:'*'Provide NDL_SDK or restore the vendored checkout.'*)
            pass 'missing ndl SDK refusal'
            ;;
        *) fail 'missing ndl SDK refusal was not actionable' "$missing_sdk_output" ;;
    esac
fi

printf 'bootstrap scripts: %s passed, %s failed\n' "$passed" "$failed"
[ "$failed" -eq 0 ]
