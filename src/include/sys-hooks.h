//
//  file: %sys-hooks.h
//  summary: "Function Pointer Definitions, defined before %tmp-internals.h"
//  project: "Ren-C Interpreter and Run-time"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2025 Ren-C Open Source Contributors
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
// While there were once a variety of C function pointer types for handling
// different kinds of functionalities for types (molding, comparing, etc.)
// everything is now run through a common generics system:
//
//   https://forum.rebol.info/t/breaking-the-64-type-barrier/2369
//


//=//// GENERIC TABLES /////////////////////////////////////////////////////=//
//
// To make looking up generic implementations more optimal, the idea is not to
// build a runtime mapping from symbol IDs to dispatchers.  Instead, there
// are const global arrays of GenericInfo structs built at compile time.
// These are directly addressed by macros that use token pasting to combine
// the symbol name with the name of the type for the generic implementation.
//
// e.g. using (&GENERIC_TABLE(APPEND).info) will give you the address of the
// table mapping TypesetByte to dispatchers for the APPEND generic.  If you
// want the dispatcher for a specific generic's implementation of a given
// typeset byte, you can say something like &GENERIC_CFUNC(APPEND, Any_List).
// These give you compile-time constants.
//
// For dispatchers that are added at runtime by loaded extensions, it's a
// little trickier.  But the extension builds a mapping from the pointer for
// the generic table to the pointer for an entry that maps an ExtraHeart* to
// a dispatcher--with room for a pointer to a link to the next entry.
//
// 1. The ExtraHeart* for extension types doesn't exist until runtime.  The
//    easiest way to refer to it in the table passed to Register_Generics()
//    is thus by a pointer to where the ExtraHeart* can eventually be found.
//    But Register_Generics() will turn that into an ExtraHeart pointer in the
//    ExtraGenericInfo struct...so no double-dereference needed for lookups.
//

typedef struct {
    TypesetByte typeset_byte;  // derived from IMPLEMENT_GENERIC()'s type
    Dispatcher* dispatcher;  // the function defined by IMPLEMENT_GENERIC()
} GenericInfo;

typedef struct ExtraGenericInfoStruct {
    const ExtraHeart* ext_heart;
    Dispatcher* dispatcher;  // the function defined by IMPLEMENT_GENERIC()
    struct ExtraGenericInfoStruct* next;  // link for next extension type
} ExtraGenericInfo;

typedef struct {  // pairs builtins and extensions to pass together
    const GenericInfo* const info;
    ExtraGenericInfo* ext_info;
} GenericTable;

typedef struct {  // passed to Register_Generics() for extension types
    GenericTable* table;
    ExtraGenericInfo* ext_info;
    Api(Value*)* datatype_ptr;  // plain pointer in ExtraGenericInfo [1]
} ExtraGenericTable;


//=//// EXTENSION COLLATOR FUNCTION ///////////////////////////////////////=//
//
// Rebol Extensions generate DLLs (or embed into the EXE) with a function
// that does initialization.  But that init function does not actually
// decompress any of the script or spec code, make any natives, or run
// any startup.  It just returns an aggregate of all the information that
// would be needed to make the extension module.  So it is called a
// "collator", and it calls the API `rebExtensionCollate_internal()`
//
// !!! The result may become an ACTION! as opposed to a BLOCK! of handle
// values, but this is a work in progress.
//
#if defined(_WIN32)
    typedef Value* (__cdecl ExtensionCollator)(RebolApiTable*);
#else
    typedef Value* (ExtensionCollator)(RebolApiTable*);
#endif
enum {
    COLLATOR_BINDING_REF = 0,
    COLLATOR_SCRIPT,
    COLLATOR_SCRIPT_NUM_CODEPOINTS,
    COLLATOR_CFUNCS,
    MAX_COLLATOR = COLLATOR_CFUNCS
};

#define CELL_FLAG_CFUNCS_NOTE_USE_LIBREBOL  CELL_FLAG_NOTE
