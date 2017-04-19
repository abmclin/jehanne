/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */

#include <u.h>
#include <libc.h>

#define	NEXIT	33

typedef struct Onex Onex;
struct Onex{
	void	(*f)(void);
	int	pid;
};

static Lock onexlock;
Onex onex[NEXIT];

int
jehanne_atexit(void (*f)(void))
{
	int i;

	jehanne_lock(&onexlock);
	for(i=0; i<NEXIT; i++)
		if(onex[i].f == 0) {
			onex[i].pid = jehanne_getpid();
			onex[i].f = f;
			jehanne_unlock(&onexlock);
			return 1;
		}
	jehanne_unlock(&onexlock);
	return 0;
}

void
jehanne_atexitdont(void (*f)(void))
{
	int i, pid;

	pid = jehanne_getpid();
	for(i=0; i<NEXIT; i++)
		if(onex[i].f == f && onex[i].pid == pid)
			onex[i].f = 0;
}

#pragma profile off

void
jehanne_exits(const char *s)
{
	void _fini(void);
	int i, pid;
	void (*f)(void);

	pid = jehanne_getpid();
	for(i = NEXIT-1; i >= 0; i--)
		if((f = onex[i].f) && pid == onex[i].pid) {
			onex[i].f = 0;
			(*f)();
		}
	_fini();
	_exits(s);
	__builtin_unreachable();
}

#pragma profile on
