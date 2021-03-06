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

char*
jehanne_strrchr(const char *s, int c)
{
	const char *r;

	if(c == 0)
		return jehanne_strchr(s, 0);
	r = 0;
	while(s = jehanne_strchr(s, c))
		r = s++;
	return (char *)r;
}
