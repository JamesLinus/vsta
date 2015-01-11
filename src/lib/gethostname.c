/*
 * gethostname.c
 *	Look up hostname from our net initialization file
 */
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <paths.h>

int
gethostname(char *hostp, int hostpsize)
{
	char *p, *nm = NULL, buf[512];
	FILE *fp;

	if (fp = fopen(_PATH_NET, "r")) {
		while (fgets(buf, sizeof(buf), fp)) {
			if (!strncmp(buf, "host ", 5)) {
				nm = buf + 5;
				p = strchr(nm, '\n');
				if (p) {
					*p = '\0';
				}
				break;
			}
		}
	}
	fclose(fp);
	if (!nm) {
		nm = "Unknown";
	}
	strncpy(hostp, nm, hostpsize);
	return(0);
}
