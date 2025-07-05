//
//  file: %sys-crash.h
//  summary: "Force System Exit with Diagnostic Info"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2025 Ren-C Open Source Contributors
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
// Crashes are the equivalent of the "blue screen of death" and should never
// happen in normal operation.  Generally, it is assumed nothing under the
// user's control could fix or work around the issue, hence the main goal is
// to provide the most diagnostic information possible to developers.
//
// From C code, crashes are triggered by calling `crash()` or `crash_at()`.
// Even if a state is not critical--such as a memory leak--it's preferable
// to crash the interpreter so that users will report the issue...instead
// of having it get lost in the shuffle like a normal error.  (Deferring the
// crash until shutdown may be acceptable for some non-corrupting cases.)
//
// The best thing to do is to pass in whatever Cell or Flex subclass
// (including Array*, VarList*, Phase*...) is a useful "smoking gun":
//
//     if (Type_Of(value) == TYPE_QUASIFORM)
//         crash (value);  // checked build points out this file and line
//
//     if (Array_Len(array) < 2)
//         crash (array);  // crash is polymorphic, see Detect_Rebol_Pointer()
//
// But if no smoking gun is available, a UTF-8 string can also be passed to
// crash...and it will terminate with that as a message:
//
//     if (sizeof(foo) != 42)
//         crash ("invalid foo size");  // kind of redundant with file + line

// From Rebol code, crashes are triggered by the CRASH native.  While usermode
// crashes do not represent situations where the interpreter internals are
// experiencing some kind of corruption, it's still important to terminate
// the interpreter.  The usermode code presumably noticed it was in a
// semantically bad state that could harm the user's files--despite the
// interpreter working fine.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * It's desired that there be a space in `crash (...)` to make it look
//   more "keyword-like" and draw attention that it's a `noreturn` call.
//
// * The diagnostics are written in such a way that they give the "more likely
//   to succeed" output first, and then get more aggressive to the point of
//   possibly crashing by dereferencing corrupt memory which triggered the
//   crash.  The checked build diagnostics will be more exhaustive, but the
//   release build gives some info.
//

#if TRAMPOLINE_COUNTS_TICKS
    #define TICK  g_tick
#else
    #define TICK  cast(Tick, 0)  // for TRAMPOLINE_COUNTS_TICKS agnostic code
#endif


#if DEBUG_FANCY_CRASH
    #define crash(p) \
        Crash_Core((p), TICK, __FILE__, __LINE__)

    #define crash_at(p,file,line) \
        Crash_Core((p), TICK, (file), (line))
#else
    #define crash(p) \
        Crash_Core((p), TICK, nullptr, 0)

    #define crash_at(v,file,line) \
        UNUSED(file); \
        UNUSED(line); \
        crash (p)
#endif
