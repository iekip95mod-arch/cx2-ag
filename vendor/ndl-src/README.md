ndl r2022 for TI-Nspire OS <= v6.4.0.74
===========================================

Installing ndl on your calculator makes it possible to run assembly programs.

ndl combines an executable loader and utilities to open the TI-Nspire to third-party C and assembly development.

This is the local ndl fork of Ndless. Upstream project and attribution links remain below.

The upstream website is available on:

->    http://ndless.me

The source code can be found on:

->    https://github.com/ndless-nspire/Ndless 

A user guide is available online, make sure to read for more installation instructions:

->    https://tiplanet.org/ndless

A guide for Developers can be found here:

->    https://ndlessly.wordpress.com/ndless-for-developers/

Pull requests and and issues are welcome!

For more in-depth info, visit the wiki:

->    https://hackspire.org

Local SDK and compatibility
===========================

Runtime sources live in ndl and SDK sources in ndl-sdk. Add ndl-sdk/bin to PATH. The compiler installation lives in ndl-sdk/toolchain/install. User headers and archives are read only from ~/.ndl/include and ~/.ndl/lib.

NDL_TOOLCHAIN_PATH and NDL_ZEHN_PATH select explicit overrides. The former _NDLESS_TOOLCHAIN_PATH and _NDLESS_ZEHN_PATH variables remain fallback aliases when the new variable is unset. genzehn accepts --ndl-min, --ndl-max, --ndl-rev-min and --ndl-rev-max. The old --ndless-* options remain aliases and conflicting old/new values are refused.

The public nl_ndl_rev and assert_ndl_rev names reuse the existing nl_ndless_rev and assert_ndless_rev binary symbols. Old syscall numbers, ZEHN flag values, NDL/NDLESS stage macros and ZEHN enum aliases remain compatible. The Lua global ndl and the legacy global ndless reference the same table. Public os.h, libndls.h and nucleus.h filenames remain stable.

Upstream URLs, copyright notices, third-party source and the upstream ndless/gcc Docker base image retain their identities. The Luna submodule section name remains its existing Git identity. Its checkout location changes to ndl-sdk/tools/luna.

The runtime reads ndl.cfg.tns and starts programs from /documents/ndl/startup. The persistent installer copies /documents/ndl/persistent.tns into the OS current-document boot slot. A resident older runtime and its saved boot document retain their original filenames. Renaming files or resetting does not replace that saved installation. Preserve the old resources and boot material until an explicit uninstall and reboot have completed, then launch the selected new installer. Opening the resources document requests uninstall and reboot. It does not install the runtime.

The ordinary runtime uses /documents/ndl. External /appdata/ndl loading belongs to the separate relocation patch in the cx2 research/folder-hiding project. Its earlier emulator results do not qualify this renamed build on a handheld.

Run the local checks from this repository root:

~~~sh
python3 tests/test_naming.py
python3 tests/test_zehn_names.py
python3 tests/test_persistency.py
php ndl-sdk/libsyscalls/test_generator.php
python3 ndl-sdk/libsyscalls/test_open.py
~~~

Persistent installation stages both boot files before replacing an existing pair. A failed publication restores the previous pair where possible and reports failure. Retained .ndl.tmp or .ndl.bak files block another attempt so recovery material is not overwritten. Preserve those files if cleanup or rollback fails. A failed persistent installation can leave the ordinary runtime loaded for the current session. A failed uninstall does not proceed to reboot. Host fault tests do not qualify the calculator filesystem or reboot behavior.

Quick guide
===========

OS 6.2.0.333 + 6.4.0.74 <img src="https://i.imgur.com/5nnLNsN.gif" align="right">
--------

Prerequisites: OS 6.2.0.333 or 6.4.0.74 on CX II, CX II-T or CX II CAS. 

### Standard Installation
* Set your language to English through the Settings menu
  * Press `[Home]` > `[5]` (Settings) > `[1]` (Change Language) -> `English`
* Transfer `ndl_installer_4.5.5-6.2.0-6.4.0.tns` into a folder named "ndl" (top-level)
* Transfer `ndl_resources.tns` into the same folder
* Open the installer, press any alphanumeric key and wait
* If it fails or reboots, establish whether a runtime is resident before another attempt. Do not launch an installer over a resident runtime.
* Have fun!

### Persistent Installation
A persistent installation means ndl will automatically load when you turn on or reboot the calculator, removing the need to run the installer every time.

**Warning:** This feature is experimental and not ready for daily use. The Ndless team is not responsible for any data loss, reboot-looped calculators, or any damage incurred to your Nspire.

* Ensure no runtime is resident. If persistence is already installed, use that installation's resources document to uninstall and wait for the reboot. A reset alone can reload persistence.
* Set your language to English through the Settings menu
  * Press `[Home]` > `[5]` (Settings) > `[1]` (Change Language) -> `English`
* Transfer the local build's ndl/calcbin/persistent_6.2.0-6.4.0.tns into the top-level ndl folder as persistent.tns.
* Transfer ndl/calcbin/ndl_resources.tns from the same build into that folder and verify both transferred files.
* Open persistent.tns and wait for installation. This document contains its own installer and does not require running the standard installer first.
* Verify visible runtime operation, then reboot and verify it again before treating persistence as working.
* Have fun!
  * If a reboot loop occurs, hold ESC during boot. Preserve recovery copies before using destructive maintenance options.
  * By running the `ndl_resources.tns` file after installing, you can uninstall the loader as well.

OS 4.5.5.79 <img src="https://i.imgur.com/oJGpCsB.gif" align="right">
--------

Prerequisites: OS 4.5.5.79 on CX or CX CAS

* Set your language to English through the Settings menu
  * Press `[Home]` > `[5]` (Settings) > `[1]` (Change Language) -> `English`
* Determine your "boot1" version (needed later).
  * Press `[Home]` > `[5]` (Settings) > `[4]` (Status) > About button.
  * Here is a YouTube [video](https://youtu.be/htr106_ggjg) for this.
* Transfer `ndl_installer_4.5.5-6.2.0-6.4.0.tns` into a folder named "ndl" (top-level)
* Transfer `ndl_resources.tns` into the same folder
* Open the installer, press the corresponding key for your boot1 version and wait
* If it fails or reboots, establish whether a runtime is resident before another attempt. Do not launch an installer over a resident runtime.
* Have fun!


OS 5.2.0.771 + 5.3.0.564 <img src="https://i.imgur.com/kozAxpP.png" align="right">
--------

Prerequisites: OS 5.2.0.771 or 5.3.0.564 on CX II, CX II-T or CX II CAS

* Transfer `ndl_installer_5.2.0-5.3.0.tns` into a folder named "ndl" (top-level)
* Transfer `ndl_resources.tns` into the same folder
* Open the installer and wait
* Have fun!

OS 4.5.3.14 + 4.5.4.48 <img src="https://i.imgur.com/uuO3ue9.png" align="right">
--------

Prerequisites: OS 4.5.3.14 or 4.5.4.48 on CX or CX CAS

* Transfer `ndl_installer_4.5.3-4.5.4.tns` into a folder named "ndl" (top-level)
* Transfer `ndl_resources.tns` into the same folder
* Open the installer and wait
* Have fun!

OS 4.5.0 <img src="https://i.imgur.com/ZdWgSCq.png" align="right">
--------

Prerequisites: OS 4.5.0 on CX or CX CAS

* Transfer `ndl_installer_4.5.0.tns` into a folder named "ndl" (top-level)
* Transfer `ndl_resources.tns` into the same folder
* Open the installer and wait
* Have fun!

OS 4.4.0 <img src="http://i.imgur.com/mDOg6JG.png" align="right">
--------

Prerequisites: OS 4.4.0 on CX or CX CAS

* Transfer `ndl_installer_4.4.0.tns` into a folder named "ndl" (top-level)
* Transfer `ndl_resources.tns` into the same folder
* Open the installer
* Close the installer with Ctrl+W
* Have fun!

OS 4.2.0 <img src="http://i.imgur.com/GS2K9tS.png" align="right">
--------

Prerequisites: OS 4.2.0 on CX or CX CAS

* Transfer `ndl_installer_4.2.0_cx.tns` or `ndl_installer_4.2.0_cascx.tns` into a folder named "ndl" (top-level)
* Transfer `ndl_resources.tns` into the same folder
* Open the installer
* Close the installer with Ctrl+W
* Have fun!

OS 4.0.3 <img src="https://i.imgur.com/oEsrtC2.png" align="right">
--------

Prerequisites: OS 4.0.3 on CX or CX CAS

* Transfer `ndl_installer_4.0.3_cx.tns` or `ndl_installer_4.0.3_cascx.tns` into a folder named "ndl" (top-level)
* Transfer `ndl_resources.tns` into the same folder
* Open the installer
* Close the installer with Ctrl+W
* Have fun!

OS 3.9.1 <img src="https://i.imgur.com/rT8Ltmy.png" align="right">
--------

Prerequisites: OS 3.9.1 on CX and CX CAS

* Transfer the `ndl_installer_3.9.1.tns` file into a folder named "ndl" (top-level)
* Transfer `ndl_resources.tns` into the same folder
* Disconnect your USB cable
* Open the installer
* If you get a message that says "Insufficient memory", reset your calculator and try again!
* Connect the handheld to your computer!
* Close the installer with Ctrl+W
* Have fun!


OS 3.9.0 <img src="https://i.imgur.com/V9U8RSc.png" align="right">
--------

Prerequisites: OS 3.9.0 on Clickpad (CAS/non-CAS) or Touchpad (CAS/non-CAS)

For CX and CX CAS please install 3.9.1.

* Transfer `ndl_installer_3.9.0_classic_new.tns` into a folder named "ndl" (top-level)
* Transfer `ndl_resources.tns` into the same folder
* Open the installer
* Close the installer with Ctrl+W
* Have fun!

OS 3.6 <img src="https://i.imgur.com/6cq8g7Z.png" align="right">
------

Prerequisites: OS 3.6

* Transfer `ndl_installer.tns` and `ndl_resources.tns` into a folder named "ndl" (top-level)
* Open the installer
* Press the menu button
* Have fun!

Uninstalling
------

To uninstall ndl, simply run the `ndl_resources.tns` file and select "Yes", after which the device will reboot.

Development team
================

Lead maintainer:
  Fabian Vogt aka Vogtinator - < vogtinator at ritter dash vogt dot de >

Creators:
  Geoffrey Anneheim aka geogeo - < geoffrey dot anneheim at free dot fr >
  Olivier Armand aka ExtendeD  - < olivier dot calc at gmail dot com >

Contributors and testers: Adriweb, AtlassianDell, bsl, cherpixel, critor, Excale, Goplat, hoffa, Jim Bauwens, Legimet, Levak, Lionel Debroux, lkj, sasdallas, satyamedh, tangrs, timmycraft... and many others.  
More contributions can be seen on [the GitHub repo](https://github.com/ndless-nspire/Ndless/graphs/contributors).

Ndless is brought to you by https://codewalr.us, https://www.omnimaga.org and https://tiplanet.org

Legal stuff
===========

Most of the work is covered by the Mozilla Public License, version 1.1 (MPL). 
Please read the file [`Mozilla-Public-License-v1.1.html`](./Mozilla-Public-License-v1.1.html) carefully before distributing any part of Ndless, with or without modification.

Some parts are covered by other licenses. Others are in the public domain. These parts are identified by the files `LICENSE.txt` or `LICENSE.html` in the sub-directory.
