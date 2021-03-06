/*
 * Copyright (c) 2013 Robert N. M. Watson
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract (FA8750-10-C-0237)
 * ("CTSRD"), as part of the DARPA CRASH research programme.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _SYS_TESLA_KERNEL_H_
#define	_SYS_TESLA_KERNEL_H_

#ifndef TESLA
/* Cause the include of tesla.h to have no effect */
#define	TESLA_H
#define	__tesla_inline_assertion(...)
#endif

#include <tesla-macros.h>

/*
 * FreeBSD kernel-specific TESLA macros.
 */

#define	incallstack(fn)	\
	TSEQUENCE(called(fn), TESLA_ASSERTION_SITE, returnfrom(fn))

#if 0
/* XXXRW: This doesn't yet work. */
struct timespec	__tesla_any_timespec();
#endif

/*
 * Convenient assertion wrappers for various scopes.
 */

#if defined(__amd64__)
struct trapframe;
extern void amd64_syscall(struct trapframe *, int);
#define	TESLA_SYSCALL(x)	TESLA_WITHIN(amd64_syscall, x)
#else
extern void syscall(void);
#define	TESLA_SYSCALL(x)	TESLA_WITHIN(syscall, x)
#endif
#define	TESLA_SYSCALL_PREVIOUSLY(x)	TESLA_SYSCALL(previously(x))
#define	TESLA_SYSCALL_EVENTUALLY(x)	TESLA_SYSCALL(eventually(x))

/*
 * XXXJA: figure out which of call()/called() to keep
 */
#define	called(...)		__tesla_call(((void) __VA_ARGS__, TIGNORE))

/*
 * XXXRW: Not all architectures have a trap_pfault() function.  Can't use
 * vm_fault() as it is used in non-trap contexts -- e.g., PMAP initialisation.
 */
#if 0
extern void trap_pfault(struct trapframe *, int, vm_offset_t);
#define	TESLA_PAGE_FAULT(x)	TESLA_WITHIN(trap_pfault, x)
#else
#define	TESLA_PAGE_FAULT(x)
#endif

#endif /* _SYS_TESLA_KERNEL_H_ */
