//
//  file: %sys-dec-to-char.h
//  summary: "Decimal conversion wrapper"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 Saphirion AG
// Copyright 2012-2017 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//

EXTERN_C char *dtoa(
    double dd,
    int mode,
    int ndigits,
    int *decpt,
    int *sign,
    char **rve
);

// Note: `strtod()` is generally considered to be part of the standard C
// library.  R3-Alpha did not include that part of the dtoa.c code, e.g.
// there is no definition of strtod() here, only a prototype:
//
// https://github.com/rebol/rebol/blob/master/src/core/f-dtoa.c
//
// See further notes in %f-dtoa.c
