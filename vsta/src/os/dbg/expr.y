%left	'*' '/'
%left	'+' '-'
%right	MINUS
%right LE GE EQ NE
%left '&'
%token ID INT
%{
#ifdef YYDEBUG
int yydebug = 1;
#endif
%}

%%

expr	:	expr '+' expr
			{$$ = $1 + $3;}
	|	expr '-' expr
			{$$ = $1 - $3;}
	|	expr '/' expr
			{$$ = $1 / $3;}
	|	expr '*' expr
			{$$ = $1 * $3;}
	|	expr LE expr
			{$$ = $1 <= $3;}
	|	expr GE expr
			{$$ = $1 >= $3;}
	|	expr EQ expr
			{$$ = $1 == $3;}
	|	expr NE expr
			{$$ = $1 != $3;}
	|	expr '&' expr
			{$$ = $1 & $3;}
	|	'~' expr %prec MINUS
			{$$ = ~$2;}
	|	'-' expr %prec MINUS
			{$$ = 0-$2;}
	|	'(' expr ')'
			{$$ = $2;}
	|	ID
	|	INT
	;
%%
