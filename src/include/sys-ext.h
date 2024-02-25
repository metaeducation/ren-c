//
//  File: %sys-ext.h
//  Summary: "Extension Hook Point Definitions"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2016-2018 Ren-C Open Source Contributors
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

#if defined(LIBREBOL_USES_API_TABLE)
    //
    // This indicates an "external extension".  Its entry point has a
    // predictable name of RX_Collate() exported from the DLL.

    #define EXT_API EXTERN_C API_EXPORT  // Hosting Rebol is a DLL/LIB

    // Just ignore the extension name parameter
    //
    #define RX_COLLATE_NAME(ext_name) RX_Collate
#else
    // If LIBREBOL_USES_API_TABLE is not defined, this is a "built-in
    // extension".  It is part of the exe or lib, and its loader function must
    // be distinguished by name from other extensions that are built-in.
    //
    // !!! This could also be done with some kind of numbering scheme (UUID?)
    // by the build process, but given that name collisions in Rebol cause
    // other problems the idea of not colliding with extension filenames
    // is par for the course.

    #ifdef __cplusplus
      #define EXT_API extern "C"
    #else
      #define EXT_API
    #endif

    // *Don't* ignore the extension name parameter
    //
    #define RX_COLLATE_NAME(ext_name) RX_Collate_##ext_name
#endif


//=//// EXTENSION MACROS //////////////////////////////////////////////////=//

#define DECLARE_EXTENSION_COLLATOR(ext_name) \
    EXT_API RebolValue* RX_COLLATE_NAME(ext_name)(RebolApiTable* api)
