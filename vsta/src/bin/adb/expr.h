#ifndef EXPR_H
#define EXPR_H
/*
 * expr.h
 *	Common definitions shared between lex and parser
 */

#define MINUS 257
#define LE 258
#define GE 259
#define EQ 260
#define NE 261
#define ID 262
#define INT 263

extern int yylex(void), yyparse(void);
extern void yyerror(char *);

extern int yylval;

#endif /* EXPR_H */
