/*
 * doprnt.c
 *	Low-level machinery used by all flavors of printf()
 */
#define NUMBUF (32)

/*
 * num()
 *	Convert number to string
 *
 * Returns length of resulting string
 */
static
num(char *buf, unsigned int x, unsigned int base)
{
	char *p = buf+NUMBUF;
	unsigned int c, len = 1;

	*--p = '\0';
	do {
		c = (x % base);
		if (c < 10) {
			*--p = '0'+c;
		} else {
			*--p = 'a'+(c-10);
		}
		len += 1;
		x /= base;
	} while (x != 0);
	bcopy(p, buf, len);
	return(len-1);
}

/*
 * baseof()
 *	Given character, return base value
 */
static
baseof(char c)
{
	switch (c) {
	case 'd':
	case 'D':
		return(10);
	case 'x':
	case 'X':
		return(16);
	case 'o':
	case 'O':
		return(8);
	default:
		return(10);
	}
}

/*
 * __doprnt()
 *	Do printf()-style printing
 */
void
__doprnt(char *buf, char *fmt, int *args)
{
	char *p = fmt, c;
	char numbuf[NUMBUF];
	int adj, width, zero, longfmt, x;

	while (c = *p++) {
		/*
		 * Non-format; use character
		 */
		if (c != '%') {
			*buf++ = c;
			continue;
		}
		c = *p++;

		/*
		 * Leading '-'; toggle default adjustment
		 */
	 	if (c == '-') {
			adj = 1;
			c = *p++;
		} else {
			adj = 0;
		}

		/*
		 * Leading 0; zero-fill
		 */
	 	if (c == '0') {
			zero = 1;
			c = *p++;
		} else {
			zero = 0;
		}

		/*
		 * Numeric; field width
		 */
		if (isdigit(c)) {
			width = atoi(p-1);
			while (isdigit(*p))
				++p;
			c = *p++;
		} else {
			width = 0;
		}

		/*
		 * 'l': "long" format.  XXX Use this when sizeof(int)
		 * stop being sizeof(long).
		 */
		if (c == 'l') {
			longfmt = 1;
			c = *p++;
		} else {
			longfmt = 0;
		}

		/*
		 * Format
		 */
		switch (c) {
		case 'D':
		case 'X':
		case 'O':
			longfmt = 1;
			/* VVV fall into VVV */

		case 'd':
		case 'x':
		case 'o':
			x = num(numbuf, *args++, baseof(c));
			if (!adj) {
				for ( ; x < width; ++x) {
					*buf++ = zero ? '0' : ' ';
				}
			}
			strcpy(buf, numbuf);
			buf += strlen(buf);
			if (adj) {
				for ( ; x < width; ++x) {
					*buf++ = ' ';
				}
			}
			break;
			num(numbuf, *args++, 16);
			strcpy(buf, numbuf);
			buf += strlen(buf);
			break;

		case 's':
			x = strlen((char *)(args[0]));
			if (!adj) {
				for ( ; x < width; ++x) {
					*buf++ = ' ';
				}
			}
			strcpy(buf, (char *)(*args++));
			buf += strlen(buf);
			if (adj) {
				for ( ; x < width; ++x) {
					*buf++ = ' ';
				}
			}
			break;

		case 'c':
			x = *args++;
			*buf++ = x;
			break;

		default:
			*buf++ = c;
			break;
		}
	}
	*buf = '\0';
}
