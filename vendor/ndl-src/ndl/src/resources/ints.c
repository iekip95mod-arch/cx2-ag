/****************************************************************************
 * ndl interrupt handlers
 *
 * The contents of this file are subject to the Mozilla Public
 * License Version 1.1 (the "License"); you may not use this file
 * except in compliance with the License. You may obtain a copy of
 * the License at http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS
 * IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * rights and limitations under the License.
 *
 * The Original Code is Ndless code.
 *
 * The Initial Developer of the Original Code is Olivier ARMAND
 * <olivier.calc@gmail.com>.
 * Portions created by the Initial Developer are Copyright (C) 2010-2013
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s): 
 *                 Geoffrey ANNEHEIM <geoffrey.anneheim@gmail.com>
 ****************************************************************************/

#include <os.h>
#include <syscall-list.h>
#include "ndl.h"

extern void *ints_next_descriptor_ptr; // but static
extern void ints_swi_handler(void);

void ints_setup_handlers(void) {
	void **adr_ptr = (void**)INTS_INIT_HANDLER_ADDR;
	// The address is used by nspire_emu for OS version detection and must be restored to the OS value
	*adr_ptr = *(void**)(OS_BASE_ADDRESS + INTS_INIT_HANDLER_ADDR);
	*(adr_ptr + 2) = &ints_swi_handler;
	// also change the SWI handler in the OS code, required by the N-ext convention
	*(void**)(OS_BASE_ADDRESS + INTS_SWI_HANDLER_ADDR) = &ints_swi_handler;
 	ints_next_descriptor_ptr = &ut_next_descriptor;
}

asm(
" .arm \n"
" @ N-ext convention: a signature and a pointer to the descriptor, before the SWI handler address in the OS copy of the vectors \n"
" .long " STRINGIFY(NEXT_SIGNATURE) "\n"
"ints_next_descriptor_ptr: .long 0 \n"
"ints_swi_handler: .global ints_swi_handler  @ caution: 1) only supports calls from the svc mode (i.e. the mode used by the OS) 2) destroys the caller's mode lr \n"
" stmfd	sp!, {r0-r2, r3}  @ r3 is dummy and will be overwritten with the syscall address. Caution, update the offset below if reg list changed. \n"
" mrs	r0, spsr \n"
" tst	r0, #0b100000     @ caller in thumb state? \n"
" addne	lr, lr, #1        @ so that the final 'bx lr' of the syscall switches back to thumb state \n"
" bicne	r0, #0b100000     @ clear the caller's thumb bit. The syscall is run in 32-bit state \n"
" mov	r4, lr \n" // Callees preserve r4, which the syscall ABI declares clobbered.
" mov	r2, r0, lsr #27 \n"
" adr	lr, back_to_caller \n"
" add	lr, lr, r2, lsl #3 \n"
" msr	spsr, r0 \n"
" subs  pc, pc, #4        @  move spsr to cpsr (restore the ints mask) \n"
"@ extract the syscall number from the comment field of the swi instruction \n"
" tst	r4, #1            @ was the caller in thumb state? \n"
" ldreq	r0, [r4, #-4]     @ ARM state \n"
" biceq	r0, r0, #0xFF000000 \n"
" ldrneh r0, [r4, #-3]    @ thumb state (-2-1, because of the previous +1) \n"
" bicne	r0, r0, #0xFF00 \n"
" mov	r1, r0            @ syscall number \n"
" and	r1, #0xE00000   @ keep the 3-bit flag \n"
" bic	r0, #0xE00000   @ clear the flag \n"
" cmp	r1, #" STRINGIFY(__SYSCALLS_ISEXT) "\n"
" ldreq	r2, =sc_ext_table \n"
" beq	have_address \n"
" cmp	r1, #" STRINGIFY(__SYSCALLS_ISEMU) "\n"
" ldreq	r2, =emu_sysc_table \n"
" ldrne	r2, sc_addrs_ptr  @ OS syscalls table \n"
"have_address: \n"
" ldr	r0, [r2, r0, lsl #2] @ syscall address \n"
" cmp	r1, #" STRINGIFY(__SYSCALLS_ISVAR) "\n"
" bne	jmp_to_syscall \n"
" str	r0, [sp] \n"
" mov	r0, lr \n"

"jmp_to_syscall: \n"
" str	r0, [sp, #12] \n" // Overwrite the dummy register previously saved
" ldmfd	sp!, {r0-r2, pc} \n" // Restore the regs and jump to the syscall

"back_to_caller: \n" // Each return path encodes its NZCVQ flags without shared storage.
" .irp flags,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31 \n"
" msr	cpsr_f, #(\\flags << 27) \n"
" b	return_from_syscall \n"
" .endr \n"
"return_from_syscall: \n"
" mrs	r12, cpsr \n"
" bic	r12, r12, #0x40 \n"
" msr	cpsr_c, r12 \n"
" bx	r4 \n"

"ext_syscall: \n"
" ldr	r1, =sc_ext_table \n" // Puts sc_ext_table into the literal pool and generates a relocation (ABS32)
" ldr	r0, [r1, r0, lsl #2] \n"
" b	jmp_to_syscall \n"

"emu_syscall: \n"
" ldr	r1, =emu_sysc_table \n"
" ldr	r0, [r1, r0, lsl #2]\n"
" b	jmp_to_syscall \n"

"sc_addrs_ptr: .global sc_addrs_ptr \n" // Defined here to access it relative to pc
" .long 0 \n"
);
