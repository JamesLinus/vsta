#ifndef _ASSERT_H
#define _ASSERT_H
/*
 * assert.h
 *	Both debug and everyday assertion interfaces
 */
extern void assfail(const char *, const char *, int);

#define ASSERT(condition, message) \
	if (!(condition)) { assfail(message, __FILE__, __LINE__); }
#ifdef DEBUG
#define ASSERT_DEBUG(c, m) ASSERT(c, m)
#else
#define ASSERT_DEBUG(c, m)
#endif

#endif /* _ASSERT_H */
