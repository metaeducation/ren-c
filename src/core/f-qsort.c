/* This file was downloaded from
 * https://raw.github.com/android/platform_bionic/master/libc/upstream-freebsd/lib/libc/stdlib/qsort.c
 */


// "The qsort_r() function is identical to qsort() except that the comparison
// function takes a third argument. A pointer is passed to the comparison
// function via [thunk]. In this way, the comparison function does not
// need to use global variables to pass through arbitrary arguments, and
// is therefore reentrant and safe to use in threads."
//
// This file can declare either qsort or qsort_r, and we'd like the latter.
// Note that `qsort_r` is part of no portability standard, and this version
// (used by Android) puts the "thunk" as the next to last parameter instead
// of the last one.  :-/
//
#define I_AM_QSORT_R


/*-
 * Copyright (c) 1992, 1993
 *  The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#if defined(LIBC_SCCS) && !defined(lint)
static char sccsid[] = "@(#)qsort.c 8.1 (Berkeley) 6/4/93";
#endif /* LIBC_SCCS and not lint */

/* commented out by L.M.
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
*/

#include <stdlib.h>

// When qsort_r is defined, it will actually wind up being named bsd_qsort_r.
// Define this after including <stdlib.h> to avoid the prototype being
// declared as extern "C"
//
#define qsort_r bsd_qsort_r

#ifdef I_AM_QSORT_R
typedef int      cmp_t(void *, const void *, const void *);
#else
typedef int      cmp_t(const void *, const void *);
#endif
#ifdef _MSC_VER
#define __inline__
#else
#define __inline__ inline
#endif

static __inline__ char  *med3(char *, char *, char *, cmp_t *, void *);
static __inline__ void   swapfunc(char *, char *, int, int);

#if !defined(min)
    #define min(a, b)   (a) < (b) ? a : b
#endif

/*
 * Qsort routine from Bentley & McIlroy's "Engineering a Sort Function".
 */
#define swapcode(TYPE, parmi, parmj, n) {       \
    long i = (n) / sizeof (TYPE);           \
    TYPE *pi = (TYPE *) (parmi);        \
    TYPE *pj = (TYPE *) (parmj);        \
    do {                        \
        TYPE    t = *pi;        \
        *pi++ = *pj;                \
        *pj++ = t;              \
        } while (--i > 0);              \
}

#define SWAPINIT(a, es) swaptype = ((char *)a - (char *)0) % sizeof(long) || \
    es % sizeof(long) ? 2 : es == sizeof(long)? 0 : 1;

static __inline__ void
swapfunc(char *a, char *b, int n, int swaptype)
{
    if(swaptype <= 1)
        swapcode(long, a, b, n)
    else
        swapcode(char, a, b, n)
}

#define swap(a, b)                  \
    if (swaptype == 0) {                \
        long t = *(long *)(a);          \
        *(long *)(a) = *(long *)(b);        \
        *(long *)(b) = t;           \
    } else                      \
        swapfunc((char*)a, (char*)b, es, swaptype)

#define vecswap(a, b, n)    if ((n) > 0) swapfunc(a, b, n, swaptype)

#ifdef I_AM_QSORT_R
#define CMP(t, x, y) (cmp((t), (x), (y)))
#else
#define CMP(t, x, y) (cmp((x), (y)))
#endif

static __inline__ char *
med3(char *a, char *b, char *c, cmp_t *cmp, void *thunk
#ifndef I_AM_QSORT_R

/* commented out by L.M.
__unused
*/

#endif
)
{
    return CMP(thunk, a, b) < 0 ?
           (CMP(thunk, b, c) < 0 ? b : (CMP(thunk, a, c) < 0 ? c : a ))
              :(CMP(thunk, b, c) > 0 ? b : (CMP(thunk, a, c) < 0 ? a : c ));
}

#ifdef I_AM_QSORT_R
void
qsort_r(void *a, size_t n, size_t es, void *thunk, cmp_t *cmp)
#else
#define thunk NULL
void
qsort(void *a, size_t n, size_t es, cmp_t *cmp)
#endif
{
    char *pa, *pb, *pc, *pd, *pl, *pm, *pn;
    size_t d, r;
    int cmp_result;
    int swaptype, swap_cnt;

loop:   SWAPINIT(a, es);
    swap_cnt = 0;
    if (n < 7) {
        for (pm = (char *)a + es; pm < (char *)a + n * es; pm += es)
            for (pl = pm;
                 pl > (char *)a && CMP(thunk, pl - es, pl) > 0;
                 pl -= es)
                swap(pl, pl - es);
        return;
    }
    pm = (char *)a + (n / 2) * es;
    if (n > 7) {
        pl = (char *)a;
        pn = (char *)a + (n - 1) * es;
        if (n > 40) {
            d = (n / 8) * es;
            pl = med3(pl, pl + d, pl + 2 * d, cmp, thunk);
            pm = med3(pm - d, pm, pm + d, cmp, thunk);
            pn = med3(pn - 2 * d, pn - d, pn, cmp, thunk);
        }
        pm = med3(pl, pm, pn, cmp, thunk);
    }
    swap(a, pm);
    pa = pb = (char *)a + es;

    pc = pd = (char *)a + (n - 1) * es;
    for (;;) {
        while (pb <= pc && (cmp_result = CMP(thunk, pb, a)) <= 0) {
            if (cmp_result == 0) {
                swap_cnt = 1;
                swap(pa, pb);
                pa += es;
            }
            pb += es;
        }
        while (pb <= pc && (cmp_result = CMP(thunk, pc, a)) >= 0) {
            if (cmp_result == 0) {
                swap_cnt = 1;
                swap(pc, pd);
                pd -= es;
            }
            pc -= es;
        }
        if (pb > pc)
            break;
        swap(pb, pc);
        swap_cnt = 1;
        pb += es;
        pc -= es;
    }
    if (swap_cnt == 0) {  /* Switch to insertion sort */
        for (pm = (char *)a + es; pm < (char *)a + n * es; pm += es)
            for (pl = pm;
                 pl > (char *)a && CMP(thunk, pl - es, pl) > 0;
                 pl -= es)
                swap(pl, pl - es);
        return;
    }

    pn = (char *)a + n * es;
    r = min(pa - (char *)a, pb - pa);
    vecswap((char*)a, (char *)(pb - r), r);
    // !!! Ren-C: pn - pd - es => (long)(pn - pd - ps) for -Wsign-compare
    r = min(pd - pc, (long)(pn - pd - es));
    vecswap(pb, pn - r, r);
    if ((r = pb - pa) > es)
#ifdef I_AM_QSORT_R
        qsort_r(a, r / es, es, thunk, cmp);
#else
        qsort(a, r / es, es, cmp);
#endif
    if ((r = pd - pc) > es) {
        /* Iterate rather than recurse to save stack space */
        a = pn - r;
        n = r / es;
        goto loop;
    }
/*      qsort(pn - r, r / es, es, cmp);*/
}
