Rebol [
    system: "Rebol [R3] Language Interpreter and Run-time Environment"
    title: "REBOL 3 Boot Sys: Top Context Functions"
    rights: {
        Copyright 2012 REBOL Technologies
        REBOL is a trademark of REBOL Technologies
    }
    license: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    context: sys
    notes: {
        Follows the BASE lib init that provides a basic set of functions
        to be able to evaluate this code.

        The boot binding of this module is SYS then LIB deep.
        Any non-local words not found in those contexts WILL BE
        UNBOUND and will error out at runtime!
    }
]

; It is desirable to express the logic of PRINT as user code, but it is
; also desirable to use PRINT from the C code.  This should likely be
; optimized as a native, but is easier to explore at the moment like this.
;
print*: :print


;-- If the host wants to know if a script or module is loaded, e.g. to
;   print out a message.  (Printing directly from this code would be
;   presumptuous.)
;
script-pre-load-hook: ~

rescue: :lib/rescue
lib/rescue: ~<See SYS.UTIL/RESCUE and https://forum.rebol.info/t/1871>~

enrescue: :lib/enrescue
lib/enrescue: ~<See SYS.UTIL/ENRESCUE and https://forum.rebol.info/t/1871>~

; DO of functions, blocks, paths, and other do-able types is done directly by
; C code in DECLARE_NATIVE(DO).  But that code delegates to this Rebol function
; for ANY-STRING! and BINARY! types (presumably because it would be laborious
; to express as C).
;
do*: function [
    {SYS: Called by system for DO on datatypes that require special handling.}
    return: [any-atom!]
    source [file! url! text! binary! tag!]
        {Files, urls and modules evaluate as scripts, other strings don't.}
    arg [any-value!]
        "Args passed as system/script/args to a script (normally a string)"
    only [logic!]
        "Do not catch quits...propagate them."
][
    ; Refinements on the original DO, re-derive for helper

    args: not null? :arg

    next: :lib/next

    ; TAG! means load new script relative to current system/script/path
    ;
    if tag? source [
        if not system/script/path [
            fail ["Can't relatively load" source "- system/script/path not set"]
        ]
        source: join system/script/path to text! source
    ]

    ; 1. DO of file path no longer evaluates in the directory of the script
    ; But we want to set the directory in the script header to the normalized
    ; version (with the `../../` and `/./` bits taken out).  Previously we
    ; got that normalization by letting the OS process it in the CHANGE-DIR
    ; instruction and then using WHAT-DIR to get where we changed to.  We
    ; don't have access to CLEAN-PATH here in sys context, and rather than
    ; sort it out for this legacy build just change to the directory and back.
    ;
    if match [file! url!] source [
        saved-dir: null
        if file: find/last/tail source slash [  ; may not have path part
            saved-dir: what-dir
            dir: copy/part source file
            change-dir dir  ; use OS to normalize path [1]
        ]
        dir: what-dir  ; effectively CLEAN-PATH'd
        if saved-dir [
            change-dir saved-dir
        ]
    ] else [
        dir: null
    ]

    original-script: null

    finalizer: lambda [
        value' [any-element!]
        /quit
        <with> return
    ][
        quit_FINALIZER: quit
        quit: :lib/quit

        ; Restore system/script if it was changed

        if original-script [system/script: original-script]

        if quit_FINALIZER [
            if only [
                quit/value value'  ; "rethrow" the QUIT if DO/ONLY
            ] else [
                value': meta value'  ; wasn't meta
            ]
        ]

        return unmeta value'  ; returns from DO*, because it's a lambda
    ]

    ; Load the code (do this before CHANGE-DIR so if there's an error in the
    ; LOAD it will trigger before the failure of changing the working dir)
    ; It is loaded as UNBOUND.
    ;
    code: ensure block! (load/header/type source 'unbound)

    ; LOAD/header returns a block with the header object in the first
    ; position, or will cause an error.  No exceptions, not even for
    ; directories or media.  "Load of URL has no special block forms." <-- ???
    ;
    ; !!! This used to LOCK the header, but the module processing wants to
    ; do some manipulation to it.  Review.  In the meantime, in order to
    ; allow mutation of the OBJECT! we have to actually TAKE the hdr out
    ; of the returned result to avoid LOCKing it when the code array is locked
    ; because even with series not at their head, LOCK NEXT CODE will lock it.
    ;
    hdr: ensure [~null~ object!] degrade take code
    is-module: 'module = select opt hdr 'type

    if (text? source) and (not is-module) [
        ;
        ; Return result without "script overhead" (e.g. don't change the
        ; working directory to the base of the file path supplied)
        ;
        intern code   ; Bind the user script
        catch/quit [
            ;
            ; The source string may have been mutable or immutable, but the
            ; loaded code is not locked for this case.  So this works:
            ;
            ;     do "append {abc} {de}"
            ;
            result': meta eval code ;-- !!! pass args implicitly?
        ] then :finalizer/quit
    ] else [
        ; Make the new script object
        original-script: system/script  ; and save old one
        system/script: make system/standard/script [
            title: select opt hdr 'title
            header: hdr
            parent: :original-script
            path: dir
            args: arg
        ]

        if set? 'script-pre-load-hook [
            script-pre-load-hook is-module hdr ;-- chance to print it out
        ]

        ; Eval the block or make the module, returned
        either is-module [ ; Import the module and set the var
            catch/quit [
                result': meta import module hdr code
            ] then :finalizer/quit
        ][
            intern code   ; Bind the user script
            catch/quit [
                result': meta eval code
            ] then :finalizer/quit
        ]
    ]

    return finalizer result'
]

export: func [
    "Low level export of values (e.g. functions) to lib."
    return: [~]
    words [block!] "Block of words (already defined in local context)"
][
    for-each word words [append lib reduce [word get word]]
]
