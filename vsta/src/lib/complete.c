/*
 * complete.c
 *	Filename completion
 *
 * Coded for VSTa by Andy Valencia, November 2000
 * Unlike the main VSTa source, this module is placed in the public
 * domain.  Enjoy!
 */
#include <ctype.h>
#include <stdlib.h>
#include <dirent.h>
#include <stat.h>
#include "getline.h"

/*
 * pos_match()
 *	Tell how many positions match in a pair of strings
 */
static int
pos_match(char *s1, char *s2)
{
	char c, *init = s1;

	while ((c = *s1) == *s2) {
		if (c == '\0') {
			break;
		}
		++s1, ++s2;
	}
	return(s1 - init);
}

/*
 * gl_tab_complete()
 *	Intercept getline() tabs and try to expand into filename
 */
int
gl_tab_complete(char *buf, int offset, int *locp)
{
	/* Don't forget tab-tab to list completions */
	int buflen = strlen(buf)+1;
	char *file = malloc(buflen), *dir = malloc(buflen), *p, c, *start,
		*longest;
	int pos = *locp, len, unique = 1, uniquedir = 0;
	DIR *dp;
	struct dirent *de;

	/*
	 * Extract the filename before the cursor
	 */
	start = buf + pos;
	for (p = start; p > buf; p -= 1) {
		c = p[-1];
		if (isspace(c)) {
			break;
		}
	}
	bcopy(p, file, start - p);
	file[start-p] = '\0';

	/*
	 * Get directory path; set to "." if there is none.  Flatten
	 * file buffer down to just base filename.
	 */
	p = strrchr(file, '/');
	if (p) {
		if (p == file) {
			strcpy(dir, "/");
		} else {
			bcopy(file, dir, p-file);
			dir[p-file] = '\0';
		}
		bcopy(p+1, file, strlen(p));
	} else {
		strcpy(dir, ".");
	}

	/*
	 * Now walk the contents of that directory, seeing what'll
	 * match the best.
	 */
	dp = opendir(dir);
	if (!dp) {
		goto out;
	}
	longest = NULL;
	while ((de = readdir(dp))) {
		int match = pos_match(de->d_name, file);

		/*
		 * This doesn't match what we've typed
		 */
		if (match < strlen(file)) {
			continue;
		}

		/*
		 * If this is the first match, just record it
		 * and continue.
		 */
		if (!longest) {
			char *path;
			struct stat sb;

			longest = strdup(de->d_name);
			unique = 1;
			path = malloc(strlen(dir) + strlen(de->d_name) + 2);
			if (!strcmp(dir, "/")) {
				sprintf(path, "/%s", de->d_name);
			} else {
				sprintf(path, "%s/%s", dir, de->d_name);
			}
			if ((stat(path, &sb) >= 0) && S_ISDIR(sb.st_mode)) {
				uniquedir = 1;
			}
			free(path);
			continue;
		}

		/*
		 * Find the longest common prefix among the possible
		 * matches.  This will be the match we're building.
		 */
		unique = 0;
		match = pos_match(longest, de->d_name);
		longest[match] = '\0';
	}

	/*
	 * If nothing matched, beep and leave
	 */
	if (!longest) {
		(void)write(2, "\7", 1);
		goto out;
	}

	/*
	 * Insert the common part of all of our matches
	 */
	if (unique) {
		longest = realloc(longest, strlen(longest)+2);
		if (uniquedir) {
			strcat(longest, "/");
		} else {
			strcat(longest, " ");
		}
	}
	len = strlen(longest) - strlen(file);
	bcopy(buf + *locp, buf + *locp + len, strlen(buf + *locp) + 1);
	bcopy(longest + strlen(file), buf + *locp, len);
	*locp += len;

	/*
	 * All done... clean up
	 */
out:
	free(file);
	free(dir);
	return(pos);
}
