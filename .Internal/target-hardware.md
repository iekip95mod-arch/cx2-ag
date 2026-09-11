# Target hardware

## Latest observed device, 2026-09-08

The physical TI-Nspire CX II non-CAS reported device code 0x1d and OS 6.40.74. The loaded StepCAS rendering release also reported Giac 1.9.0 and Ndl r2022. Package hashes and visible checks are in [BUDGETS.md](../nps/benchmarks/BUDGETS.md#equation-opening-check-2026-09-08). This identifies that session and does not establish runtime persistence or release memory budgets.

## Historical inventory, 2026-09-01

The OS version, filenames and memory observations below belong to the earlier inventory. Recheck the connected device before choosing an installer or interpreting memory measurements. Idle free RAM is not peak live allocation.

Non-CAS TI-Nspire CX II running OS **6.2.0.333**.

That exact build is a first-class Ndless target. The syscall generator lists it by name:

```
ndless-src/ndless/src/tools/MakeSyscalls/mkSyscalls.php:22
  "OS_ncascx2-6.2.0.333.idc", "OS_ncascx2t-6.2.0.333.idc", "OS_cascx2-6.2.0.333.idc",
```

Here ncascx2 means non-CAS CX II, ncascx2t is the CX II-T, cascx2 is the CAS model. All three
have address tables at 6.2.0.333, so nothing about the non-CAS variant is second class.

The matching installer builds to a combined artifact covering three OS versions:

```
ndless-src/ndless/src/installer-6.2/Makefile:8
  all: $(DISTDIR)/ndless_installer_4.5.5-6.2.0-6.4.0.tns
```

That file is already on disk at ndless-r2022/ndless_installer_4.5.5-6.2.0-6.4.0.tns.

## Silicon

Verified figures only. The hackspire Hardware page predates the CX II and does not list it, so the
CPU numbers come from datamath.org and the memory figure from the CX II memory-map page.

| Property | Value | Source |
|---|---|---|
| Core | ARM926EJ-S in ASIC ET-NS2018-000 (S6M98) | http://www.datamath.org/Graphing/NSpire_CXII.htm |
| Clock | 396 MHz | same, versus 156 MHz on CX and 132 MHz on the original CX |
| SDRAM | 64 MiB at base 0x10000000, controller at 0x90120000 | https://www.hackspire.org/Memory-mapped_IO_ports_on_CX_II/ |
| On-chip ROM | 128 kB at 0x00000000, mirrored at 0xA0000000 | same |
| Model ID | 0x202 at 0x900A0000, the CX II discriminator | same |
| Screen | 320x240, 16-bit colour | PRD section 9.8 and the libndls screen types |

New on CX II relative to CX: touchpad behind an I2C controller at 0x90050000, and an Aladdin PMU
at 0x90140000.

The ARM926EJ-S has **no hardware FPU**. The whole toolchain is built soft float, see
ndl-platform.md. Any floating point in StepCAS is a library call. This is one more argument for
keeping arithmetic exact and symbolic, which the PRD wants anyway (principle 4.3).

## Memory reality for the PRD budgets

64 MiB total, but StepCAS never gets 64 MiB. Three facts shape the real budget:

1. The TI OS is resident and owns the allocator. malloc, free and realloc are OS syscalls, not
   newlib code. See ndless-src/ndless-sdk/include/syscall-list.h lines 23, 24 and 68, where
   e_malloc is 5, e_free is 6 and e_realloc is 50. The newlib build passes MALLOC_PROVIDED
   precisely so newlib does not supply its own.
2. luagiac.luax.tns is 4.1 MB of code that has to be resident during any symbolic call.
3. PRD requirement PERF-011 and WP-035 both demand the end-to-end peak with UI, parser, Giac,
   verifier and derivation all resident. Isolated microbenchmarks do not qualify a feature.

So the Milestone 0 measurement that actually matters is free heap after luagiac is loaded, on a
device, not a number derived from the 64 MiB total. Measured below: about 22 MiB in one block.

Zehn carries a RUNS_ON_32MB flag (ndless-src/ndless-sdk/include/zehn.h:64) for executables that
also work on 32 MiB devices. StepCAS targets CX II only, so it can decline that flag and assume
the 64 MiB part.

## Measured on the device, 2026-09-01

First real reading off the maintainer's calculator, via nsptool info over USB. This replaces the inferred
figures above wherever the two disagree.

```
name          TI-Nspire CX II
hw type       0x1d                      non-CAS CX II
os version    6.20.333                  libnspire's encoding of 6.2.0.333
boot1         5.0.42
boot2         6.20.7
storage       91645952 free of 96862208
ram           31235368 free of 36083840
lcd           320x240 16bpp
clock         32
extensions    file=.tns  os=.tco2
```

### The RAM number is the one that matters

**The OS reports 36083840 bytes total, about 34.4 MiB, with 31235368 free, about 29.8 MiB.** The
part carries 64 MiB (see the memory map above), so roughly half is not available to the running OS
at all.

Every PRD memory budget has to be sized against 29.8 MiB and falling, not 64 MiB. For scale,
luagiac.luax.tns is 4.1 MB of that before StepCAS has allocated anything, and PERF-011 requires the
end-to-end peak to be measured with UI, parser, Giac, verifier and derivation all resident at once.

This is a free-memory reading with the OS idle and nothing else loaded, so it is a ceiling rather
than a budget. It is also **not the malloc heap**, which is smaller again. See the next section.

## Heap measured on the device, 2026-09-01

probe/heap.c, built with the nspire MCP and run on the calculator, writes
/documents/ndless/heap-result.txt.tns. It reports two numbers because they differ under
fragmentation: the largest single malloc that succeeds, bisected to 64 KiB, and the total the
allocator hands out in 1 MiB pieces held at once. Every path fails by NULL return, so it cannot
fault, which is what makes it safe to leave in the startup folder.

| Context | Largest single block | Total in 1 MiB chunks |
|---|---|---|
| `startup=1`, during Ndless install | 4928 KiB | 4 MiB |
| `startup=0`, launched from Documents | 22208 KiB | 25 MiB |

**Plan against roughly 22 MiB in one piece, 25 MiB overall.** Two consequences worth carrying
forward:

1. The startup-folder figure is five times smaller. Anything measured at `startup=1` is measuring
   Ndless's install-time state, not the state StepCAS runs in, and must not be used as a budget.
2. Neither figure is the 29.8 MiB the OS reports as free RAM, and neither is 64 MiB. The link-level
   free-RAM reading is not the allocator's view and should not appear in a budget.

Still unmeasured: whether luagiac was resident when the 25 MiB reading was taken. The order of the
two launches was not recorded. PERF-011 wants the peak with Giac, UI, parser and derivation all
resident, so the number that qualifies a release is still ahead of us, and it is at or below 25 MiB.

### Other notes

Storage is comfortable: about 87 MiB free of 92 MiB, so installed size is not the binding
constraint. Memory is.

The OS file extension is .tco2, not the .tcc or .tco used by earlier models. That matters for the
reflash path in backup-and-recovery.md: a CX II OS image for 6.2.0.333 is a .tco2 file.

libnspire's hardware-type enum predated the CX II and reported 0x1d as unknown. Added locally as
NSPIRE_NONCASCX2. The CAS counterpart 0x0d is a guess by symmetry with the other pairs and has not
been seen on a device.
