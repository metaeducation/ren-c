Rebol [
    system: "Rebol [R3] Language Interpreter and Run-time Environment"
    title: "REBOL 3 Boot Sys: Top Context Functions"
    rights: --[
        Copyright 2012 REBOL Technologies
        REBOL is a trademark of REBOL Technologies
    ]--
    license: --[
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    ]--
    context: sys
    notes: --[
        Follows the BASE lib init that provides a basic set of functions
        to be able to evaluate this code.

        The boot binding of this module is SYS then LIB deep.
        Any non-local words not found in those contexts WILL BE
        UNBOUND and will error out at runtime!
    ]--
]


; If the host wants to know if a script or module is loaded, e.g. to print out
; a message.  (Printing directly from this code would be presumptuous.)
;
; !!! This is not null but trash because it is risky to have variables meant to
; hold functions be NULL since they act as no-ops.
;
script-pre-load-hook: ~

; NOTE: we are in SYS context here!

enrecover: lib.enrecover/
recover: enclose enrecover/ lambda [f] [
    match error! eval-free f
]

unprotect $lib.enrecover
lib.enrecover: ~<See SYS.UTIL/ENRECOVER and https://forum.rebol.info/t/1871>~

set extend lib 'recover (
    ~<See SYS.UTIL/RECOVER and https://forum.rebol.info/t/1871>~
)

exit: lib.exit-process/

unprotect $lib.exit-process
lib.exit-process: ~<See SYS.UTIL/EXIT>~


; 1. !!! The product is the secondary return from running the module.  This is
;    not necessarily right, because it means that if the module did (quit 1)
;    it probably means the module was not successfully made and you should
;    be getting an error.  This needs to be rethought.
;
/module: func [
    "Creates a new module (used by both IMPORT and DO)"

    return: [
        ~(module! any-value?)~
        "Module and result (may be FAILURE!) of running the body"
    ]
    spec "The header block of the module (modified)"
        [<opt> block! object!]
    body "The body of the module"
        [block!]
    :mixin "Bind body to this additional object before executing"
        [object!]
    :into "Add data to existing MODULE! context (vs making a new one)"
        [module!]
    {
        mod  ; note: overwrites MODULO shorthand in this function
        ^product
    }
][
    mod: any [
        into
        make module! body  ; inherits binding from body, does not run it
    ]

    if block? cond spec [  ; turn spec into an object if it was a block
        spec: construct:with (pin spec) system.standard.header  ; see def.
    ]

  ;----
  ; 1. !!! Should non-Modules be prohibited from having exports?  Or just
  ;    `type: script` be prohibited?  It could be the most flexible answer is
  ;    that IMPORT works on anything that has a list of exports, which allows
  ;    designing new kinds like `type: supermodule`, but such ideas have not
  ;    been mapped out.

    if spec [  ; validate the important fields of the header, if there is one
        for-each [var types] [
            spec.name [<null> word! tuple!]
            spec.type [word!]  ; `script` default from system.standard.header
            spec.version [<null> tuple!]
            spec.options [<null> block!]
        ][
            if not typecheck types get var [
                panic ["Module" @var "must be" @types "not" @(reify get var)]
            ]
        ]

        all [
            spec.type = 'module
            not has spec 'exports
        ] then [  ; default to having exports block if it's a module [1]
            extend spec [exports: make block! 10]
        ]

        set-adjunct mod spec
    ]

  ;----
  ; 1. Add importing and exporting as specializations of lower-level IMPORT*
  ;    and EXPORT* functions.  (Those are still available if you ever want to
  ;    specify a "where".)
  ;
  ;    If you don't want these, there could be a refinement to MODULE that
  ;    would omit them.  But since MODULE is not that complex to write,
  ;    probably better to have such cases use MAKE MODULE! instead of MODULE.

    set extend mod 'import cascade [
        specialize sys.util.import*/ [  ; specialize low-level [1]
            where: mod
        ]
        decay/  ; don't want body evaluative result
    ]

    set extend mod 'export (
        if spec and (spec.type = 'module) [  ; only modules export
            specialize sys.util.export*/ [  ; specialize low-level [3]
                where: mod
            ]
        ] else [
            ~<Scripts must be invoked via IMPORT to get EXPORT>~
        ]
    )

    if object? cond mixin [body: bind mixin body]

  ;----
  ; 1. We add a QUIT slot to the module (alongside IMPORT* and EXPORT*), and
  ;    fill it in with a custom THROW word to catch it.  MAKE-QUIT gives us a
  ;    function that interprets non-zero integers e.g. (quit 1) as FAILURE!
  ;
  ; 2. If there's no explicit QUIT or QUIT* call in the body code, we do what
  ;    (quit) with no arguments would do... generate an ~okay~ signal.
  ;
  ;    !!! We might consider having this run the module's QUIT with 0 instead,
  ;    so that an ADAPT'ed QUIT would always get a quit signal.  But this has
  ;    not been done with RETURN for FUNCTION, so it's a controversial idea.

    ignore ^product: catch [  ; IGNORE stops FAILURE! results becoming a panic
        set (extend mod 'quit*) throw/  ; module's QUIT* [1]

        wrap* mod body  ; add top-level declarations to module
        body: bindable body  ; MOD inherited body's binding, we rebind
        eval inside mod body  ; ignore body result, only QUIT returns value
        throw ~okay~  ; same behavior as QUIT 0
    ]

    mod.quit*: ~<Module finished init, QUIT* expired (see SYS.UTIL/EXIT)>~

    return pack [mod ^product]  ; FAILURE! antiform is legal product
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
;   script, and restoring the prior path on completion of panic
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
/do: func [
    "Execution facility for Rebol or other Languages/Dialects (see also: EVAL)"

    return: [any-value?]
    source "Files interpreted based on extension, dialects based on 'kind'"
        [
            text!  ; source code with header
            blob!  ; treated as UTF-8, same interpretation as text
            url!  ; load code from URL via protocol
            file!  ; load from file relative to OS current directory
            tag!  ; load relative to system.script.name
            @word!  ; module name (URL! looked up from table)
        ]
    :args "Args passed as system.script.args to a script (normally a string)"
        [element?]
][
    return [_ _ {^}]: import*:args () source args
]
