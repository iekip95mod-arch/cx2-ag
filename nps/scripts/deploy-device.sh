#!/usr/bin/env bash
set -u

# Sends files to the physical calculator and proves each one arrived.
#
# Two things this exists to stop, both measured on 2026-09-05. A module sent without its SHA-256
# sidecar loads with every native surface off and the document says "StepCAS unavailable (integrity:
# missing)", which reads as a corrupt build rather than an incomplete deploy. And a transfer can
# report success and still leave a truncated file, so the only proof is reading it back.

fail() {
    printf 'deploy-device: %s\n' "$1" >&2
    exit 1
}

if [ "$#" -lt 1 ]; then
    printf 'usage: deploy-device.sh <file.tns> [<file.tns>...]\n' >&2
    printf '  NPS_DEVICE_DIR   directory on the calculator, default / (the documents root)\n' >&2
    printf '  NPS_NSPTOOL      path to nsptool, default the checkout build then the install\n' >&2
    exit 2
fi

script_dir=$(cd -P -- "${0%/*}" 2>/dev/null && pwd) || fail "cannot locate the script directory"
project_dir=$(cd -P -- "$script_dir/.." 2>/dev/null && pwd) || fail "cannot locate the project directory"
repo_dir=$(cd -P -- "$project_dir/.." 2>/dev/null && pwd) || fail "cannot locate the repository"

nsptool=${NPS_NSPTOOL:-}
if [ -z "$nsptool" ]; then
    for candidate in "$repo_dir/tools/nsptool/nsptool.new" "$repo_dir/tools/nsptool/nsptool" \
                     /usr/local/bin/nsptool; do
        if [ -x "$candidate" ]; then
            nsptool=$candidate
            break
        fi
    done
fi
[ -n "$nsptool" ] || fail "no nsptool found, set NPS_NSPTOOL"
[ -x "$nsptool" ] || fail "nsptool is not executable: $nsptool"

command -v cmp >/dev/null 2>&1 || fail "required utility is unavailable: cmp"

# The documents root always exists, so the default deploy creates no directory on somebody's device.
device_dir=${NPS_DEVICE_DIR:-/}
case "$device_dir" in
    /*) ;;
    *) fail "NPS_DEVICE_DIR must be an absolute calculator path: $device_dir" ;;
esac

readback_dir=$(mktemp -d) || fail "cannot create a scratch directory"
cleanup() { rm -rf "$readback_dir"; }
trap cleanup EXIT

# The module derives its sidecar from its own path the same way, so this has to agree with
# derive_integrity_sidecar_path in src/platform/nspire/integrity.cc.
sidecar_for() {
    case "$1" in
        *.tns) printf '%s.sha256.tns\n' "${1%.tns}" ;;
        *) printf '\n' ;;
    esac
}

send_and_verify() {
    local local_path=$1
    local name=${local_path##*/}
    local remote
    if [ "$device_dir" = "/" ]; then
        remote=/$name
    else
        remote=$device_dir/$name
    fi

    printf 'deploy-device: sending %s to %s\n' "$name" "$remote" >&2
    "$nsptool" put "$local_path" "$remote" >/dev/null ||
        fail "put failed: $name"

    "$nsptool" get "$remote" "$readback_dir/$name" >/dev/null ||
        fail "sent $name but could not read it back, so the transfer is unproven"

    cmp -s "$local_path" "$readback_dir/$name" ||
        fail "$name arrived different from what was sent, so the install is broken"

    printf 'deploy-device: verified %s\n' "$name" >&2
}

if [ "$device_dir" != "/" ]; then
    "$nsptool" mkdir "$device_dir" >/dev/null 2>&1 || true
fi

for source in "$@"; do
    [ -f "$source" ] || fail "no such file: $source"
    [ -r "$source" ] || fail "file is not readable: $source"

    sidecar=$(sidecar_for "$source")
    case "$source" in
        *.luax.tns)
            [ -n "$sidecar" ] && [ -f "$sidecar" ] ||
                fail "$source has no ${sidecar##*/} beside it. A module deployed without its sidecar loads with every native surface off, so this is refused rather than half done."
            ;;
    esac

    send_and_verify "$source"
    if [ -n "$sidecar" ] && [ -f "$sidecar" ]; then
        send_and_verify "$sidecar"
    fi
done

printf 'deploy-device: all files verified on the calculator\n' >&2
