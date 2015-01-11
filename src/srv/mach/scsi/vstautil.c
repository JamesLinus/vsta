/*
 * vstautil.c - vsta specific utilities.
 */
#include <stdio.h>
#include <ctype.h>

/*
 * device_name		: basename unit type suffix ;
 * basename		: [A-Za-z_]* ;
 * unit			: [0-9]* ;
 * suffix		: [^0-9].* ;
 */
void	pdev_parse_name(name, base, base_sz, unit, type, type_sz, suffix)
char	*name, **base, **type;
int	*base_sz, *type_sz;
long	*unit, *suffix;
{
	char	*start, *p;

	start = p = name;
	while(isalpha(*p) || (*p == '_'))
		p++;
	if(base != NULL)
		*base = start;
	if(base_sz != NULL)
		*base_sz = p - start;

	start = p;
	while(isdigit(*p))
		p++;
	if(unit != NULL)
		sscanf(start, "%ld", unit);

	start = p;
	while(isalpha(*p) || (*p == '_'))
		p++;
	if(type != NULL)
		*type = start;
	if(type_sz != NULL)
		*type_sz = p - start;

	start = p;
	while(isdigit(*p))
		p++;
	if(suffix != NULL)
		sscanf(start, "%ld", suffix);
}

