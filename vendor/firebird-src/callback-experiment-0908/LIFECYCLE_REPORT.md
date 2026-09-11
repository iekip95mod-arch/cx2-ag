# Frozen callback lifecycle repair

This report supersedes REPORT.md for the current copied implementation. No original source, active binary, image or physical device changed in this batch. No emulator session was launched. All builds and standalone tests completed.

## Reproductions

The reviewer lifecycle reproducer returned exit 1 with stale flagged PC accepted=1, first_calls=0, second_calls=1 and CPU_changed=1. Its callback loader is the prior frozen source. The log is reviewer-lifecycle-before.log.

The executable ownership suite initially failed seven checks. A separate queued-owner case then failed two checks before adding its refusal. Logs are ownership-before.log and queued-before.log.

The standalone actual CPU-loop harness failed three checks before the repair. CPU dispatch erased a callback installed reentrantly at the same return site, executed the stale NOP instead of the successor's saved-LR push, and lost the successor callback. The log is cpu-lifecycle-before2.log. This executes the actual copied CPU and loader objects, not a dispatch model.

## Repair

The loader has one active owner. Direct overlapping loads and new deferred requests are refused before saved CPU state or guest memory changes. An existing queued request also prevents a direct load from becoming a second owner. Deferred polling cannot enter while an owner runs. Existing replacement of a queued request remains supported.

A return requires active ownership and the original PC, expected SP, processor mode and instruction state. The owner records the return instruction's physical address so retirement clears its flag independently of the current virtual mapping. Retirement occurs before invoking the saved callback. Null callbacks also retire ownership. Failed setup never arms ownership. CPU reset retires ownership and queued work.

After accepting a callback, the CPU loop starts its next iteration. This preserves a newly armed flag and refetches instructions if the callback launches its successor.

The changed production copies are armsnippets_loader.c, armsnippets.h and cpu.cpp. Exact diffs against the unchanged original source are loader-lifecycle.patch, header-lifecycle.patch and cpu-lifecycle.patch. Their relative paths describe these copies, so do not apply them blindly to another root. The internal callback still returns bool, and the new armloader_reset function is called by CPU reset. The public snippet and deferred request signatures are unchanged.

## Validation

Final normal and ASan plus UBSan runs pass:

- callback_test.c: 27 checks each.
- ownership_test.c: 29 checks each, including active and queued overlap, wrong PC, mode and instruction state, null callbacks, duplicate returns, failed parameter writes, invalid return addresses, cancellation during active execution and reset retirement.
- cpu_lifecycle_test.cpp: 10 checks each through actual CPU, memory, loader and ARM interpreter code.

The CPU harness executes the existing ARM saved-LR push and LDM return instructions. It observes the expected SP and the callback matching that return. It bypasses the OS SVC. Therefore it qualifies the instruction and dispatch contract but does not establish a working OS injection point, a live Ndl/Giac launch, raw-blob return under the OS, or handheld performance. The original callback unit suite still uses memory stubs and simulated ARM register states for its additional parameter and blob cases.

Commands ran in this experiment directory:

~~~sh
clang -std=c11 -g -O1 -I../core /private/tmp/cx2-matrix-callback-review-mq84vl/callback-final/lifecycle_test.c -o reviewer-lifecycle-before
./reviewer-lifecycle-before
clang -std=c11 -g -O1 -I../core ownership_test.c -o ownership-test-after
./ownership-test-after
clang -std=c11 -g -O1 -fsanitize=address,undefined -I../core ownership_test.c -o ownership-test-san
./ownership-test-san
clang -std=c11 -g -O1 -I../core callback_test.c -o callback-test-final
./callback-test-final
clang -std=c11 -g -O1 -fsanitize=address,undefined -I../core callback_test.c -o callback-test-final-san
./callback-test-final-san
make -j4 cpu-lifecycle-test experiment
./cpu-lifecycle-test
make -j4 OBJDIR=san-objects SUFFIX=-san CFLAGS='-std=c11 -g -O1 -fsanitize=address,undefined -I.. -I../core -DSUPPORT_LINUX' CXXFLAGS='-std=c++11 -g -O1 -fsanitize=address,undefined -I.. -I../core -DSUPPORT_LINUX' cpu-lifecycle-test-san
./cpu-lifecycle-test-san
make -n cpu-lifecycle-test experiment
~~~

The final dry run reports the CPU test up to date and only the phony experiment relink, with no stale compilation inputs. The standalone CPU test uses synthetic RAM and no OS or flash image. Its link includes the actual dependency objects and a copy of headless main compiled with a renamed entry point. It never calls that entry point. Sanitizer objects are separate from the normal objects and all linked C and C++ translation units are instrumented.

## Frozen identities

lifecycle-hashes.txt records all copied source, test, Makefile, patch and binary SHA256 values, followed by unchanged original source and active binary hashes. The current isolated headless binary SHA256 is 793c6b7de05fdece7b2df2c19e51df2d2f670c8f04b74e8ceb20efc81591ff61. Original Firebird HEAD remains b10f3b51a9ab8e27d2703444b5e6bd278d4c5e6e with an empty index and its prior unrelated dirty work preserved. Earlier logs, snapshots and evidence files were retained.
