/*
 * obsolete.c
 *	Placeholder to abort on calls to obsolete functions
 */
static void
obsolete(void)
{
	notify(0, 0, "obsolete");
}

void
seg_create(void)
{
	obsolete();
}
