/*	$NetBSD$	*/

/*-
 * Copyright (c) 2018 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Taylor R. Campbell.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");

#include <sys/param.h>
#include <sys/types.h>
#include <sys/bitops.h>
#include <sys/condvar.h>
#include <sys/mutex.h>
#include <sys/systm.h>

#include <linux/bitops.h>
#include <linux/sched.h>
#include <linux/wait_bit.h>

static struct {
	struct waitbitentry {
		kmutex_t	lock;
		kcondvar_t	cv;
	}		ent;
	char		pad[CACHE_LINE_SIZE - sizeof(struct waitbitentry)];
} waitbittab[PAGE_SIZE/CACHE_LINE_SIZE] __cacheline_aligned;
CTASSERT(sizeof(waitbittab) == PAGE_SIZE);
CTASSERT(sizeof(waitbittab[0]) == CACHE_LINE_SIZE);

int
linux_wait_bit_init(void)
{
	size_t i;

	for (i = 0; i < __arraycount(waitbittab); i++) {
		mutex_init(&waitbittab[i].ent.lock, MUTEX_DEFAULT, IPL_VM);
		cv_init(&waitbittab[i].ent.cv, "waitbit");
	}

	return 0;
}

void
linux_wait_bit_fini(void)
{
	size_t i;

	for (i = 0; i < __arraycount(waitbittab); i++) {
		cv_destroy(&waitbittab[i].ent.cv);
		mutex_destroy(&waitbittab[i].ent.lock);
	}
}

static inline size_t
wait_bit_hash(const volatile unsigned long *bitmap, unsigned bit)
{
	/* Try to avoid cache line collisions.  */
	const volatile unsigned long *word = bitmap + bit/(NBBY*sizeof(*word));

	return ((uintptr_t)word >> ilog2(CACHE_LINE_SIZE)) %
	    __arraycount(waitbittab);
}

static struct waitbitentry *
wait_bit_enter(const volatile unsigned long *bitmap, unsigned bit)
{
	struct waitbitentry *wbe = &waitbittab[wait_bit_hash(bitmap, bit)].ent;

	mutex_enter(&wbe->lock);

	return wbe;
}

static void
wait_bit_exit(struct waitbitentry *wbe)
{

	mutex_exit(&wbe->lock);
}

void
wake_up_bit(const volatile unsigned long *bitmap, unsigned bit)
{
	struct waitbitentry *wbe;

	wbe = wait_bit_enter(bitmap, bit);
	cv_broadcast(&wbe->cv);
	wait_bit_exit(wbe);
}

int
wait_on_bit(const volatile unsigned long *bitmap, unsigned bit, int flags)
{
	struct waitbitentry *wbe;
	int error, ret;

	if (test_bit(bit, bitmap))
		return 0;

	wbe = wait_bit_enter(bitmap, bit);

	while (!test_bit(bit, bitmap)) {
		if (flags & TASK_UNINTERRUPTIBLE) {
			cv_wait(&wbe->cv, &wbe->lock);
		} else {
			error = cv_wait_sig(&wbe->cv, &wbe->lock);
			if (error == EINTR || error == ERESTART)
				ret = -ERESTARTSYS;
			else if (error != 0)
				ret = -error;
			if (ret)
				goto out;
		}
	}

	/* Bit is set.  Return zero on success.   */
	ret = 0;

out:	wait_bit_exit(wbe);
	return ret;
}

int
wait_on_bit_timeout(const volatile unsigned long *bitmap, unsigned bit,
    int flags, unsigned long timeout)
{
	struct waitbitentry *wbe;
	int error, ret;

	if (test_bit(bit, bitmap))
		return timeout;

	wbe = wait_bit_enter(bitmap, bit);

	while (!test_bit(bit, bitmap)) {
		unsigned starttime, endtime;

		starttime = hardclock_ticks;
		if (flags & TASK_UNINTERRUPTIBLE) {
			error = cv_timedwait(&wbe->cv, &wbe->lock,
			    MIN(INT_MAX, timeout));
		} else {
			error = cv_timedwait_sig(&wbe->cv, &wbe->lock,
			    MIN(INT_MAX, timeout));
		}
		endtime = hardclock_ticks;

		/* If we timed out, return zero time left.  */
		if (error == EWOULDBLOCK || endtime - starttime < timeout) {
			ret = 0;
			goto out;
		}

		/* If we were interrupted, return -ERESTARTSYS.  */
		if (error == EINTR || error == ERESTART) {
			ret = -ERESTARTSYS;
			goto out;
		}

		/* Otherwise, debit the time spent.  */
		timeout -= (endtime - starttime);
	}
	/* Bit is set.  Return the time left.  */
	ret = timeout;

out:	wait_bit_exit(wbe);
	return ret;
}
