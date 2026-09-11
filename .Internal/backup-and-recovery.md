# Backup and recovery

## Current recovery boundaries, 2026-09-08

Retain verified document copies, hidden-storage manifests, original runtime files and their hashes before a migration. A folder listing alone does not cover the prototype's /appdata storage. Its recovery rules are in [research/folder-hiding/README.md](../research/folder-hiding/README.md). Keep each saved currentdoc.tns and currentdoc.data pair together.

Boot and partition dumps and assembled emulator images already exist under images. The earlier statement below that no CX II dumper was available is superseded. Those files support emulator provenance. They do not establish a tested full-device restore procedure. The OS package images/TI-NspireCXII-6.4.0.74.tco2 also exists. Recheck the connected device's variant and version before choosing any OS image. Do not assume the earlier 6.2 inventory describes it.

Current physical file operations use tools/nsptool through the serialized USB session. Keep the original files until their copied bytes are verified. If an operation times out, inspect its outcome before another attempt. An upload receipt does not establish successful execution and an unverified download does not replace the original recovery copy.

The local runtime sources and installation guide are in [ndl-src/README.md](../ndl-src/README.md). Current files use the top-level ndl folder. A still-resident older installation may use the earlier names, including its saved persistence document. Reset alone can reload that installation. Its matching resources document performs explicit uninstall and reboot. Do not open resources as an installation step or run a second installer over a resident runtime. The current persistent.tns contains its own installer.

At the 2026-09-07 migration checkpoint, renamed package bytes were staged and read back. The older runtime was explicitly uninstalled and the device rebooted. The new startup service and StepCAS were later observed, but the service did not return after a hard restart. Later cleanup retained the essential /ndl and /nps installation. The 2026-09-08 [physical check](../nps/benchmarks/BUDGETS.md#equation-opening-check-2026-09-08) then identified the loaded StepCAS release and reported Ndl revision. It did not repeat a restart or hash the resident runtime. Persistence remains unqualified. Preserve the exported originals and recovery material independently of the current installation.

SDK commit 06d0e8d2752f61d03abc9e924c8a9ca86f2cd2a8 checks persistence I/O failures and stages replacement boot files before publication. Failed rollback or cleanup retains recovery files. Completed publication with failed backup cleanup retains its hooks and reports a warning. The host fault tests pass, but the repaired runtime has not been qualified through a handheld reboot. This repair is not evidence that persistence caused the current key-service timeout.

## Historical research and superseded advice

The remaining notes predate the retained image dumps, current USB client and persistence work. Their quoted upstream warnings and original filenames are preserved. The blanket advice against persistence, n-link preference, unavailable-image claims and installer retry instructions are not the current procedure. Confirm recovery-menu controls against current device documentation before using them. Formatting or reinstalling an OS is not a prerequisite for the ordinary runtime migration.

## The blunt answer

There is no full-image backup for a CX II. You cannot dump the NAND, boot1, boot2 or the OS to a
file and restore it later. The tooling that did that on older Nspires predates the CX II, and the
CX II changed the flash layout. A web search of nsNandMgr coverage reports that CX II NAND writing
was intentionally left out to avoid bricking, and that flash and ROM dumping software still needs
updating for the model. That report is second-hand and not verified against the tool's own source,
but nothing found so far contradicts it and no working CX II dumper turned up.

So "backup" here means two separate things, and only one of them is a file you hold:

1. **Your documents.** Copyable off the device. Do this.
2. **The OS.** Not dumpable, but re-installable, because TI distributes the OS image and the
   calculator has a recovery path that does not depend on a backup you made.

## What can actually go wrong

Read the risk honestly before spending effort on the wrong safeguard. From the Ndless README
(ndless-src/README.md), the failure modes the project itself warns about are reboots, reboot loops
and needing to format the filesystem. Those cost data, not the calculator.

Quoted from ndless-src/README.md, standard install section:

```
* Open the installer, press any alphanumeric key and wait
* If it fails or reboots, try again until it works
```

And from the persistent install section:

```
**Warning:** This feature is experimental and not ready for daily use. The Ndless team is not
responsible for any data loss, reboot-looped calculators, or any damage incurred to your Nspire.
...
* If you get stuck in a reboot loop, hold `[ESC]`. If that fails, format your filesystem via the
  maintenance menu.
```

An Ndless program is a userland ARM binary. It can hang or crash the calculator, which reboots it.
Bricking, in the sense of an unrecoverable device, comes from writing bad data to NAND or
interrupting an OS flash. StepCAS development does neither. We compile C or C++ to a .tns and run
it.

**Do not use the persistent installation.** It is the one part of the Ndless workflow the authors
flag as experimental and reboot-loop prone, and it buys only the convenience of not re-running the
installer after a reset. Not worth it during development.

## Backing up documents

Use **n-link**, an open-source cross-platform linking program that explicitly supports CX II.

- Repo: https://github.com/lights0123/n-link
- Downloads and instructions: https://lights0123.com/n-link/

Listed features: connect to multiple calculators including CX II, browse, rename, upload and
download files, install an operating system, view software version information, and queue actions.

**Use the browser version, not the desktop build.** The latest release is v0.1.6 from 2021-04-06,
checked through the GitHub releases API, and its macOS assets are `n-link_0.1.6_x64.dmg` and
`n-link_0.1.6_x64.app.tgz`. Both are x64 only, so on this Apple Silicon machine they would run under
Rosetta. The web version at https://lights0123.com/n-link/ runs over WebUSB in any Chrome-based
browser and sidesteps both the architecture question and the unidentified-developer prompt.

libusb is installed here, checked with brew list, which the desktop build would need.

Practical routine, run before the first Ndless install and again whenever the device state matters:

1. Connect the calculator over USB.
2. Open n-link, browse the filesystem, and download everything. The documents tree is what matters.
3. Drop the copy under a dated folder outside this project, or in a folder here that is not
   committed anywhere.

TI's own Computer Link Software does the same job. n-link is preferred here because it is
cross-platform, supports CX II without driver replacement, and does not need a TI account.

## Recovery ladder

Try these in order. Each is more destructive than the last.

1. **Reboot loop after an install.** Hold ESC while it boots. From the Ndless README, persistent
   install section.
2. **Uninstall Ndless.** Run ndless_resources.tns on the calculator and select Yes. The device
   reboots. From the Ndless README, Uninstalling section. Note this file lives in the same ndless
   folder used for install and is already in ndless-r2022/.
3. **Maintenance menu.** Hold doc, enter and EE together, then press on. Reported options include
   Cancel, Delete Operating System, which keeps documents, and Delete Document Folder Contents,
   which keeps the OS. If the battery is out, hold the keys, connect power, wait for the replace
   battery warning, reconnect the battery and repeat. This came from a search summary of TI's
   knowledge base article at
   https://education.ti.com/en/customer-support/knowledge-base/ti-nspire-family/troubleshooting-messages-unexpected-results/20569
   and the exact key combination should be confirmed against that page before it is needed in
   anger.
4. **Reflash the OS.** Deleting the OS leaves the calculator waiting for one over USB. n-link can
   install an OS, as can TI's Computer Link Software.

## Get the OS image now, not later

Step 4 needs the OS file on disk. Download the **6.2.0.333** non-CAS CX II image and keep it
locally before touching anything, because a calculator sitting at the OS-install prompt is a bad
time to start hunting for a download.

Two things to keep in mind when sourcing it:

- Match the exact variant. The device is a **non-CAS CX II**. A CAS or CX II-T image is the wrong
  file.
- Reinstalling 6.2.0.333 keeps you on a supported Ndless target. Do not accept an OS update prompt
  to a version outside the supported list. Per http://ndless.me/ the only supported CX II builds
  are 5.2.0.771, 5.3.0.564, 6.2.0.333 and 6.4.0.74. Downgrade protection lives in the bootdata NAND
  area, so upgrading past a supported version may not be reversible.

**Not yet done.** The OS image has not been downloaded and its source has not been checked.

## Install procedure for reference

From ndless-src/README.md, the section for OS 6.2.0.333 and 6.4.0.74. Prerequisite is OS 6.2.0.333
or 6.4.0.74 on a CX II, CX II-T or CX II CAS.

```
* Set your language to English through the Settings menu
    [Home] > [5] (Settings) > [1] (Change Language) -> English
* Transfer ndless_installer_4.5.5-6.2.0-6.4.0.tns into a top-level folder named "ndless"
* Transfer ndless_resources.tns into the same folder
* Open the installer, press any alphanumeric key and wait
* If it fails or reboots, try again until it works
```

Both files are already present in ndless-r2022/. The English language requirement is real and easy
to overlook.

Ndless installed this way is not persistent, so it has to be re-run after any reset. That is the
safe trade and we take it.
