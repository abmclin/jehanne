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
#include <auth.h>
#include <9P2000.h>
#include "../boot/boot.h"

char	*authaddr;
static void glenda(void);

void
authentication(int cpuflag)
{
	char *argv[16], **av;
	int ac;

	if(jehanne_access(factotumPath, AEXEC) < 0 || jehanne_getenv("user") != nil){
		glenda();
		return;
	}

	/* start agent */
	ac = 0;
	av = argv;
	av[ac++] = "factotum";
	if(jehanne_getenv("debugfactotum"))
		av[ac++] = "-p";
//	av[ac++] = "-d";		/* debug traces */
//	av[ac++] = "-D";		/* 9p messages */
	if(cpuflag)
		av[ac++] = "-S";
	else
		av[ac++] = "-u";
	av[ac++] = "-sfactotum";
	if(authaddr != nil){
		av[ac++] = "-a";
		av[ac++] = authaddr;
	}
	av[ac] = 0;
	switch(jehanne_fork()){
	case -1:
		fatal("starting factotum");
	case 0:
		exec(factotumPath, (const char**)av);
		fatal("execing factotum");
	default:
		break;
	}

	/* wait for agent to really be there */
	while(jehanne_access("/mnt/factotum", AEXIST) < 0)
		jehanne_sleep(250);

	if(cpuflag)
		return;
}

static void
glenda(void)
{
	int fd;
	char *s;

	s = jehanne_getenv("user");
	if(s == nil)
		s = "glenda";

	fd = open("#c/hostowner", OWRITE);
	if(fd >= 0){
		if(write(fd, s, jehanne_strlen(s)) != jehanne_strlen(s))
			jehanne_fprint(2, "setting #c/hostowner to %s: %r\n", s);
		close(fd);
	}
	jehanne_fprint(2, "Set hostowner to %s\n", s);
}
