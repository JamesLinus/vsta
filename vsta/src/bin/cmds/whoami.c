/*
 * whoami.c
 *	Print out our username
 */
#include <pwd.h>

int
main(int argc, char **argv)
{
	struct passwd *pw;
	uid_t uid;

	uid = getuid();
	pw = getpwuid(uid);
	if (pw) {
		printf("%s\n", pw->pw_name);
	} else {
		printf("%ld\n", uid);
	}
	return(0);
}
