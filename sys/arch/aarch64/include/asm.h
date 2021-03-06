/* $NetBSD: asm.h,v 1.6 2020/01/19 16:12:56 skrll Exp $ */

#ifndef _AARCH64_ASM_H_
#define _AARCH64_ASM_H_

#include <arm/asm.h>

#ifdef __aarch64__

#ifdef __ASSEMBLER__
.macro	adrl 	reg, addr
	adrp	\reg, \addr
	add	\reg, \reg, #:lo12:\addr
.endm
#endif

#define	fp	x29
#define	lr	x30

/*
 * Add a speculation barrier after the 'eret'.
 * Some aarch64 cpus speculatively execute instructions after 'eret',
 * and this potentiates side-channel attacks.
 */
#define	ERET	\
	eret; dsb sy; isb

#endif

#endif /* !_AARCH64_ASM_H_ */
