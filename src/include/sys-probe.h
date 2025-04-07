//
//  File: %sys-probe.h
//  Summary: "Polymorphic Pointer Probing Tool (Cell*, Stub*, Utf8*)"
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
// The PROBE() macro can be used in RUNTIME_CHECKS builds to mold cells much
// like the Rebol2 `probe` operation.  But it's actually polymorphic, and if
// you have a Flex*, VarList*, or Array*, UTF-8 String, etc. it can be used
// with those as well.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * Since macros can't be called from the C debugger watchlist, Probe() and
//   Probe_Limit() functions are defined as well.  Very useful!
//
// * In C++, PROBE is done via template, so you can even get the same value
//   and type out as input.  This permits things like:
//
//       return PROBE(Make_Some_Flex(...));
//
// * In order to make it easier to find out where a piece of debug spew is
//   coming from, the file and line number will be output.
//
// * As a convenience, PROBE also flushes the `stdout` and `stderr` in case
//   the checked build was using printf() to output contextual information.
//

#if CPLUSPLUS_11
    template <
        typename T,
        typename std::enable_if<
            std::is_pointer<T>::value  // assume pointers are Node*
            or std::is_convertible<T,Cell*>::value
            or std::is_convertible<T,Stub*>::value
        >::type* = nullptr
    >
    T Probe_Cpp_Helper(
        T v,
        Length limit,
        const char *expr,
        Option(const char*) file,
        Option(LineNumber) line
    ){
        Probe_Core_Debug(v, limit, expr, file, line);
        return v;
    }

    template <
        typename T,
        typename std::enable_if<
            !std::is_pointer<T>::value  // ordinary << output operator
            and !std::is_convertible<T,Cell*>::value
            and !std::is_convertible<T,Stub*>::value
        >::type* = nullptr
    >
    T Probe_Cpp_Helper(
        T v,
        Length limit,
        const char *expr,
        Option(const char*) file,
        Option(LineNumber) line
    ){
        std::stringstream ss;
        ss << v;
        printf("C++ PROBE(%s) => %s\n", expr, ss.str().c_str());
        UNUSED(file);
        UNUSED(line);
        UNUSED(limit);
        return v;
    }

    #define PROBE(v) \
        Probe_Cpp_Helper((v), 0, #v, __FILE__, __LINE__)
#else
    #define PROBE(v) \
        Probe_Core_Debug((v), 0, #v, __FILE__, __LINE__)  // returns void*
#endif

#define WHERE(L) \
    Where_Core_Debug(L)
