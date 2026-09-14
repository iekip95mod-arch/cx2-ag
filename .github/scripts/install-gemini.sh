#!/usr/bin/env bash
set -euo pipefail
curl --fail --silent --show-error --location \
  https://storage.googleapis.com/antigravity-public/antigravity-cli/1.2.2-6061403484848128/linux-x64/cli_linux_x64.tar.gz \
  --output "$RUNNER_TEMP/antigravity.tar.gz"
printf '%s  %s\n' \
  74342cf2a78b344392e573b638a648a6ad1f8e877f494b96e20f9c2b79158d5c423c40b2dcf788703362bb0a9150f09c707fde599d7557ce01c12208802a63cb \
  "$RUNNER_TEMP/antigravity.tar.gz" | sha512sum --check
mkdir -p "$RUNNER_TEMP/antigravity-bin"
tar -xzf "$RUNNER_TEMP/antigravity.tar.gz" -C "$RUNNER_TEMP/antigravity-bin" antigravity
chmod +x "$RUNNER_TEMP/antigravity-bin/antigravity"
