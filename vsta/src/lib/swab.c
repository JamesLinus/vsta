/*
 * swab()
 *	Swap pairs of bytes in a string
 */
void
swap(const char *src, char *dest, size_t len)
{
	for ( ; len > 1; len -= 2) {
		dest[1] = src[0];
		dest[0] = src[1];
		src += 2;
		dest += 2;
	}
}
