#include <ctype.h>

main()
{
	unsigned int x;

	for (x = 0; x < 128; ++x) {
		if (isspace(x)) printf(" %02x", x);
	}
	return(0);
}
