#ifndef _CTYPE_H
#define _CTYPE_H
/*
 * ctype.h
 *	Categorize character types
 *
 * ctype.c holds the actual tables; these just use bits from that
 * initialized table.  ctype.c also provides functions for each
 * macro; this saves code space at the expense of speed.
 */

extern const unsigned char *__get_ctab(void);

/*
 * Bits in each slot
 */
#define _CT_DIG (1)
#define _CT_HEXDIG (2)
#define _CT_LOWER (4)
#define _CT_UPPER (8)
#define _CT_WHITE (16)
#define _CT_CTRL (32)

/*
 * Macros
 */
#ifndef _CT_NOMACS

/*
 * The table used.  Note that __ctab is queried via __get_ctab()
 * during C startup, and placed here.
 */
extern const unsigned char *__ctab;

#define __bits(c, b) (__ctab[(unsigned)(c) & 0x7F] & (b))
#define isupper(c) __bits((c), _CT_UPPER)
#define islower(c) __bits((c), _CT_LOWER)
#define isalpha(c) __bits((c), _CT_UPPER|_CT_LOWER)
#define isalnum(c) __bits((c), _CT_UPPER|_CT_LOWER|_CT_DIG)
#define isdigit(c) __bits((c), _CT_DIG)
#define isxdigit(c) __bits((c), _CT_HEXDIG)
#define isspace(c) __bits((c), _CT_WHITE)
#define iscntrl(c) __bits((c), _CT_CTRL)
#define ispunct(c) (!iscntrl(c) && !isalnum(c))
#define isprint(c) (!iscntrl(c))
#define isascii(c) ((unsigned)(c) <= 0x7F)
#define tolower(c) (isupper(c) ? ((c) - 'A' + 'a') : c)
#define toupper(c) (islower(c) ? ((c) - 'a' + 'A') : c)
#define toascii(c) ((c) & 0x7F)
#define isblank(c) (((c) == ' ') || ((c) == '\t'))
#define isgraph(c) (((c) > ' ') && ((c) < '\177'))
#endif /* !_CT_NOMACS */

#endif /* _CTYPE_H */
