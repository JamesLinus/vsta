#include <stdio.h>
#include <pwd.h>

main()
{
	char buf[128];
	struct passwd *pwd;
	uid_t uid;

	for (;;) {
		printf("UID #: "); fflush(stdout);
		gets(buf);
		if (buf[0] == '\0') {
			exit(1);
		}
		uid = atoi(buf);
		pwd = getpwuid(uid);
		if (pwd == 0) {
			printf("UID %d not known\n", uid);
			continue;
		}
		printf(" -> %s\n", pwd->pw_name);
	}
}
