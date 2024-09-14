REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "REBOL 3 Boot Sys: Top Context Functions"
    Rights: {
        Copyright 2012 REBOL Technologies
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Context: sys
    Note: {
        Follows the BASE lib init that provides a basic set of functions
        to be able to evaluate this code.

        The boot binding of this module is SYS then LIB deep.
        Any non-local words not found in those contexts WILL BE
        UNBOUND and will error out at runtime!
    }
]


; If the host wants to know if a script or module is loaded, e.g. to print out
; a message.  (Printing directly from this code would be presumptuous.)
;
; !!! This is not null but is unset because it is risky to have variables
; meant to hold functions be NULL or BLANK! as they act as no-ops.
;
script-pre-load-hook: ~

enrescue: get $lib/enrescue
lib.enrescue: ~  ; forcing long name of SYS.UTIL/ENRESCUE hints it is dangerous

; Returns NULL if no error, otherwise the error
;
rescue: enclose get $enrescue func [f [frame!]] [
    return match error! eval f
]

exit: get $lib/exit
lib.exit: ~  ; forcing long name of SYS.UTIL/EXIT

module: func [
    {Creates a new module}

    return: [module!]
    @product "The result of running the body (~quit~ antiform if it ran QUIT)"
        [any-value?]
    @quitting "If requested and quitting, when yes PRODUCT is QUIT's argument"
        [yesno?]
    spec "The header block of the module (modified)"
        [~null~ block! object!]
    body "The body of the module"
        [block!]
    /mixin "Bind body to this additional object before executing"
        [object!]
    /into "Add data to existing MODULE! context (vs making a new one)"
        [module!]
    <local>
        mod  ; note: overwrites MODULO shorthand in this function
][
    mod: any [
        into
        make module! body  ; inherits specifier from body, does not run it
    ]
    body: inside mod bindable body

    ; Turn spec into an OBJECT! if it was a block.  See system.standard.header
    ; for a description of the fields and benefits of using a standard object.
    ;
    if block? spec [
        ;; !!! unbind/deep spec
        spec: construct/with (inert spec) system.standard.header
    ]

    if spec [  ; Validate the important fields of the header, if there is one
        ;
        ; !!! Historically, the `Name:` and `Type:` fields would tolerate
        ; either a quoted word or plain word.  Now only WORD! is tolerated.
        ;
        for-each [var types] [  ; !!! `has spec` doesn't work here, why not?
            spec.name [~null~ word!]
            spec.type [word!]  ; default is `script` from system.standard.header
            spec.version [~null~ tuple!]
            spec.options [~null~ block!]
        ][
            if not (match/meta inside [] types get inside [] var) [
                fail ["Module" var "must be in" mold types "- not" ^(get var)]
            ]
        ]

        ; Default to having an Exports block in the spec if it's a module.
        ;
        ; !!! Should non-Modules be prohibited from having Exports?  Or just
        ; `Type: Script` be prohibited?  It could be the most flexible answer
        ; is that IMPORT works on anything that has a list of Exports, which
        ; would let people design new kinds like `Type: 'SuperModule`, but
        ; such ideas have not been mapped out.
        ;
        all [
            spec.type = 'Module
            not has spec 'Exports
        ] then [
            append spec spread compose [Exports: (make block! 10)]
        ]

        set-adjunct mod spec
    ]

    ; We add importing and exporting as specializations of lower-level IMPORT*
    ; and EXPORT* functions.  (Those are still available if you ever want to
    ; specify a "where".)
    ;
    ; If you don't want these added, there could be a refinement to MODULE that
    ; would omit them.  But since MODULE is not that complex to write,
    ; probably better to have such cases use MAKE MODULE! instead of MODULE.
    ;
    append mod 'import
    mod.import: specialize get $sys.util/import* [where: mod]

    ; If you DO a file, it doesn't get an EXPORT operation...only modules.
    ;
    append mod 'export
    mod.export: if spec and (spec.type = 'Module) [
        specialize get $sys.util/export* [where: mod]
    ] else [
        specialize get $fail [reason: [
            {Scripts must be invoked via IMPORT to get EXPORT, not DO:}
            (file else ["<was run as text!/binary!>"])
        ]]
    ]

    if object? mixin [bind body mixin]

    ; We need to catch the quit here, because if we do not we cannot return
    ; a module that we created.  The caller expects to get a module back even
    ; if that module's init code decided to QUIT to end processing prematurely.
    ; (QUIT is not a failure when running scripts.)
    ;
    catch/quit [
        ;
        ; If the body didn't get turned into a block (and is still a BINARY!
        ; or similar) then DO'ing it will cause a confusing infinite recursion.
        ; Good to notice the problem before that.
        ;
        assert [block? body]

        product: ^ eval body  ; !!! meta-convention to return PACKs?
        quitting: 'no
    ]
    then ^arg-to-quit -> [
        quitting: 'yes
        product: arg-to-quit  ; !!! meta convention?
    ]

    return mod
]


; DO delegates to this Rebol function for ANY-STRING? and BINARY! types
; (presumably because it would be laborious to express as C).
;
do*: func [
    {SYS: Called by system for DO on datatypes that require special handling}

    return: "Final evaluative product of code or block"
        [any-value?]
    source "Files, urls and modules evaluate as scripts, other strings don't"
        [file! url! text! binary! tag! the-word!]
    args "Args passed as system.script.args to a script (normally a string)"
        [~null~ element?]
    only "Do not catch quits...propagate them"
        [boolean?]
][
    ; For the moment, common features of DO and IMPORT are implemented in the
    ; IMPORT* command.  This includes:
    ;
    ; * Changing the working directory to match the file path (or URL "path")
    ;   of a script, and restoring the prior path on completion of failure
    ;
    ; * Isolating the executed code into a MODULE! so that it doesn't leak
    ;   into the caller's context.
    ;
    ; * Turning @xxx identified script names to DO into a URL by looking it
    ;   up in the modules library.  So `do @chess` and `import @json` use
    ;   the same logic to figure out where that name points.
    ;
    ; * Setting system.script to reflect the executing script during its run,
    ;   and then putting it back when control is returned to the caller.
    ;
    ; * Adding specialized IMPORT, EXPORT, and INTERN definitions that know
    ;   where the importing and exporting and interning needs to be done,
    ;   e.g. into the containing context.  (Lower-level mechanics that want
    ;   to avoid this should use MAKE MODULE! instead for full control.)
    ;
    ; But the actual action taken is different: DO returns the evaluative
    ; product of the script as its primary result, while IMPORT returns the
    ; module context the script was executed in.  And IMPORT is designed to
    ; return the same context every time it is called--so modules are loaded
    ; only once--while DO performs an action that you can run any number
    ; of times.  So what DO does is effectively flips the order of the
    ; return results of IMPORT.
    ;
    ; !!! Because DO presumably wants to be able to return stable isotopes,
    ; this is likely done backwards, as having product as a secondary result
    ; means that it has to be meta.
    ;
    return unmeta [_ @]: import*/args/only null source args true? only
]
