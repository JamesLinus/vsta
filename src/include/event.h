/*
 * Wrapper around VSTa's event mechanism
 */
#ifndef _EVENT_H
#define _EVENT_H
#include <sys/types.h>

/*
 * Set handler for event. When event == NULL, all events are sent to the
 * handler, unless another handler for the event exists (other than the
 * DEF_NOTIFY_HANDLER). 
 */
extern int handle_event(const char *event, voidfun handler);

#endif /* _EVENT_H */
