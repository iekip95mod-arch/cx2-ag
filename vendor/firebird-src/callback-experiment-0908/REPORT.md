# Frozen callback experiment

The experiment is partial. All owned emulator processes are stopped. No original Firebird source, object or binary was changed. No Giac source or object was changed. No physical USB action occurred.

## Confirmed result

Calling the actual loader callback at the interrupted PC while another stack is active restores the saved ARM state into that foreign context. The executable baseline fails 17 checks. The isolated SP guard passes 27 checks normally and under ASan plus UBSan.

The test includes the actual copied armsnippets_loader.c translation unit. It invokes the real snippet setup, blob setup and callback functions. It stubs guest memory access, translation flushing and diagnostic output. It sets simulated ARM registers to represent foreign task and returning snippet states. It does not execute ARM instructions or the CPU interpreter loop. Therefore its positive SP expectation is derived from the existing snippet instructions, not a completed observed guest return. The full isolated emulator compiles and runs the changed CPU code, but no callback-match log or successful diagnostic completion was observed in that run.

## Copied implementation

The three exact copied-file diffs are loader.patch, header.patch and cpu.patch. They describe files in this experiment directory relative to the original core files. Do not apply their relative paths blindly to another root.

The copied loader records the expected return SP. For snippets this is the stack after parameters minus four bytes, since the existing snippet saves LR and returns with an LDM without stack writeback. For raw blobs it is the entry SP. The callback also requires the original processor mode and instruction state. It returns a bool. The copied CPU loop clears the pending callback flag only after acceptance. Diagnostic logging is retained for qualification, bounded to five rejected returns plus one accepted return line.

This changes the internal armloader_cb declaration from void to bool. Its sole CPU caller is updated. The externally used snippet and blob setup signatures remain unchanged.

## Commands and outcomes

All commands ran in firebird-src/callback-experiment-0908.

~~~sh
clang -std=c11 -g -O1 -I../core callback_test.c -o callback-test-before
./callback-test-before > callback-before.log 2>&1
~~~

Before repair: exit 1, 31 checks and 17 failures. The count includes callbacks that should have been withheld, which add checks only in the broken run.

~~~sh
clang -std=c11 -g -O1 -I../core callback_test.c -o callback-test-after
./callback-test-after > callback-after.log 2>&1
clang -std=c11 -g -O1 -fsanitize=address,undefined -I../core callback_test.c -o callback-test-san
./callback-test-san > callback-san.log 2>&1
make -j4 experiment > experiment-build.log 2>&1
make -n experiment
~~~

After repair: both tests exit 0 with 27 checks and zero failures. Full isolated headless build exits 0. The final dry run lists only linking, with no stale translation units. The Makefile compiles original read-only source inputs and the three owned copies into this directory's objects directory. It never targets original objects or the active emulator binary.

## Runtime boundary

The original fault registers and disassembly are retained in /private/tmp/cx2-rref-trace-dJ18Da/trace2-context-mismatch.txt and trace2-fault-disassembly.txt. The current task was crit_not with stack 120dcbc4 through 120debb8, while the abort SP was 17fe0008. The OS stack check raised error 3 recursively. This observed mismatch is consistent with the reproduced callback defect, but the initiating callback was not captured directly.

The isolated guarded run injected at 103a4f90 immediately before the OS call at 1042b1d0. Execution did not reach the diagnostic's first file marker or complete a callback return. A protection-related blocked launch remains a hypothesis. A second run attempted inspected breakpoints, but did not execute the diagnostic. Both transcripts are retained as first-run.txt and second-run.txt. Both processes exited successfully after debugger stop. The managed rref-trace-0908 instance also reports stopped.

Normal Documents Browse and New render an empty black document area in this private image. Home and Esc work. This is a separate observed seed/UI blocker, not evidence of Giac failure. No actual ref/rref callback sequences or handheld/performance qualification were obtained.

## Preserved state and hashes

Firebird HEAD before and after is b10f3b51a9ab8e27d2703444b5e6bd278d4c5e6e. The index is empty, retained as original-index.patch. original-status.txt lists 18 pre-existing modified tracked files, retained build-qt5 and this new experiment directory. original-working-tree.patch snapshots the original tracked dirty work. No original file was edited. The three original copied sources were hashed before and after and remained identical.

SHA256 identities:

| Artifact | SHA256 |
| --- | --- |
| loader.patch | 8276798bc3431b15f16c10c365cff775d4b331f05f340464d21de86deed3808b |
| header.patch | bc1054d0ccc84bada1abf0cad3f5d110230c8fbdbddb09c3c6dcf3512eaa1196 |
| cpu.patch | 19b2073c0532f33c852ab3b27491edf79553dda340060145a5742a0acf45455c |
| copied armsnippets_loader.c | a3d39e9470c52c763498ea7691e22287d638a485bdd2fef54df204e36a6a3d12 |
| copied armsnippets.h | 1f1a5817a5f39444dbffde10179ff0789311fe55564eb2d2ff6a64729873400f |
| copied cpu.cpp | a8c350ef7bf461444cbffe2325c080e9d456c686d6b19f50bdac6a08b81c6ab3 |
| callback_test.c | f4066d41ba4a585fcf2d73844b7968908a82389787e7f6a1f84f2f551a08dccf |
| firebird-headless-experiment | cb61bc2d06bccc33991ddbba793f41f6193200f5e491fd374ae071065a912363 |
| original armsnippets_loader.c | 16f4ff7847879d3fcb257602f565440df4e295def3c862880ee14185a7c32f5d |
| original armsnippets.h | 50d96b40aade84db9fc43f2e2f7d7f58a81552943ccd38b2d90fcb089afa510e |
| original cpu.cpp | 73811771780a20db9fac6dc4de030541b508e141a34ad74be0da2109f5dabca2 |
| original headless binary | 7e741d92a155afa1512b2dbb429756b0c165d7d4a625206f9d85e7b149231d44 |
| original-status.txt | abc85aa7e3fe7ed794699fdc7d267cb2cc28539e8ade661f56ede12229789b90 |
| original-working-tree.patch | 1a1df8adc792040175a47417bf5255c03d7794dbffa79756e8c1fbd83fe328a0 |
| unchanged seed image | 6fbe80e8bb010ec24ef6a2e0abb13f08a376a2f2964ea95110dceaa3f3b727c3 |
| unchanged boot1 | 74227e458bb5c17f9028ac3d54e1d1726f3148601fb2fb24eacc2c294d5c9838 |

The only writable guest image used is images/.nspire-instances/rref-trace-0908/39d4820474b5420b-integer-os64-seed-20260907.img. The original seed, other instances and recovery images were not modified.
