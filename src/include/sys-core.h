//
//  File: %sys-core.h
//  Summary: "Core API With Additional Definitions for Core Natives"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2024 Ren-C Open Source Contributors
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
// While most extensions should use %rebol.h, some extensions need access to
// the inner bits of Cells and Stubs provided by %rebol-internals.h.  But
// they don't want *all* the definitions the core uses.  So those definitions
// are added in this file.
//
// As an example, there may be overlaps in the name of a native that is in
// an extension and in the core.  This would mean the INCLUDE_PARAMS_OF_XXX
// macros would collide.
//
// So the core includes this distinct file, that adds a few definitions which
// would not be applicable to extensions using the internal API.
//

#include "rebol-internals.h"

//=//// NATIVES ////////////////////////////////////////////////////////////=//
//
// The core has a different definition of DECLARE_NATIVE() than extensions.
// Extensions have to include the module name in the function name, in case
// they are linked directly into the executable--so their linknames aren't
// ambiguous with core natives (or other extension natives) of the same name.
//
// 1. Because there are macros for things like `maybe`, trying to reuse the
//    NATIVE_CFUNC() macro inside DECLARE_NATIVE() would expand maybe before
//    passing it to the token paste.  It's easiest just to repeat `N_##name`
//
// 2. Forward definitions of DECLARE_NATIVE() for all the core natives.  This
//    means functions are available via NATIVE_CFUNC() throughout the core code
//    if it wants to explicitly reference a native's dispatcher function.

#define NATIVE_CFUNC(name)  N_##name  // e.g. NATIVE_CFUNC(foo) => N_foo

#define DECLARE_NATIVE(name) \
    Bounce N_##name(Level* level_)  // NATIVE_CFUNC(macro) would expand [1]

#include "tmp-native-fwd-decls.h"  // forward declarations of natives [2]


// %tmp-paramlists.h is the file that contains macros for natives and actions
// that map their argument names to indices in the frame.  This defines the
// macros like INCLUDE_ARGS_FOR_INSERT which then allow you to naturally
// write things like REF(part) and ARG(limit), instead of the brittle integer
// based system used in R3-Alpha such as D_REF(7) and ARG_N(3).
//
#include "tmp-paramlists.h"
