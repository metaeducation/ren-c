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
// the inner bits of cells and series provided by %rebol-internals.h.  But
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

// Helper for declaring a native dispatcher function.  Extensions do this
// differently, by including the module name in the function name.
//
#define DECLARE_NATIVE(n) \
    Bounce N_##n(Level* level_)

// Helper for declaring an intrinsic native (can be dispatched w/o a frame).
//
#define DECLARE_INTRINSIC(n) \
    void N_##n(Atom* out, Phase* phase, Value* arg)

// Forward definitions of DECLARE_NATIVE() and DECLARE_INTRINSIC() for all
// the core natives.  This means functions are available as &N_native_name
// throughout the core code if they are needed.
//
#include "tmp-native-fwd-decls.h"

// %tmp-paramlists.h is the file that contains macros for natives and actions
// that map their argument names to indices in the frame.  This defines the
// macros like INCLUDE_ARGS_FOR_INSERT which then allow you to naturally
// write things like REF(part) and ARG(limit), instead of the brittle integer
// based system used in R3-Alpha such as D_REF(7) and D_ARG(3).
//
#include "tmp-paramlists.h"
