#include <unistd.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

int
main(int argc, char **argv)
{
	pid_t pid;
	int p[2], ppgrp, opgrp, npgrp;
	char c = '?';

	if (pipe(p) != 0)
		perror("pipe() error");
	else {
		ppgrp = getpgrp();
		printf("parent's pid %d; process group id %d\n", getpid(), ppgrp);
		if ((pid = fork()) == 0) {
			opgrp = getpgrp();
			printf("child's pid %d; process group id %d\n", getpid(), opgrp);
			write(p[1], &c, 1);
			setsid();
			npgrp = getpgrp();
			if(opgrp == npgrp){
				printf("FAIL: setsid did not changed child's process group id\n");
				exit(EXIT_FAILURE);
			}
			printf("child's process group id is now %d\n", npgrp);
			sleep(5);
			exit(EXIT_SUCCESS);
		} else {
			read(p[0], &c, 1);
			sleep(3);
			if(ppgrp != getpgrp()){
				printf("FAIL: parent's process group id changed from %d to %d\n", ppgrp, getpgrp());
				exit(EXIT_FAILURE);
			}

#ifndef WITH_SIGCHLD
			npgrp = getsid(pid);
			if(npgrp < 0){
				printf("FAIL: parent's getsid(%d) failed with errno %d\n", pid, errno);
				exit(EXIT_FAILURE);
			}
			if(npgrp == ppgrp){
				printf("FAIL: parent's getsid(%d) returned old process group id that should be changed\n", pid);
				exit(EXIT_FAILURE);
			}
#endif
		}
	}
}
