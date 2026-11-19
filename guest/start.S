/* SPDX-License-Identifier: MPL-2.0 */
/* Mode B guest entry and QNX kernel call stubs (see qnx_kernel.h). */
	.syntax unified
	.arm
	.text

/* The kernel starts the process with argc at [sp], then argv, a NULL, envp
 * and a NULL. */
	.global _start
	.type _start, %function
_start:
	mov	fp, #0
	mov	lr, #0
	ldr	r0, [sp]
	add	r1, sp, #4
	add	r2, r1, r0, lsl #2
	add	r2, r2, #4
	bic	sp, sp, #7
	bl	nbt_guest_start
	udf	#0
	.size _start, . - _start

/* Up to six arguments: the fifth and sixth come from the caller's stack. */
	.macro KCALL name, number
	.global \name
	.type \name, %function
	.balign 4
\name:
	push	{r4, r5, lr}
	ldr	r4, [sp, #12]
	ldr	r5, [sp, #16]
	mov	ip, #\number
	svc	#0
	pop	{r4, r5, pc}		@ success
	rsb	r0, r0, #0		@ failure: r0 holds the errno value
	pop	{r4, r5, pc}
	.size \name, . - \name
	.endm

	KCALL qk_cpupage_get, 7
	KCALL qk_msg_sendv, 11
	KCALL qk_msg_sendvnc, 12
	KCALL qk_msg_send_pulse, 20
	KCALL qk_msg_receive_pulsev, 24
	KCALL qk_signal_kill, 26
	KCALL qk_channel_create, 35
	KCALL qk_channel_destroy, 36
	KCALL qk_connect_attach, 39
	KCALL qk_connect_detach, 40
	KCALL qk_thread_create, 46
	KCALL qk_thread_destroy, 47
	KCALL qk_thread_detach, 49
	KCALL qk_thread_join, 50
	KCALL qk_clock_time, 65
	KCALL qk_timer_timeout, 75
	KCALL qk_sched_yield, 90

	.section .note.GNU-stack, "", %progbits
