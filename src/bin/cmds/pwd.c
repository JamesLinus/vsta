/*
 * pwd.c
 *	Print current working directory
 */
#include <std.h>

main(void)
{
	char buf[1024], *p;

	p = getcwd(buf, sizeof(buf));
	if (p) {
		printf("%s\n", p);
		return(0);
	} else {
		printf("Can't get CWD\n");
		return(1);
	}
}
