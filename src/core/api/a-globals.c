//
//  file: %a-globals.c
//  summary: "global variables"
//  section: environment
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
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
// There are two types of global variables:
//   process vars - single instance for main process
//   thread vars - duplicated within each R3 task
//

/* To do: there are still a few globals in various modules that need to be
** incorporated back into sys-globals.h.
*/

#include "sys-core.h"

#undef PVAR
#undef TVAR

#define PVAR
#define TVAR

#include "sys-globals.h"
