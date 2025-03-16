//
//  File: %bsd-qsort_r.h
//  Summary: "Definition for bundled qsort_r() obeying BSD conventions"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// bsd_qsort_r() implementation is Copyright (c) 1992, 1993
// The Regents of the University of California.
//
// See README.md and CREDITS.md for more information.
//
//=////////////////////////////////////////////////////////////////////////=//
//
// !!! There are several incompatible definitions for qsort_r:
//
// * BSD/macOS version: comparator first, context last
// * GNU/Linux version: context in the middle
// * C11 standard (qsort_s): context first
//
// Due to not being able to trust the availability of a known implementations
// Rebol includes the FreeBSD implementation, and hacks it to define the
// function as `bsd_qsort_r()`, so that it won't conflict with any standard
// library names.  This means the qsort_r() code for BSD is actually built
// into the executable, to avoid misunderstandings. 
//
//=//// NOTES //////////////////////////////////////////////////////////////=//
//
// A. The BSD definition calls the parameter tunneled through to the callback
//    a "thunk".  This is not the conventional meaning of what a thunk is,
//    so the name is no longer used in Ren-C sources for that argument.
//
// B. The sources for qsort_r() were directly edited to force the definition
//    to use the name bsd_qsort_r()
//

typedef int cmp_t(void *, const void *, const void *);
extern void bsd_qsort_r(void *a, size_t n, size_t es, void *thunk, cmp_t *cmp);
