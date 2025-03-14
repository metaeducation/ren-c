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

#include "rebol-internals.h"  // definitions common to core and core extensions


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
//
// 3. %tmp-paramlists.h is the file containing macros for natives and actions
//    that map their argument names to indices in the frame.  This defines the
//    macros like INCLUDE_ARGS_OF_INSERT which then allow you to naturally
//    write things like REF(part) and ARG(limit), instead of the brittle
//    integer-based system used in R3-Alpha such as D_REF(7) and ARG_N(3).

#define NATIVE_CFUNC(name)  N_##name  // e.g. NATIVE_CFUNC(foo) => N_foo

#define DECLARE_NATIVE(name) \
    Bounce N_##name(Level* level_)  // NATIVE_CFUNC(macro) would expand [1]

#include "tmp-native-fwd-decls.h"  // forward declarations of natives [2]

#include "tmp-paramlists.h"  // INCLUDE_ARGS_OF_XXX macro definitions [3]


//=//// GENERICS ///////////////////////////////////////////////////////////=//
//
// Historical Rebol mapped each datatype to a function which had a switch()
// statement with cases representing every generic function that type could
// handle.  It was possible to write code that was shared among all the
// generics at the top before the switch() or at the bottom after it, and goto
// could be used to jump between the handlers.
//
// Ren-C uses a more granular approach, where each generic's entry point is
// very much like a native.  This makes it possible to write common code that
// runs before or after the moment of dispatch, implementing invariants that
// are specific to each generic.  Then implementations are more granular,
// associating a datatype or other "decider" (like a typeset) in tables that
// are assembled during the build preparation.
//
// 1. At the moment, extensions are not allowed to define generics.  That
//    would complicate the table generation, but such complications would
//    be necessary if user types were going to handle the generic.
//
// 2. See DECLARE_NATIVE() noates for why G_##name##_##type is repeated here.
//
// 3. Forward definitions of IMPLEMENT_GENERIC() for all the generics.

#define GENERIC_CFUNC(name,type)  G_##name##_##type  // no extension form [1]

#define IMPLEMENT_GENERIC(name,type) \
    Bounce G_##name##_##type(Level* level_)  // doesn't use GENERIC_CFUNC() [2]

#include "tmp-generic-fwd-decls.h"  // forward generic handler definitions [3]
