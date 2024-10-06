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

enrescue: lib/enrescue/
lib.enrescue: ~  ; forcing long name of SYS.UTIL/ENRESCUE hints it is dangerous

; Returns NULL if no error, otherwise the error
;
rescue: enclose enrescue/ lambda [f] [
    match error! eval f
]

exit: lib/exit/
lib.exit: ~  ; forcing long name of SYS.UTIL/EXIT


; 1. The console exits to the shell, and hence really only needs the exit code
;    (vs. a script quit that can return any value or raised error).  It's
;    easier to do the work here than in API strings in the console C code.
;
; 2. Allowing QUIT to run without an argument opens up the possibility of
;    someone putting a QUIT statement in the middle of code and not thinking
;    it takes any arguments, but it picks one up from the next line.  Only
;    the console's QUIT does this--for convenience.  All other places must
;    write out the "long" form of (quit 0)
;
make-quit: lambda [
    "Make a quit function out of a plain THROW"
    quit* [action?]
    /console "Just integer, no argument acts like quit 0"  ; [1]
][
    func compose:deep [
        ^result "If not /value, integer! exit code (non-zero is failure)"
            [any-atom? (if console [<end>])]  ; endability has pitfalls [2]
        /value "Return any value, non-raised are exit code 0"
    ][
        result: default [just '0]
        if value [  ; not an exit status integer
            if not console [
                quit* unmeta result  ; may be raised error
            ]
            quit* any [
                if unraised? unmeta result [0]  ; non-raised is shell success
                try (unquasi result).exit-code  ; null if no exit-code field
                1  ; generic quit signal for all non-exit-code-bearing errors
            ]
        ]
        let exit-code: unmeta result
        if not integer? exit-code [
            fail // [
                "QUIT without :VALUE accepts INTEGER! exit status only"
                :blame $result
            ]
        ]
        quit* any [
            if console [exit-code]  ; console gives code to shell, not to DO
            if exit-code = 0 [~]  ; suppresses display when given back to DO
        ] else [
            raise make error! compose [  ; give definitional error back to DO
                message: [
                   "Process returned non-zero exit code:" exit-code
               ]
               exit-code: (exit-code)
            ]
        ]
    ]
]

; 1. !!! The product is the secondary return from running the module.  This is
;    not necessarily right, because it means that if the module did (quit 1)
;    it probably means the module was not successfully made and you should
;    be getting a raised error.  This needs to be rethought.
;
; 2. !!! Should non-Modules be prohibited from having Exports?  Or just
;    `Type: Script` be prohibited?  It could be the most flexible answer is
;    that IMPORT works on anything that has a list of Exports, which would let
;    people design new kinds like `Type: 'SuperModule`, but such ideas have not
;    been mapped out.
;
; 3. We add importing and exporting as specializations of lower-level IMPORT*
;    and EXPORT* functions.  (Those are still available if you ever want to
;    specify a "where".)
;
;    If you don't want these added, there could be a refinement to MODULE that
;    would omit them.  But since MODULE is not that complex to write,
;    probably better to have such cases use MAKE MODULE! instead of MODULE.
;
; 4. We add a QUIT slot to the module (alongside like IMPORT* and EXPORT*), and
;    fill it in with a custom THROW word to catch it.  If we don't catch it,
;    the result is just set to be NOTHING.
;
module: func [
    "Creates a new module (used by both IMPORT and DO)"

    return: "Module and meta-result of running the body (may be raised)"  ; [1]
        [~[module! any-atom?]~]
    spec "The header block of the module (modified)"
        [~void~ block! object!]
    body "The body of the module"
        [block!]
    /mixin "Bind body to this additional object before executing"
        [object!]
    /into "Add data to existing MODULE! context (vs making a new one)"
        [module!]
    <local>
        mod product'  ; note: overwrites MODULO shorthand in this function
][
    if void? spec [spec: null]  ; safer to use void on interface (blank?)

    mod: any [
        into
        make module! body  ; inherits binding from body, does not run it
    ]

    if block? spec [  ; turn spec into an object if it was a block
        comment [
            unbind:deep spec  ; !!! preserve binding?
        ]
        spec: construct:with (inert spec) system.standard.header  ; see def.
    ]

    if spec [  ; validate the important fields of the header, if there is one
        for-each [var types] [
            spec.name [~null~ word!]
            spec.type [word!]  ; `script` default from system.standard.header
            spec.version [~null~ tuple!]
            spec.options [~null~ block!]
        ][
            if not (match:meta inside [] types get inside [] var) [
                fail ["Module" var "must be in" mold types "- not" ^(get var)]
            ]
        ]

        all [
            spec.type = 'Module
            not has spec 'Exports
        ] then [  ; default to having Exports block if it's a module [2]
            append spec spread compose [Exports: (make block! 10)]
        ]

        set-adjunct mod spec
    ]

    append mod 'import
    mod.import: cascade [
        specialize sys.util/import*/ [  ; specialize low-level [3]
            where: mod
        ]
        :decay  ; don't want body evaluative result
    ]

    append mod 'export
    mod.export: if spec and (spec.type = 'Module) [  ; only modules export
        specialize sys.util/export*/ [  ; specialize low-level [3]
            where: mod
        ]
    ] else [
        specialize fail/ [reason: [
            "Scripts must be invoked via IMPORT to get EXPORT, not DO:"
            (file else ["<was run as text!/binary!>"])
        ]]
    ]

    if object? mixin [bind body mixin]

    trap [  ; !!! currently `then x -> [...] except e -> [...]` is broken
        catch* 'quit [  ; fill in definitional QUIT slot in module [4]
            append mod 'quit
            mod.quit: make-quit :quit

            wrap* mod body  ; add top-level declarations to module
            body: bindable body  ; MOD inherited body's binding, we rebind
            eval inside mod body  ; ignore body result, only QUIT returns value
            quit ~  ; this is the "THROW" to the customized CATCH* above
        ]
        then ^arg-to-quit -> [  ; !!! should THEN with ^META get errors?
            product': arg-to-quit  ; meta convention used for product
        ]
    ] then error -> [
        product': quasi error
    ]

    mod.quit: func [atom] [
        fail // [
            "Module finished init, no QUIT (do you want SYS.UTIL/EXIT?)"
            :blame $atom
        ]
    ]

    return pack* [mod (unmeta product')]  ; pack* for raised error
]


; DO is aiming to be polymorphic, to run things like JavaScript.  It always
; requires a header (or an implied header from filename/location).  It will
; support blocks at one point, but not in the historical way.
;
;   https://forum.rebol.info/t/polyglot-polymorphic-do/1846
;
; Modern calls for evaluation services on BLOCK! etc. should use EVAL.
;
; For the moment, common features of DO and IMPORT are implemented in the
; IMPORT* command.  This includes:
;
; * Changing the working directory to match the file path (or URL "path") of a
;   script, and restoring the prior path on completion of failure
;
; * Isolating the executed code into a MODULE! so that it doesn't leak into
;   the caller's context.
;
; * Turning @xxx identified script names to DO into a URL by looking it up in
;   the modules library.  So `do @chess` and `import @json` use the same logic
;   to figure out where that name points.
;
; * Setting system.script to reflect the executing script during its run, and
;   then putting it back when control is returned to the caller.
;
; * Adding specialized IMPORT, EXPORT, and INTERN definitions that know where
;   the importing and exporting and interning needs to be done, e.g. into the
;   containing context.  (Lower-level mechanics that want to avoid this should
;   use MAKE MODULE! instead for full control.)
;
; But the actual action taken is different: DO returns the evaluative product
; (scripts must use QUIT/VALUE to return such a product).  While IMPORT returns
; the module context the script was executed in.  Also, IMPORT is designed to
; return the same context every time it is called--so modules are loaded only
; once--while DO performs an action that you can run any number of times.
;
do: func [
    "Execution facility for Rebol or other Languages/Dialects (see also: EVAL)"

    return: "Evaluative product, or error"
        [any-value? raised?]
    source "Files interpreted based on extension, dialects based on 'kind'"
        [
            <maybe>  ; opts out of the DO, returns null
            text!  ; source code with header
            binary!  ; treated as UTF-8, same interpretation as text
            url!  ; load code from URL via protocol
            file!  ; load from file relative to OS current directory
            tag!  ; load relative to system.script.name
            the-word!  ; module name (URL! looked up from table)
        ]
    /args "Args passed as system.script.args to a script (normally a string)"
        [element?]
][
    return [_ _ @]: import*:args null source args
]
