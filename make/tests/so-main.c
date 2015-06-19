#include <stdio.h>

struct base {
	int bi;
};

struct c {
	struct base bs;
	int i[2];
	int j;
};

struct d {
	struct base bs;
	float f;
	double d;
};

void read_s (struct c a)
{
	printf("a.bs.bi: %d, a.i[0]: %d, a.i[1]: %d, a.j: %d\n", a.bs.bi, a.i[0], a.i[1], a.j);
}
void read_s10 (struct c a,
			   struct d a1,
			   struct c a2,
			   struct d a3,
			   struct c a4,
			   struct c a5,
			   struct c a6,
			   struct c a7,
			   struct c a8,
			   struct c a9)
{
	printf("a.bs.bi: %d, a.i[0]: %d, a.i[1]: %d, a.j: %d\n", a.bs.bi, a.i[0], a.i[1], a.j);
	//printf("a1.bs.bi: %d, a1.i[0]: %d, a1.i[1]: %d, a1.j: %d\n", a1.bs.bi, a1.i[0], a1.i[1], a1.j);
	printf("a1.bs.bi: %d, a1.f: %f, a1.d: %f\n", a1.bs.bi, a1.f, a1.d);
	printf("a2.bs.bi: %d, a2.i[0]: %d, a2.i[1]: %d, a2.j: %d\n", a2.bs.bi, a2.i[0], a2.i[1], a2.j);
	printf("a3.bs.bi: %d, a3.f: %f, a3.d: %f\n", a3.bs.bi, a3.f, a3.d);
	printf("a9.bs.bi: %d, a9.i[0]: %d, a9.i[1]: %d, a9.j: %d\n", a9.bs.bi, a9.i[0], a9.i[1], a9.j);
	a9.bs.bi = 12345;
}

struct c return_s (int i)
{
	struct c cs;
	cs.j = i;
	return cs;
}
