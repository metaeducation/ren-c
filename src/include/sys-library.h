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
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//=////////////////////////////////////////////////////////////////////////=//
//
// A library represents a loaded .DLL or .so file.  This contains native
// code, which can be executed through extensions.  The type is also used to
// load and execute non-Rebol-aware C code by the FFI extension.
//

INLINE void *LIB_FD(REBLIB *l) {
    return LINK(l).fd; // file descriptor
}

INLINE bool IS_LIB_CLOSED(REBLIB *l) {
    return LINK(l).fd == nullptr;
}

INLINE REBCTX *VAL_LIBRARY_META(const Cell* v) {
    return MISC(v->payload.library.singular).meta;
}

INLINE REBLIB *VAL_LIBRARY(const Cell* v) {
    return v->payload.library.singular;
}

INLINE void *VAL_LIBRARY_FD(const Cell* v) {
    return LIB_FD(VAL_LIBRARY(v));
}
