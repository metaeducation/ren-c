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


// The API Action Details can be built manually by things like the TCC
// extension.  It doesn't want to use rebFunction() because it allows a weird
// behavior of defining a function and then having it compiled on demand
// into something that uses the Api_Function_Dispatcher(), and it wants to
// reuse the paramlist it already has.
//
enum {
    IDX_API_ACTION_CFUNC = 1,  // HANDLE! of RebolActionCFunction*
    IDX_API_ACTION_BINDING_BLOCK,  // BLOCK! so binding is GC marked
    IDX_API_ACTION_MAX
};


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


// PER-TYPE TO HOOKS: for `to datatype value`
//
// These functions must return a Value* to the type they are making
// (either in the output cell or an API cell).  They are NOT allowed to
// throw, and are not supposed to make use of any binding information in
// blocks they are passed...so no evaluations should be performed.
//
// !!! Note: It is believed in the future that MAKE would be constructor
// like and decided by the destination type, while TO would be "cast"-like
// and decided by the source type.  For now, the destination decides both,
// which means TO-ness and MAKE-ness are a bit too similar.
//
typedef Bounce (ToHook)(Level* level_, Kind kind, Element* def);


// Port hook: for implementing generic ACTION!s on a PORT! class
//
typedef Bounce (PORT_HOOK)(Level* level_, Value* port, const Symbol* verb);


//=//// PARAMETER ENUMERATION /////////////////////////////////////////////=//
//
// Parameter lists of composed/derived functions still must have compatible
// frames with their underlying C code.  This makes parameter enumeration of
// a derived function a 2-pass process that is a bit tricky.
//
// !!! Due to a current limitation of the prototype scanner, a function type
// can't be used directly in a function definition and have it be picked up
// for %tmp-internals.h, it has to be a typedef.
//
typedef enum {
    PHF_UNREFINED = 1 << 0  // a /refinement that takes an arg, made "normal"
} Reb_Param_Hook_Flags;
#define PHF_MASK_NONE 0
typedef bool (PARAM_HOOK)(
    const Key* key,
    const Param* param,
    Flags flags,
    void *opaque
);
