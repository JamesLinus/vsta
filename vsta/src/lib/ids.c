/*
 * ids.c
 *	Routines for mapping using the /vsta/etc/ids file
 *
 * This code assumes that tab-indented lines represent the
 * position under a parent.  I don't know if this is too
 * risky or not; time will tell.
 */
#include <sys/perm.h>
#include <lib/llist.h>
#include <stdio.h>
#include <std.h>
#include <string.h>

#define IDS "/vsta/etc/ids"

/*
 * Nodes in our tree
 */
struct id {
	uchar i_id;		/* ID # for this node */
	char *i_name;		/* Corresponding name */
	struct id *i_up;	/* Our parent */
	struct llist		/* Nodes below this one */
		i_children;
};

/*
 * Root
 */
static struct id root;		/* Root of all nodes */
static int tree_read = 0;	/*  ...table is in core */

/*
 * id_init()
 *	Initialize an id node
 */
static void
id_init(struct id *i)
{
	bzero(i, sizeof(struct id));
	ll_init(&i->i_children);
}

/*
 * getent()
 *	Read next entry, parse it into fields
 *
 * Returns 0 on successful read, 1 on EOF or error.
 */
static
getent(FILE *fp, char *name, ulong *num, uint *depthp)
{
	char *p, *q, buf[128];

	/*
	 * Get line
	 */
	if (fgets(buf, sizeof(buf), fp) == 0) {
		return(0);
	}

	/*
	 * Count leading tabs
	 */
	*depthp = 0;
	for (p = buf; *p && (*p == '\t'); ++p) {
		*depthp += 1;
	}

	/*
	 * Extract name
	 */
	q = name;
	while (*p && (*p != ':')) {
		*q++ = *p++;
	}
	*q = '\0';
	if (*p == '\0') {	/* Corrupt field */
		return(0);
	}
	++p;

	/*
	 * Extract UID
	 */
	*num = atoi(p);
	return(1);
}

/*
 * read_tree2()
 *	Read IDS and build in-core representation
 */
static void
read_tree2(FILE *fp)
{
	struct id *i, *lastn, *curn;
	uint depth, x;
	ulong id;
	char name[NAMESZ];

	/*
	 * Initialize root of tree, open IDS file
	 */
	id_init(&root);
	curn = &root;
	lastn = 0;
	depth = 0;

	/*
	 * Get lines and build the tree
	 */
	while (getent(fp, name, &id, &x)) {
		/*
		 * Allocate node, fill in fields
		 */
		i = malloc(sizeof(struct id));
		if (i == 0) {
			return;
		}
		id_init(i);
		i->i_name = strdup(name);
		if (i->i_name == 0) {
			free(i);
			return;
		}
		i->i_id = id;

		/*
		 * Inserting below last node created.  Move down
		 * to it here.
		 */
		if (x > depth) {
			if (lastn == 0) {
				return;
			}
			depth++;
			x = depth;
			curn = lastn;

		/*
		 * Inserting up some levels.  Move curn up.
		 */
		} else if (x < depth) {
			while (depth > x) {
				curn = curn->i_up;
				depth -= 1;
			}
		}

		/*
		 * Insert node at current position
		 */
		if (ll_insert(&curn->i_children, i) == 0) {
			return;
		}
		i->i_up = curn;

		/*
		 * Record last node to be created
		 */
		lastn = i;
	}
}

/*
 * read_tree()
 *	Open IDS, then call our routine to do the actual reading
 */
static void
read_tree(void)
{
	FILE *fp;

	if ((fp = fopen(IDS, "r")) == 0) {
		return;
	}
	read_tree2(fp);
	fclose(fp);
}

/*
 * look_id()
 *	Convert next digit in an ID
 *
 * Returns 2 on any corruption or what-not.  This is the
 * ID for something not very trusted; hopefully this case
 * won't even need to be exercised.
 */
look_id(char *field, uchar *leading, int nlead)
{
	struct id *i, *i2;
	struct llist *l;
	int x;

	/*
	 * Read tree first time only
	 */
	if (!tree_read) {
		read_tree();
		tree_read = 1;
	}

	/*
	 * Walk down tree for given leading ID prefix
	 */
	i = &root;
	for (x = 0; x < nlead; ++x) {
		int found;

		/*
		 * Walk list at this level looking for the
		 * current position value.
		 */
		l = LL_NEXT(&i->i_children);
		found = 0;
		while (l != &i->i_children) {
			i2 = l->l_data;
			if (i2->i_id == leading[x]) {
				i = i2;
				found = 1;
				break;
			}
			l = LL_NEXT(l);
		}

		/*
		 * Not found--can't translate ID
		 */
		if (!found) {
			return(2);
		}
	}

	/*
	 * "i" now points at node under which we search for the
	 * current name.
	 */
	l = LL_NEXT(&i->i_children);
	while (l != &i->i_children) {
		i2 = l->l_data;
		if (!strcmp(field, i2->i_name)) {
			return(i2->i_id);
		}
		l = LL_NEXT(l);
	}

	/*
	 * Not found under this node
	 */
	return(2);
}

/*
 * cvt_id()
 *	Convert numeric dotted format into symbolic
 */
char *
cvt_id(uchar *ids, int nid)
{
	char *p = 0;
	int x, len = 0, first = 1;
	struct id *i;

	/*
	 * Read tree first time only
	 */
	if (!tree_read) {
		read_tree();
		tree_read = 1;
	}

	/*
	 * Convert each position
	 */
	i = &root;
	for (x = 0; x < nid; ++x) {
		char buf[64];

		/*
		 * Set default value--just numeric representation
		 */
		sprintf(buf, "%d", ids[x]);

		/*
		 * Scan tree if we're still within it
		 */
		if (i) {
			struct llist *l;
			struct id *i2;

			l = LL_NEXT(&i->i_children);
			while (l != &i->i_children) {
				i2 = l->l_data;
				if (i2->i_id == ids[x]) {
					i = i2;
					strcpy(buf, i->i_name);
					break;
				}
				l = LL_NEXT(l);
			}

			/*
			 * If we didn't find it, we have wandered into
			 * a name space not described by our ID file.
			 * We use numeric representation from here.
			 */
			if (l == &i->i_children) {
				i = 0;
			}
		}

		/*
		 * Grow result string and add this entry
		 */
		len = (!first ? (len + strlen(buf) + 1) : strlen(buf));
		p = realloc(p, len+1);
		if (p == 0) {
			return("2.2");
		}
		if (first) {
			*p = '\0';
		} else {
			strcat(p, ".");
		}
		strcat(p, buf);
		first = 0;
	}
	return(p);
}
