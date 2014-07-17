#include <stdio.h>

struct base {
	int bi;
};

struct c {
	struct base bs;
	int i[2];
	int j;
};

void read_s (struct c a)
{
	printf("a.bs.bi: %d, a.i[0]: %d, a.i[1]: %d, a.j: %d\n", a.bs.bi, a.i[0], a.i[1], a.j);
}

struct c return_s (int i)
{
	struct c cs;
	cs.j = i;
	return cs;
}
