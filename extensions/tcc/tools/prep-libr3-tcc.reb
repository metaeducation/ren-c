REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Pre-Build Step for API entry points exported via tcc_add_symbol()"
    File: %prep-libr3-tcc.reb

    Rights: {
        Copyright 2017 Atronix Engineering
        Copyright 2017-2021 Ren-C Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Description: {
        The TCC extension compiles user natives into memory directly.  These
        natives are linked against some libs that are on disk (the extension
        is packaged with some of these libraries that it extracts and puts
        on disk to use).  However, the API entry points for rebol.h are
        connected to the running executable directly with tcc_add_symbol().

        !!! This is redundant with the API export table used in the main API,
        except that table does not have the string names in it.  If it did,
        we could just use that.  It's probably worth it to store those strings
        in non-TCC builds in order to avoid having this extra complexity in
        the build process...although that is kind of presumptive that the
        libRebol mechanics won't change (but maybe an okay assumption to make)
    }
]

; Note: There are no `import` statements here because this is run via DO LOAD
; inside the %make-reb-lib.r script's context.  This is done in order to
; inherit the `api` object, and the `for-each-api` enumerator.  As a result
; it also inherits access to CWRAP and other tools.  Review.


e: make-emitter "libRebol exports for tcc_add_symbol()" (
    join output-dir %tmp-librebol-symbols.inc
)

for-each-api [
    e/emit [name] {
        Add_API_Symbol_Helper(state, "RL_$<Name>", cast(CFUNC*, &RL_$<Name>));
    }
]

e/write-emitted
