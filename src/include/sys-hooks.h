//
//  File: %sys-hooks.h
//  Summary: "Function Pointer Definitions, defined before %tmp-internals.h"
//  Project: "Ren-C Interpreter and Run-time"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2021 Ren-C Open Source Contributors
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
// These are function pointers that need to be defined early, before the
// aggregated forward declarations for the core.
//


//
// EXTENSION COLLATOR FUNCTION DEFINITION
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
    IDX_COLLATOR_BINDING_REF = 0,
    IDX_COLLATOR_SCRIPT,
    IDX_COLLATOR_SCRIPT_NUM_CODEPOINTS,
    IDX_COLLATOR_CFUNCS,
    IDX_COLLATOR_MAX
};

#define CELL_FLAG_CFUNCS_NOTE_USE_LIBREBOL  CELL_FLAG_NOTE

// GenericInfo is a replacement for all makehook, tohook, mold, compare, etc.
//
typedef struct {
    TypesetByte typeset_byte;  // derived from IMPLEMENT_GENERIC()'s type
    Dispatcher* dispatcher;  // the function defined by IMPLEMENT_GENERIC()
} GenericInfo;


// Port hook: for implementing generic ACTION!s on a PORT! class
//
typedef Bounce (PORT_HOOK)(Level* level_, Value* port, const Symbol* verb);
