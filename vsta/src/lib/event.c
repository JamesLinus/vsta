/*
 * Wrapper around VSTa's event mechanism
 *
 * This wrapper makes it possible to install multiple event handlers, at most
 * one per event string.
 *
 * NOT MT-safe.
 */
#include <sys/fs.h>
#include <sys/syscall.h>
#include <event.h>
#include <std.h>
#include <stdio.h>

extern int __seterr(char *);
static void ev_handler(const char *event);

/*
 * # of hash buckets
 */
#define NBUCKETS (8)

/*
 * Simple hash table for the event handlers
 */
static struct event {
	char *ev_string;		/* String to handle */
	voidfun ev_handler;	/* Handler */
	struct event *ev_next;		/* Hash collision overflow */
} *ev_hash[NBUCKETS];

/*
 * Global default handler, when the handler is 0, the event handler aborts
 * the process.
 */
static voidfun def_handler = 0;

/*
 * string_hash()
 *	Generate a hash value for the given string
 */
static uint
string_hash(const char *string)
{
	unsigned char c;
	uint result = 0;

	while ((c = *string++)) {
		result += c;
	}
	return(result % NBUCKETS);
}

/*
 * init_events()
 *	Initialize event tables
 */
static void
init_events(void)
{
	static int initialised = 0;

	/*
	 * Initialize one time only
	 */
	if (initialised) {	
		return;
	}
	initialised = 1;

	/*
	 * Connect our dispatch routine
	 */
	notify_handler(ev_handler);
}

/*
 * handle_event()
 *	(Dis)connect handlers for given event strings
 */
int
handle_event(const char *event, voidfun handler)
{
	uint the_hash;
	struct event *current, *ev_new;

	/*
	 * One-time init
	 */
	init_events();

	/*
	 * Restore default handler for an event
	 */
	if (handler == 0) {
		struct event **prevp;

		/*
		 * Restore the default handler for events without a 
		 * custom handler.
		 */
		if (event == 0) {
			def_handler = 0;
			return(0);
		}

		/*
		 * No handler set, thus the current handler is the 
		 * default one and we are ready.
		 */
		the_hash = string_hash(event);
		current = ev_hash[the_hash];
		if (current == 0) {
			return(0);
		}

		/*
		 * Find handler entry
		 */
		prevp = &ev_hash[the_hash];
		while (current) {
			/*
			 * Here's our event; patch it out of the list
			 * and free it.
			 */
			if (strcmp(current->ev_string, event) == 0) {
				*prevp = current->ev_next;
				free(current->ev_string);
				free(current);
				break;
			}
			prevp = &current->ev_next;
			current = *prevp;
		}
		return(0);
	}

	/*
	 * Install handler
	 */

	/*
	 * Global default handler
	 */
	if (event == 0) {
		def_handler = handler;
		return(0);
	}

	/*
	 * If already in the table, replace handler
	 */
	the_hash = string_hash(event);
	for (current = ev_hash[the_hash]; current;
			current = current->ev_next) {
		if (strcmp(current->ev_string, event) == 0) {
			current->ev_handler = handler;
			return(0);
		}
	}
 
	/*
	 * Add the new handler as the first entry on the chain.
	 */
	ev_new = malloc(sizeof(struct event));
	if (ev_new == 0) {
		return(__seterr(ENOMEM));
	}
	ev_new->ev_string = strdup(event);
	if (ev_new->ev_string == 0) {
		free(ev_new);
		return(__seterr(ENOMEM));
	} 
	ev_new->ev_handler = handler;
	ev_new->ev_next = ev_hash[the_hash];
	ev_hash[the_hash] = ev_new;
	return(0);
}

/*
 * ev_handler()
 *	The event handler
 *
 * Look for a custom event handler for the event.
 * If no such handler exists, check if a global default handler exists,
 * if so call it, otherwise kill the current thread.
 */
static void
ev_handler(const char *event)
{
	struct event *cur;

	for (cur = ev_hash[string_hash(event)]; cur; cur = cur->ev_next) {
		if (strcmp(cur->ev_string, event) == 0) {
			cur->ev_handler(event);
			return;
		} 
	}

	/*
	 * No handler, call the global default, if one exists
	 */
	if (def_handler) {
		def_handler(event);
		return;
	}

	/*
	 * Not even a global default, kill the process by
	 * removing our handler and re-sending the event.
	 */
#define msg(str) write(2, str, strlen(str))
	msg("Event not caught: ");
	msg(event);
	msg("\n");
	notify_handler(0);
	notify(0, 0, event);
}
