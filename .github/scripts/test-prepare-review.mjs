import assert from 'node:assert/strict';
import test from 'node:test';
import { mkdirSync, mkdtempSync, writeFileSync, chmodSync, readFileSync } from 'node:fs';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';

test('review setup restores missing tools, reuses cached tools and refuses stale or failed setup', () => {
  const workspace = fileURLToPath(new URL('../../.Internal/workspaces/', import.meta.url));
  mkdirSync(workspace, { recursive: true });
  for (const scenario of ['download', 'cached', 'partial-cache', 'stale', 'build-failure']) {
    const root = mkdtempSync(join(workspace, 'review-setup-'));
    const bin = join(root, 'bin');
    const sdk = join(root, 'vendor/ndl-src/ndl-sdk');
    for (const path of [bin, join(sdk, 'toolchain'), join(root, 'vendor/deps'), join(root, 'vendor/toolchain')]) mkdirSync(path, { recursive: true });
    for (const path of ['vendor/ndl-src/ndl-sdk/toolchain/build_toolchain.sh', 'vendor/deps/build-gmp.sh', 'vendor/deps/build-mpfr-mpfi.sh']) writeFileSync(join(root, path), 'fixture');
    const digest = spawnSync('bash', ['-c', "shasum -a 256 vendor/ndl-src/ndl-sdk/toolchain/build_toolchain.sh vendor/deps/build-gmp.sh vendor/deps/build-mpfr-mpfi.sh | shasum -a 256 | cut -d' ' -f1"], { cwd: root, encoding: 'utf8' });
    assert.equal(digest.status, 0);
    writeFileSync(join(root, 'vendor/toolchain/built-from.sha256'), scenario === 'stale' ? 'stale\n' : digest.stdout);
    const stub = [
      '#!/bin/bash', 'set -eu', 'name=$(basename "$0")', 'echo "$name $*" >> "$SETUP_LOG"',
      'case "$name" in',
      'brew) if [ "$1" = --prefix ]; then echo "$SETUP_ROOT/brew/$2"; fi ;;',
      'sysctl) echo 2 ;;',
      'git|zstd) : ;;',
      'tar) mkdir -p "$SETUP_ROOT/vendor/ndl-src/ndl-sdk/toolchain/install/bin"; touch "$SETUP_ROOT/vendor/ndl-src/ndl-sdk/toolchain/install/bin/arm-none-eabi-gcc"; chmod +x "$SETUP_ROOT/vendor/ndl-src/ndl-sdk/toolchain/install/bin/arm-none-eabi-gcc" ;;',
      'make) if [ "$SETUP_SCENARIO" = build-failure ]; then exit 7; fi; mkdir -p "$SETUP_ROOT/vendor/ndl-src/ndl-sdk/bin" "$SETUP_ROOT/vendor/ndl-src/ndl-sdk/lib"; touch "$SETUP_ROOT/vendor/ndl-src/ndl-sdk/bin/genzehn" "$SETUP_ROOT/vendor/ndl-src/ndl-sdk/lib/libsyscalls.a"; chmod +x "$SETUP_ROOT/vendor/ndl-src/ndl-sdk/bin/genzehn" ;;',
      'esac', ''
    ].join('\n');
    for (const name of ['brew', 'git', 'zstd', 'tar', 'make', 'sysctl']) {
      writeFileSync(join(bin, name), stub); chmodSync(join(bin, name), 0o755);
    }
    if (scenario === 'cached') {
      mkdirSync(join(sdk, 'toolchain/install/bin'), { recursive: true });
      writeFileSync(join(sdk, 'toolchain/install/bin/arm-none-eabi-gcc'), '');
      chmodSync(join(sdk, 'toolchain/install/bin/arm-none-eabi-gcc'), 0o755);
      writeFileSync(join(sdk, 'toolchain/install/.review-ready'), '');
    }
    if (scenario === 'partial-cache') {
      mkdirSync(join(sdk, 'toolchain/install/bin'), { recursive: true });
      writeFileSync(join(sdk, 'toolchain/install/bin/arm-none-eabi-gcc'), '');
      chmodSync(join(sdk, 'toolchain/install/bin/arm-none-eabi-gcc'), 0o755);
    }
    const log = join(root, 'commands.log'), envFile = join(root, 'environment');
    const run = spawnSync('bash', [fileURLToPath(new URL('./prepare-review.sh', import.meta.url))], { cwd: root, encoding: 'utf8', env: { ...process.env, PATH: bin + ':' + process.env.PATH, SETUP_ROOT: root, SETUP_LOG: log, SETUP_SCENARIO: scenario, GITHUB_ENV: envFile } });
    const commands = readFileSync(log, 'utf8');
    if (scenario === 'stale' || scenario === 'build-failure') {
      assert.notEqual(run.status, 0);
      if (scenario === 'stale') assert.doesNotMatch(commands, /git .*lfs|make -C/);
    } else {
      assert.equal(run.status, 0, run.stderr);
      assert.match(commands, /brew install ccache gmp php lua luajit boost/);
      assert.match(commands, /make -C .* build-libndls build-tools/);
      assert.equal(commands.includes('lfs pull'), scenario === 'download' || scenario === 'partial-cache');
      assert.match(readFileSync(envFile, 'utf8'), /PKG_CONFIG_PATH=.*luajit.*gmp/);
    }
  }
});
