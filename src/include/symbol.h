/*
 * symbol.h
 *	Public API for symbol table service
 */
#ifndef _SYMBOL_H
#define _SYMBOL_H

extern struct symbol *sym_alloc(void);
extern const char *sym_lookup(struct symbol *, const char *);
extern void sym_dealloc(struct symbol *);

#endif /* _SYMBOL_H */
