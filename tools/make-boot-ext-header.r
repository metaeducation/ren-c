REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Generate extention native header files"
    File: %make-boot-ext-header.r  ; EMIT-HEADER uses to note emitting script
    Rights: {
        Copyright 2017 Atronix Engineering
        Copyright 2017-2018 Ren-C Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Needs: 2.100.100
    Description: {
        Extensions are written in such a way that based on influencing some
        #define macros in %sys-ext.h, they can target being standalone DLLs,
        or as part of a Rebol EXE, or as part of Rebol built as a library.

        Each extension has a corresponding C function that collates all the
        static pieces of data for the extension and returns it to the system.
        (This includes native C function dispatchers, compressed UTF-8 of the
        specs for those native functions, the compressed module source code
        for the Rebol portion of the extension's implementation, etc.)

        For a DLL that C function has a fixed name that the system can find
        through system APIs when loading the extension from a file.  But for
        extensions built into the EXE, there can be several of them...each
        needing a unique name for the collating function.  Then these have to
        be gathered in a table to be gathered up so the system can find them.

        This script gets the list of *only* the built-in extensions on the
        command line, then builds that table.  It is included as part of
        %a-lib.h, which exposes it via the rebBuiltinExtensions() API to
        whatever client (C, JavaScript, etc.) that may want to start them
        up selectively...which must be at some point *after* rebStartup().
    }
]

do %common.r
do %common-emitter.r

r3: system/version > 2.100.0

args: parse-args system/script/args  ; either from command line or DO/ARGS
output-dir: make-file [(system/options/path) prep /]
mkdir/deep make-file [(output-dir) include /]

extensions: map-each e (split args/EXTENSIONS #":") [
    to-c-name e  ; so SOME-EXTENSION becomes SOME_EXTENSION for C macros
]

e: (make-emitter "Boot Modules"
        make-file [(output-dir) include/tmp-boot-extensions.inc])

e/emit {
    #include "sys-ext.h"

    DECLARE_EXT_COLLATE($[Extensions]);

    #define NUM_BUILTIN_EXTENSIONS $<length of extensions>

    /*
     * List of C functions that can be called to fetch the collated info that
     * the system needs to bring an extension into being.  No startup or
     * decompression code is run when these functions are called...they just
     * return information needed for later reifying those extensions.
     */
    static COLLATE_CFUNC *Builtin_Extension_Collators[] = {
        RX_COLLATE_NAME($[Extensions]),
        nullptr  /* Just for guaranteeing length > 0, as C++ requires it */
    };
}

e/write-emitted
