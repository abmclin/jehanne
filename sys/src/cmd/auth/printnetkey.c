#include <u.h>
#include <lib9.h>
#include <bio.h>
#include <authsrv.h>
#include "authcmdlib.h"

void	usage(void);

void
main(int argc, char *argv[])
{
	char *key;
	char *u;
	char keybuf[DESKEYLEN];

	argv0 = "printnetkey";
	fmtinstall('K', deskeyfmt);

	ARGBEGIN{
	default:
		usage();
	}ARGEND
	if(argc != 1)
		usage();

	u = argv[0];
	if(memchr(u, '\0', ANAMELEN) == 0)
		error("bad user name");
	key = finddeskey(NETKEYDB, u, keybuf);
	if(!key)
		error("%s has no netkey\n", u);
	print("user %s: net key %K\n", u, key);
	exits(0);
}

void
usage(void)
{
	fprint(2, "usage: printnetkey user\n");
	exits("usage");
}
