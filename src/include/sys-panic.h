//
//  file: %sys-panic.h
//  summary: "Force System Exit with Diagnostic Info"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2019 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Panics are the equivalent of the "blue screen of death" and should never
// happen in normal operation.  Generally, it is assumed nothing under the
// user's control could fix or work around the issue, hence the main goal is
// to provide the most diagnostic information possible to devleopers.
//
// The best thing to do is to pass in whatever Cell or Flex subclass
// (including Array*, VarList*, Phase*...) is a useful "smoking gun":
//
//     if (Type_Of(value) == TYPE_QUASIFORM)
//         panic (value);  // checked build points out this file and line
//
//     if (Array_Len(array) < 2)
//         panic (array);  // panic is polymorphic, see Detect_Rebol_Pointer()
//
// But if no smoking gun is available, a UTF-8 string can also be passed to
// panic...and it will terminate with that as a message:
//
//     if (sizeof(foo) != 42)
//         panic ("invalid foo size");  // kind of redundant with file + line
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * It's desired that there be a space in `panic (...)` to make it look
//   more "keyword-like" and draw attention that it's a `noreturn` call.
//
// * The diagnostics are written in such a way that they give the "more likely
//   to succeed" output first, and then get more aggressive to the point of
//   possibly crashing by dereferencing corrupt memory which triggered the
//   panic.  The checked build diagnostics will be more exhaustive, but the
//   release build gives some info.
//

#if TRAMPOLINE_COUNTS_TICKS
    #define TICK g_tick
#else
    #define TICK u_cast(Tick, 0)  // for TRAMPOLINE_COUNTS_TICKS agnostic code
#endif


#if DEBUG_FANCY_PANIC
    #define panic(v) \
        Panic_Core((v), TICK, __FILE__, __LINE__)

    #define panic_at(v,file,line) \
        Panic_Core((v), TICK, (file), (line))
#else
    #define panic(v) \
        Panic_Core((v), TICK, nullptr, 0)

    #define panic_at(v,file,line) \
        UNUSED(file); \
        UNUSED(line); \
        panic(v)
#endif
