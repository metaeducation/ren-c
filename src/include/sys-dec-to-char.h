//
//  file: %sys-dec-to-char.h
//  summary: "Decimal conversion wrapper"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 Saphirion AG
// Copyright 2012-2017 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
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
