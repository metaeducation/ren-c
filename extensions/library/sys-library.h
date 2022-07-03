//
//  File: %sys-library.h
//  Summary: "Definitions for LIBRARY! (DLL, .so, .dynlib)"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2014 Atronix Engineering, Inc.
// Copyright 2014-2017 Ren-C Open Source Contributors
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
// A library represents a loaded .DLL or .so file.  This contains native
// code, which can be executed through extensions.  The type is also used to
// load and execute non-Rebol-aware C code by the FFI extension.
//
// File descriptor in singular->link.fd
// Meta information in singular->misc.meta
//

typedef Raw_Array REBLIB;

extern REBTYP *EG_Library_Type;

inline static bool IS_LIBRARY(Cell(const*) v) {  // Note: QUOTED! doesn't count
    return IS_CUSTOM(v) and CELL_CUSTOM_TYPE(v) == EG_Library_Type;
}

inline static void *LIB_FD(REBLIB *lib)
  { return lib->link.fd; }  // (F)ile (D)escriptor

inline static bool IS_LIB_CLOSED(REBLIB *lib)
  { return lib->link.fd == nullptr; }

inline static REBLIB *VAL_LIBRARY(noquote(Cell(const*)) v) {
    assert(CELL_CUSTOM_TYPE(v) == EG_Library_Type);
    return ARR(VAL_NODE1(v));
}


inline static void *VAL_LIBRARY_FD(noquote(Cell(const*)) v) {
    assert(CELL_CUSTOM_TYPE(v) == EG_Library_Type);
    return LIB_FD(VAL_LIBRARY(v));
}


// !!! These functions are currently statically linked to by the FFI extension
// which should probably be finding a way to do this through the libRebol API
// instead.  That could avoid the static linking--but it would require the
// library to give back HANDLE! or otherwise pointers that could be used to
// call the C functions.
//
extern void *Open_Library(const REBVAL *path);
extern void Close_Library(void *dll);
extern CFUNC *Find_Function(void *dll, const char *funcname);
