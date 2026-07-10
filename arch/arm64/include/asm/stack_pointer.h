/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_STACK_POINTER_H
#define __ASM_STACK_POINTER_H

/*
 * how to get the current stack pointer from C
 */
static __always_inline unsigned long current_stack_pointer(void)
{
	unsigned long sp;

	asm volatile("mov %0, sp" : "=r" (sp));
	return sp;
}

#endif /* __ASM_STACK_POINTER_H */
