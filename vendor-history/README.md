# Vendor history

The vendored trees under this repository used to be nested checkouts with their own Git state. They
are tracked as ordinary files now, so that state is gone. Anything that existed only inside one of
those `.git` directories is preserved here.

## ndl-src

Upstream is https://github.com/ndless-nspire/Ndless.git, branch master. The checkout sat 7 commits
ahead of upstream `9484d8d`, and those 7 commits were never pushed anywhere, so this directory is
the only copy. `0001` through `0007` apply in order onto `9484d8d`:

| Patch | Commit | What it is |
| --- | --- | --- |
| 0001 | 22dac0e | Fix Lua number syscall ABI |
| 0002 | 184eb40 | Preserve syscall contracts during regeneration |
| 0003 | eb22213 | Honor file creation and descriptor contracts |
| 0004 | dbf40f3 | Rename the runtime and SDK to ndl |
| 0005 | 34a265a | Clarify persistent runtime installation |
| 0006 | 06d0e8d | Preserve boot files on persistence failures |
| 0007 | 73da543 | Expose loader failure stages to Lua |

Two stashes were also saved, as plain diffs rather than as commits, because a stash carries no
useful parentage once its repository is gone:

- `stash-0-retain-sdk-edits-before-ndl-rename.patch`, was `stash@{0}`, "On master: Retain SDK edits before ndl rename"
- `stash-1-retain-obsolete-generated-syscall-output.patch`, was `stash@{1}`, "On master: Retain obsolete generated syscall output before SDK repair"

The working tree those commits produced is what is tracked under `ndl-src/` now, so these patches
are history rather than pending work. They matter if the fork ever has to be rebuilt on a newer
upstream, or if one of the seven has to be read on its own.

## libnspire-src

Upstream is https://github.com/Vogtinator/libnspire.git, branch master. The checkout sat 11 commits
ahead of upstream 7d7d962, and those 11 commits were never pushed anywhere, so this directory is
the only copy. 0001 through 0011 apply in order onto 7d7d962:

| Patch | Commit | What it is |
| --- | --- | --- |
| 0001 | c45c696 | Recover CX II connections without resetting the USB bus |
| 0002 | 3bc42ce | Preserve early USB replies |
| 0003 | 4839944 | Report failed device service completion |
| 0004 | 2ab7fd6 | Trace file transfer failure phases |
| 0005 | 48fdcb4 | Add standard OS key transport |
| 0006 | 12d91cd | Trace handshake frame progress |
| 0007 | 879f105 | Trace validated NavNet header metadata |
| 0008 | 3c8114e | Suppress duplicate stream delivery |
| 0009 | 4dc3d3e | Preserve partial USB reads on timeout |
| 0010 | bf7db6b | Wait for handshake progress until its deadline |
| 0011 | f6764a0 | Distinguish unavailable USB configuration |

What is tracked under libnspire-src is not f6764a0's tree. Eleven files were still modified in the
working copy when the checkout was flattened, and those edits are now tracked as files with nothing
recording that they sit on top of the last commit: config.h.in, src/cx2.cpp, src/cx2.h,
src/handle.h, src/init.c, src/service.c, src/usb.c, src/usb.h, and the diagnostics, key, service
and transport tests. Diff against the bundle to see them on their own.

upstream-and-local.bundle carries the same 11 commits as real objects, along with the branch and
worktree refs that pointed at them. It is not a whole clone. Its pack stops at 7d7d962, whose own
parent is absent, so git bundle verify calls it complete while a clone from it fails partway up the
upstream ancestry. The local work is what this preserves. Fetch the rest from upstream if the fork
ever has to be rebuilt.

## The other trees

`firebird-src` and `giac-src` each sat exactly on an upstream commit with nothing unpushed and no
stashes, so nothing needed saving. Their working-tree modifications are tracked as files like
everything else. Their upstreams and the commits they were taken from:

| Tree | Upstream | Commit |
| --- | --- | --- |
| firebird-src | https://github.com/nspire-emus/firebird.git | b10f3b5 |
| giac-src | https://github.com/dmaugis/giac.git | 8ffc722 |

`khi-src`, the active Giac fork, never had a `.git` directory here, so it has no upstream commit to
record and no history to lose.
