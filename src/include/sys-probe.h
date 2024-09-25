//
//  File: %sys-probe.h
//  Summary: {Polymorphic Pointer Probing Tool (Cell*, Stub*, Utf8*}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2024 Ren-C Open Source Contributors
// Copyright 2012 REBOL Technologies
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
//              !!! THIS IS IMPORTANT, LEARN TO USE IT !!!
//
// The PROBE() macro can be used in debug builds to mold a cell much like the
// Rebol2 `probe` operation.  But it's actually polymorphic, and if you have
// a Flex*, VarList*, or Array* it can be used with those as well.
//
// A function Probe() is defined as well, which can be called directly from
// C debuggers (without having to pass the file and line number needed by the
// Probe_Core_Debug() function.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * In C++, PROBE is done via template, so you can even get the same value
//   and type out as input...just like in Rebol.  This permits things like:
//
//       return PROBE(Make_Some_Flex(...));
//
// * In order to make it easier to find out where a piece of debug spew is
//   coming from, the file and line number will be output as well.
//
// * As a convenience, PROBE also flushes the `stdout` and `stderr` in case
//   the debug build was using printf() to output contextual information.
//

#if CPLUSPLUS_11
    template <
        typename T,
        typename std::enable_if<
            std::is_pointer<T>::value  // assume pointers are Node*
        >::type* = nullptr
    >
    T Probe_Cpp_Helper(T v, const char *expr, const char *file, int line)
    {
        Probe_Core_Debug(v, expr, file, line);
        return v;
    }

    template <
        typename T,
        typename std::enable_if<
            !std::is_pointer<T>::value  // ordinary << output operator
        >::type* = nullptr
    >
    T Probe_Cpp_Helper(T v, const char *expr, const char *file, int line)
    {
        std::stringstream ss;
        ss << v;
        printf("PROBE(%s) => %s\n", expr, ss.str().c_str());
        UNUSED(file);
        UNUSED(line);
        return v;
    }

    #define PROBE(v) \
        Probe_Cpp_Helper((v), #v, __FILE__, __LINE__)
#else
    #define PROBE(v) \
        Probe_Core_Debug((v), #v, __FILE__, __LINE__)  // returns void*
#endif

#define WHERE(L) \
    Where_Core_Debug(L)
