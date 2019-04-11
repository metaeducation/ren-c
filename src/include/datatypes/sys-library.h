//
//  File: %sys-library.h
//  Summary: "Definitions for LIBRARY! (DLL, .so, .dynlib)"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2014 Atronix Engineering, Inc.
// Copyright 2014-2017 Rebol Open Source Contributors
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
//=////////////////////////////////////////////////////////////////////////=//
//
// A library represents a loaded .DLL or .so file.  This contains native
// code, which can be executed through extensions.  The type is also used to
// load and execute non-Rebol-aware C code by the FFI extension.
//
// File descriptor in singular->link.fd
// Meta information in singular->misc.meta
//

typedef REBARR REBLIB;

inline static void *LIB_FD(REBLIB *l) {
    return LINK(l).fd; // file descriptor
}

inline static bool IS_LIB_CLOSED(REBLIB *l) {
    return LINK(l).fd == NULL;
}

#define VAL_LIBRARY_SINGULAR_NODE(v) \
    PAYLOAD(Any, (v)).first.node

inline static REBLIB *VAL_LIBRARY(const REBCEL *v) {
    assert(CELL_CUSTOM_TYPE(v) == PG_Library_Type);
    return ARR(VAL_LIBRARY_SINGULAR_NODE(v));
}

#define VAL_LIBRARY_META_NODE(v) \
    MISC_META_NODE(VAL_LIBRARY_SINGULAR_NODE(v))

inline static REBCTX *VAL_LIBRARY_META(const REBCEL *v) {
    assert(CELL_CUSTOM_TYPE(v) == PG_Library_Type);
    return CTX(VAL_LIBRARY_META_NODE(v));
}


inline static void *VAL_LIBRARY_FD(const REBCEL *v) {
    assert(CELL_CUSTOM_TYPE(v) == PG_Library_Type);
    return LIB_FD(VAL_LIBRARY(v));
}
