// hello, world
#include <inc/lib.h>

void
umain(int argc, char **argv)
{
	int i;
	for (i = 1; i <= 3; ++i) {
		int pid = priority_fork(i);
		if (pid == 0) {
			cprintf("child %x\n with priority %x\n", i, i);
			int j;
			for (j = 0; j < 3; ++j) {
				cprintf("child %x\n with priority %x\n", i, i);
				sys_yield();
			}
			break;
		}
	}
}
