# Document concealment on the TI-Nspire CX II

Current source uses ndl. Its runtime roots are /documents/ndl and /appdata/ndl, with ndl_resources.tns, ndl.cfg.tns and ndl_installer_4.5.5-6.2.0-6.4.0.tns. Build with build-relocated-ndl.sh. The retained observations and artifact hashes below describe the earlier spelling and have not been requalified under the renamed runtime.

This research targets casual browsing. The files remain on the calculator and the design does not encrypt their contents.

The interface is test_app.tns, a working calculator. Its restore trigger is exactly 1+9+9+8 with spaces ignored. A successful restore displays 27 and omits the expression from the result history. Other calculations do not restore anything. There is no restore dialog. Missing or failed native support displays Unavailable while ordinary calculations remain usable.

This prototype's acceptance target is the files supplied with the OS plus test_app.tns. It therefore places its runtime and native support outside My Documents. That target does not prescribe the active StepCAS handheld layout. The earlier runtime passed emulator relocation, reboot and broad concealment checks. Those results have not been repeated with the renamed build. Live secret-trigger restoration and physical migration of this prototype remain pending in the completion record below.

## Verified folder experiment

The retained isolated Firebird session is file-hide-research. It used a private copy of images/nspire-os.img on the CX II non-CAS configuration. The available OS package is images/TI-NspireCXII-6.4.0.74.tco2. The physical read recorded for this experiment reports OS 6.40.74, Boot1 5.0.42 and Boot2 6.20.7 on hardware 0x1d. The original keysvc issue reported OS 6.20.333. Recheck device identity before a new migration.

The physical OS and extracted 6.4 package now match by version. Factory preservation still requires a live inventory and content checks before hiding. Emulator results do not establish physical boot or restoration behavior.

The first experiment created HideLab with ordinary files, a Folder directory and a LockedFolder directory containing Inside.tns and Nested/Deep.tns. It compared several ways of concealing them.

| Operation | Browser result |
| --- | --- |
| Rename Dot.tns to .Dot.tns | Still visible as .Dot |
| Rename Folder to .Folder | Still visible as .Folder |
| Rename Extension.tns to Extension.bin | Absent from the document list |
| Move Outside.tns outside /documents | Absent from the document list |
| Move LockedFolder outside /documents | Folder and its nested contents absent |

The whole-folder operation used rename from /documents/HideLab/LockedFolder to /hide-research-store/LockedFolder. The inverse rename restored it. The folder remained concealed across emulator restarts.

The historical probe accepted the test PIN 4931 through a native input dialog. A normal browser launch restored all fixtures. Entering 4930 and pressing Cancel both left them concealed. These logs retain the literal test PIN so the evidence distinguishes an actual wrong-code test from a failed input injection. This dialog is retained only as historical research code. It is not the requested interface.

Inside.tns and Nested/Deep.tns were fetched before hiding and after restoration. Both comparisons were byte-for-byte equal. Each file was 17,744 bytes with SHA-256 55fc9bb207ba83b0e9ae8e59f679b946578560e244a812eda01509657b7870cd. A separate real Lua document inside Nested also opened successfully.

Useful evidence files are [before.png](evidence/before.png), [hidden-expanded.png](evidence/hidden-expanded.png), [current-doc-browser.png](evidence/current-doc-browser.png), [native-hide.log](evidence/native-hide.log), [native-restore.log](evidence/native-restore.log), [native-wrong.log](evidence/native-wrong.log), [native-cancel.log](evidence/native-cancel.log) and [restart-restored.log](evidence/restart-restored.log).

## Execution and browser refresh

An early debugger-injected run completed every rename but reset the emulator afterward. Later injected native-dialog runs showed delayed or missing service input and invalid input results. A screenshot even showed 431 after 4931 had been requested.

Launching the same operations from the ordinary document browser produced successful hide and restore operations without a reset. The traced restore also completed both refresh_homescr and refresh_docbrowser. The evidence therefore supports using the normal browser to launch these probes. It does not establish that refresh_osscr itself caused the initial reset. The debugger's broad task-stack test does not prove that an arbitrary interruption point is safe for nested OS UI calls.

The native hide program closed the test's current Lua document. Current was disabled on the home screen afterward. Selecting Recent and pressing Enter did not expose a document in that experiment. This is a limited check with the disposable fixture, not proof that every OS version or saved-document history is cleared.

[nSonic's historical tutorial](https://tiplanet.org/forum/viewtopic.php?f=57&lang=en&t=17640) describes whole-folder concealment and warns that a previously opened private document can remain accessible until another document opens. The [TI guide](https://education.ti.com/html/eguides/nspire/eg_nspire/en/content/m_docshh/wc_closing_a_document.HTML) documents Ctrl+W for closing a document and the save prompt for modified documents. Save or close personal work before hiding it.

## Identifying factory files

The factory-6.4 directory preserves the actual files from resources.zip and samples.zip inside the OS package. Its extractor records the nested archive member, installed-name candidate, byte count, CRC32 and SHA-256. The package SHA-256 is 4ea892106c884a5dc2e4f6ab8f4cfdfe9477a275c64088570edec6e77d141b9e.

Common documents are the MyLib libraries, MyWidgets stopwatch and PyLib modules. Samples have localized names supplied by copysamples. Examples is the candidate sample installation directory and must be checked against the live inventory. The source archive does not explicitly supply that directory name.

The emulator inventory verified the Examples installation directory. Every present factory file matched its expected relative name, size and CRC32, including both English examples. The extraction manifest retains its original distinction between archive evidence and the later runtime check.

Preservation requires the expected relative filename, size and CRC32. A personal file inside MyLib or Examples is still personal. A modified copy of a shipped file is also retained in concealed storage rather than silently treated as the original. Missing factory files are not recreated.

CRC32 is used for recognizing unchanged factory contents. SHA-256 records the provenance of the retained reference files. Neither is an access-control mechanism.

## Runtime locations and retained dependencies

Stock CX II bootstraps name A:\\documents\\ndless\\ndless_resources.tns. The runtime scans ndless/startup relative to the documents directory. Lua nrequire searches below the documents directory and compares basenames. An absolute module name is not an alternative lookup method.

Those are the original runtime names. The current ordinary SDK runtime uses /documents/ndl. Moving that directory alone still breaks its next load. The separate relocation patch supports /appdata/ndl when stat succeeds for ndl_resources.tns and otherwise selects /documents/ndl. The patched bootstrap, startup directory, configuration and persistence source use this selection. Startup scans only the selected installation. Patched Lua module lookup checks the external directory before ordinary documents.

Selection is an existence check rather than an integrity check. A corrupt external resource whose stat succeeds still selects the external installation. A later load failure does not trigger a second attempt from documents. Verify the resource bytes before booting. The earlier external installation survived an emulator restart. The renamed relocation build and its documents-only fallback still require runtime qualification.

| Native filesystem location | Purpose |
| --- | --- |
| /documents/test_app.tns | The calculator document that remains visible |
| /appdata/ndl/ndl_resources.tns | Preferred custom runtime in the relocation build |
| /appdata/ndl/persistent.tns | Preferred persistence installer document |
| /appdata/ndl/startup/keysvc.tns | Resident service loaded during external startup |
| /appdata/ndl/calc_helpers.luax.tns | Calculator restoration module |
| /documents/ndl | Fallback installation root in the relocation build |
| /appdata/test_app/store | Concealed contents and recovery manifests |
| /appdata/test_app/actions.log | Native hide and restore operation results |
| /appdata/relocation.log | Migration and rollback results |

The native filesystem and document-transfer namespace differ. During the original physical investigation a transfer to /ndless/startup/keysvc.tns worked while /documents/ndless/startup/keysvc.tns failed with Path does not exist. Use the transfer tool's observed document-relative names to stage files. Native programs use the absolute locations above to move them outside My Documents.

The [relocation patch](ndl-relocation.patch) is built in a private source copy so the shared ndl checkout remains unchanged. The [runtime manifest](relocated-runtime/manifest.json) and artifact table below retain the earlier selected build and its original names. Their validation statements do not apply to a newly rebuilt or renamed runtime.

| Runtime artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| ndless_resources.tns | 196680 | c665ea6d61cca466cdd3db62224806c898ca533bf487e363fc3389fd2805a91a |
| persistent.tns | 4229 | c6b20c007cc0d81217acada1deb06591b13496261b934ebcb4ed8b9c3783a0e6 |
| ndless_installer_4.5.5-6.2.0-6.4.0.tns | 8490 | 1c92098d2e112e6f20d6be78d0c77b9c7d2bda8a4eaf3d52aaeb461f6ff788af |

## Storage format and recovery

Each hide cycle creates the first unused directory named batch000001, batch000002 and so on below /appdata/test_app/store. It records relative original names in manifest.tmp and closes it before renaming it to manifest.bin. Only then do document contents move into slots named item00000000, item00000001 and so on. Entire non-factory folders move as units, including empty folders. Factory folders remain in place while personal additions inside them move separately.

The binary manifest begins with the eight bytes CX2HIDE1 followed by a little-endian 32-bit entry count. Each entry has a little-endian 16-bit path length followed by that many path bytes. The entry's zero-based index identifies its item slot. The relative name is resolved under the documents root.

The restore operation checks every unfinished batch before moving anything. It validates manifests, parent directories and destinations as well as the stored entries.

| Stored item | Original location | Restore action |
| --- | --- | --- |
| Present | Absent | Move the item back |
| Absent | Present | Already restored or never moved |
| Present | Present, both regular files with identical bytes | Keep both copies and continue |
| Present | Present with different contents or either a directory | Refuse the collision |
| Absent | Absent | Report missing contents |

A successful restore creates an empty done directory inside each completed batch. Manifests remain available afterward. If an identical regular file already occupies the original location, the stored copy also remains in the completed batch. No contents are overwritten or deleted. A new hide refuses to proceed while any unfinished batch exists. Restore that batch first. Repeated completed cycles use new batch directories and can include files added since the previous cycle.

The first live bridge test loaded calc_helpers and entered its restore callback successfully. Restoration then stopped at /documents/NspireLogs.zip because the OS had recreated that empty file after reboot. The original zero-byte log was still in storage. The updated comparison handles identical regular files generically, including nonempty files. Differing files and directory collisions still stop the operation. Host tests for the fix pass. The updated module has not yet completed its emulator restoration test. This failure did not establish a browser-refresh defect. The diagnostic screenshot is /tmp/cx2-hide-research/bridge-diagnosis.png.

The host suite exercises interrupted moves and restores, absent directory parents, filename collisions, modified factory documents and directories with more than 100 entries. Collision tests include empty files, identical 8193-byte files, same-size files differing in the last byte and directories. They check that the stored original keeps its contents and inode and that retrying a completed restore is harmless. The suite also rejects invalid manifests, unexpected stored entries and overlapping roots. The implementation bounds full paths to 1024 bytes and nesting to 64 levels. The format is not a full power-loss durability guarantee for the calculator filesystem. Keep document writes and transfers idle during a hide or restore because the filesystem operations do not provide a concurrent no-replace transaction.

Current migration source keeps the old resources as /appdata/ndl/ndl_resources.stock.tns and the old persistence document as /appdata/ndl/persistent.stock.tns. Boot arming keeps the previous OS snapshot files at /appdata/currentdoc.stock.tns and /appdata/currentdoc.stock.data. Earlier artifacts retain the ndless spelling. Failed copy preparation can leave currentdoc.next files. Move those aside after inspecting them before retrying. Do not discard the preserved originals.

If a restore reports a collision, preserve both versions and move the newly created visible file or folder to an unused location. Retry with the original destination free. Do not remove a batch manifest or create a done marker to bypass a check. A missing-item error requires finding the missing contents from the retained inventory or backups before another attempt.

If test_app displays Unavailable, check that the selected runtime survived the restart and that its calc_helpers.luax.tns matches the selected build. Current sources use /appdata/ndl and retained earlier installations may still use /appdata/ndless. The generic app message does not distinguish a missing module from a storage collision. With the runtime working, a freshly transferred RestoreAll.tns can invoke the same storage restoration directly. It writes action=2 and the return code to /appdata/test_app/actions.log. Preserve that log before changing anything. RestoreAll is a recovery probe and becomes visible while staged.

If migration fails, inspect /appdata/relocation.log before retrying. Relocate attempts reverse moves after an ordinary move failure. Current source refuses to start when /appdata/ndl already exists, including a partial migration. ArmBoot similarly retains previous boot files and reports attempted rollback. A partial operation requires inspecting every source and destination before completing or reversing it. Keep the currentdoc.tns and currentdoc.data pair together when restoring the saved boot state. Do not launch another installer over a still-resident runtime as a retry method.

The pre-relocation emulator flash is /tmp/cx2-hide-research/pre-relocation.img. Stop only file-hide-research before restoring its private flash from that backup. Preserve the failed flash separately first. Restoring this image also restores the earlier document contents. It cannot recover personal work added after that backup.

## Build and test

Run these commands from the cx2 workspace root. The Makefile selects ndl-src/ndl-sdk and writes ordinary probe builds into research/folder-hiding/build. The relocation script copies selected sources into an unused directory before applying its patch and rebuilding the SDK configuration library and runtime. It reuses the existing compiler installation.

```sh
make -C research/folder-hiding all
make -C research/folder-hiding test
bash research/folder-hiding/build-relocated-ndl.sh /tmp/cx2-ndl-new-build
```

The last command requires an unused destination. It prints the generated runtime hashes. Its artifacts are under ndl-src/ndl/calcbin within that destination. Record a new manifest for a new build and keep the retained runtime manifest with its original artifacts. Native programs are packaged with HW-W 240x320 support. Host tests use the system C compiler with AddressSanitizer and UndefinedBehaviorSanitizer. Calculator tests use LuaJIT, configurable with the LUA make variable. The retained fixture directories under /tmp are intentionally not deleted by the test target.

## Retained installation and validation sequence

The sequence below records the earlier ndless build that reached external boot and persistent concealment in the isolated 6.4 emulator. Its filenames are preserved as evidence, not current deployment commands. Use the current source locations above for a new build and requalify every boot and restore step. The live secret calculation and physical migration remain pending. A matching OS version alone cannot identify modified factory files or personal additions.

1. Save the private emulator flash and retain a backup. For hardware retain fetched copies of documents and the current runtime before migration. Close or save ordinary documents.
2. Stage the custom resources at /documents/ndless_resources_custom.tns, the custom persistence document at /documents/persistent_custom.tns and the native bridge at /documents/calc_helpers.luax.tns. Verify the transferred bytes against the selected build.
3. Transfer test_app.tns and the temporary Inventory, Relocate, ArmBoot and HideAll test programs to the documents root.
4. Launch Inventory from the normal document browser. Fetch InventoryReport.tns and check the preserve/conceal decisions before a broad move. Retain the inventory outside the calculator along with reference copies for the later byte comparisons.
5. Launch Relocate from the normal browser. It moves the existing Ndless directory outside My Documents, preserves the old runtime files and installs the staged replacements. Missing files, empty files, directory substitutions and destination collisions are rejected. Inspect /appdata/relocation.log and require relocation rc=0 before arming the next boot.
6. Launch ArmBoot. It prepares the new OS current-document boot slot while retaining the old one. Fetch RelocationStatus.tns and require arm boot rc=0.
7. Save the emulator flash and restart once. Confirm /documents/ndless is absent and keysvc restarted from the external startup directory. The service query must return 6b 01 02 07. Do not execute a second Ndless installer over the still-resident stock runtime. Verify native Lua loading separately from service startup.
8. Launch HideAll. It also conceals the temporary probe programs and reports kept under documents. Check that only factory files and test_app remain visible. Restart and check this again. The emulator showed Examples, MyLib, MyWidgets, PyLib and test_app after both checks.
9. Open test_app. Confirm an ordinary calculation works. Enter 1+9+9+8 and require the normal result 27. Reopen the browser and compare restored documents with the pre-hide copies.

The debugger stops the guest before each command. Resume with c. A keysvc acknowledgment records acceptance rather than completion. Inspect the next screen before advancing. The link uses a documents-relative namespace and its debugger parser does not handle filenames containing spaces as a single fetch argument. The native inventory avoids relying on those fetches to identify factory examples.

## Retained code

- hide_probe.c, proof.c and current_document.lua preserve the original folder experiments.
- test_app.lua implements the calculator interface and secret expression.
- calculator_test.lua checks arithmetic, editing, trigger matching and missing native support.
- storage.c and storage.h implement factory recognition and reversible document moves.
- calc_helpers.c exposes restoration to the calculator's Lua document.
- bridge_probe.lua exposes native module loading and restore errors for diagnosis. Its visible restore wording belongs only to this temporary probe.
- storage_tool.c provides native inventory and hide probes for emulator testing.
- relocate.c stages the external Ndless directory while retaining the original resources.
- arm_boot.c prepares the next persistence boot while preserving the previous OS snapshot pair.
- relocate_test.c and arm_boot_test.c exercise migration failures and rollback without a calculator.
- ndl-relocation.patch and build-relocated-ndl.sh build the runtime in an isolated source copy.
- relocated-runtime retains the selected install artifacts and their hash manifest.
- factory-6.4 contains the reproducible factory manifest and reference bytes.

The code and evidence are retained for further work. Read [AGENTS.md](AGENTS.md) before changing the storage format, runtime location or installation sequence.

## Completion record

| Check | Result | Scope |
| --- | --- | --- |
| Original whole-folder hide and restore | Passed | Isolated emulator with disposable fixtures |
| Historical wrong PIN and Cancel | Passed | Both left the original fixtures concealed |
| Historical restart persistence and restored-byte comparison | Passed | The retained evidence above records both |
| Native programs and calculator document build | Passed | Build artifacts alone do not establish live behavior |
| Storage, relocation and boot-arming host suites | Passed | AddressSanitizer and UndefinedBehaviorSanitizer |
| Calculator host suite | Passed | Arithmetic, editing, exact trigger and missing-module handling |
| Runtime clean rebuild and configuration suites | Passed | See relocated-runtime/manifest.json |
| Independent production-code review | Clean | Reviewed before the subsequent identical-file collision fix |
| Current physical version read | Passed | OS 6.40.74, Boot1 5.0.42, Boot2 6.20.7 |
| Relocated external runtime after reboot | Passed | Emulator service query returned 6b 01 02 07 |
| Broad HideAll visible contents | Passed | Emulator showed four factory folders and test_app |
| Broad concealment after another reboot | Passed | Same emulator visible contents remained |
| External native module loading and restore callback | Passed | Emulator probe reached the NspireLogs.zip collision |
| Identical-file collision fix | Host tests passed | Updated emulator module validation pending |
| Live calculator secret restore and restored-byte comparison | Pending | Recreated log collision diagnosed and host fix tested |
| Physical runtime migration, hide and restore | Pending | Current version read is the only physical result recorded here |
| Documents-only fallback boot | Pending | Implemented and host-tested selection, no runtime claim |

Append final runtime evidence here with the tested artifact hashes, calculator version, screenshots and restored-byte comparisons. Keep emulator and physical results separate.
