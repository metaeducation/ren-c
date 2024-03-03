REBOL [
    Title: "Shim to bring old executables up to date to use for bootstrapping"
    Rights: {
        Rebol 3 Language Interpreter and Run-time Environment
        "Ren-C" branch @ https://github.com/metaeducation/ren-c

        Copyright 2012-2018 Rebol Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Purpose: {
        Ren-C "officially" supports two executables for doing a bootstrap
        build.  One is a frozen "stable" version (`8994d23`) which was
        committed circa Dec-2018:

        https://github.com/metaeducation/ren-c/commit/dcc4cd03796ba2a422310b535cf01d2d11e545af

        The only other executable that is guaranteed to work is the *current*
        build.  This is ensured by doing a two-step build in the continuous
        integration, where 8994d23 is used to make the first one, and then
        the build is started over using that product.

        This shim is for 8994d23, in order to bring it up to compatibility
        for any new features used in the bootstrap code that were introduced
        since it was created.  This is facilitated by Ren-C's compositional
        operations, like ADAPT, CHAIN, SPECIALIZE, and ENCLOSE.
    }
    Usage: {
        1. This should be called like:

          (change-dir do join copy system/script/path %bootstrap-shim.r)

        The reason is that historical Rebol would change the working directory
        to match the script directory, while modern ones do not.  This is
        an incantation which makes it clear that the directory may be changed
        by running the shim (to SYSTEM/OPTIONS/PATH)...if it wasn't a new
        version that was set to that already.

        Also important is JOIN COPY, because some older Ren-Cs had a mutating
        JOIN...and this one has to run before the shim makes those versions
        of JOIN do copying.
    }
]

read: lib/read: adapt 'lib/read [
    ;
    ; !!! This can be useful in build 8994d23 to get better error messages
    ; about where a bad read is reading from (fixed in R3C and later)
    ;
    ; if not port? source [print ["READING:" mold source "from" what-dir]]
]

; 2. Some routines in r3-8994d23 treat NULL as opt out (e.g. APPEND, COMPOSE)
; while others treat BLANK! like an opt out (e.g. FIND, TO-WORD).  These have
; been consolidated to all take VOID to opt out in modern Ren-C, and MAYBE is
; consolidated to just taking NULL and making VOID.
;
; For bootstrap purposes here, use MAYBE+ to make blank opt-outs and MAYBE-
; to make null opt-outs.  The newer executable produces voids for both.
;
maybe: func [] [fail/where "MAYBE+ => blank or MAYBE- => null" 'return]


; The snapshotted Ren-C existed right before <maybe> was legal to mark an
; argument as meaning a function returns null if that argument is blank.
; See if this causes an error, and if so assume it's the old Ren-C, not a
; new one...?
;
; What this really means is that we are only catering the shim code to the
; snapshot.  (It would be possible to rig up shim code for pretty much any
; specific other version if push came to shove, but it would be work for no
; obvious reward.)
;
trap [
    func [i [<maybe> integer!]] [...]
] else [
    nulled?: func [var [word! path!]] [return null = get var]
    null-to-blank: func [x [<opt> any-value!]] [either null? :x [_] [:x]]

    maybe-: maybe+: func [x [<opt> any-value!]] [  ; see [2]
        either any [blank? :x null? :x] [void] [:x]
    ]

    quit/with system/options/path  ; see [1]
]

print "== SHIMMING OLDER R3 TO MODERN LANGUAGE DEFINITIONS =="

; "WORKAROUND FOR BUGGY PRINT IN BOOTSTRAP EXECUTABLE"
;
; Commit #8994d23 circa Dec 2018 has sporadic problems printing large chunks
; (in certain mediums, e.g. to the VSCode integrated terminal).  Replace PRINT
; as early as possible in the boot process with one that uses smaller chunks.
; This seems to avoid the issue.
;
prin3-buggy: :lib/prin
print: lib/print: lib/func [value <local> pos] [
    if value = newline [  ; new: allow newline, to mean print newline only
        prin3-buggy newline
        return
    ]
    value: spaced value  ; uses bootstrap shim spaced (once available)
    while [true] [
        prin3-buggy copy/part value 256
        if tail? value: skip value 256 [break]
    ]
    prin3-buggy newline
]

; The bootstrap executable was picked without noticing it had an issue with
; reporting errors on file READ where it wouldn't tell you what file it was
; trying to READ.  It has been fixed, but won't be fixed until a new bootstrap
; executable is picked--which might be a while since UTF-8 Everywhere has to
; stabilize and speed up.
;
; So augment the READ with a bit more information.
;
lib-read: copy :lib/read
lib/read: read: enclose :lib-read function [f [frame!]] [
    saved-source: :f/source
    if e: trap [bin: do f] [
        parse e/message [
            [
                {The system cannot find the } ["file" | "path"] { specified.}
                | "No such file or directory"  ; Linux
            ]
            to end
        ] then [
            fail/where ["READ could not find file" saved-source] 'f
        ]
        print "Some READ error besides FILE-NOT-FOUND?"
        fail e
    ]
    bin
]

maybe+: :try  ; see [2]
maybe-: func [x [<opt> any-value!]] [either blank? :x [null] [:x]]

null-to-blank: :try  ; if we put null in variables, word accesses will fail
try: func [] [fail/where "Use MAYBE instead of TRY for bootstrap" 'return]
opt: func [] [fail/where "Use REIFY instead of OPT for bootstrap" 'return]

trash: :void
trash!: :void!
void!: <opt>
void: :null

reify: func [value [<opt> any-value!]] [
    case [
        void? :value [return '~trash~]
        ; there is no actual "void" type, null acts as void sometimes
        null? :value [return '~null~]
    ]
    return :value
]

degrade: func [value [any-value!]] [
    case [
        '~trash~ = :value [return void]
        '~void~ = :value [return null]  ; append [a b c] null is no-op
        '~null~ = :value [return null]
    ]
    return :value
]

; New interpreters do not change the working directory when a script is
; executed to the directory of the script.  This is in line with what most
; other languages do.  Because this makes it more difficult to run helper
; scripts relative to the script's directory, `do <subdir/script.r>` is used
; with a TAG! to mean "run relative to system/script/header".
;
; To subvert the change-to-directory-of-script behavior, we have to LOAD the
; script and then DO it, so that DO does not receive a FILE!.  But this means
; we have to manually update the system/script object.
;
do: enclose :lib/do func [f <local> old-system-script] [
    old-system-script: _
    if tag? :f/source [
        f/source: append copy system/script/path to text! f/source
        f/source: clean-path f/source
        old-system-script: system/script
        system/script: construct system/standard/script [
            title: spaced ["Bootstrap Shim DO LOAD of:" f/source]
            header: compose [File: f/source]
            parent: system/script
            path: split-path f/source
            args: f/args
        ]
        f/source: load f/source  ; avoid dir-changing mechanic of DO FILE!
    ]
    lib/do f  ; avoid SET/ANY vs SET/OPT problem by using ELIDE
    elide if old-system-script[
        system/script: old-system-script
    ]
]
load: func [source /all /header] [  ; can't ENCLOSE, does not take TAG!
    if tag? source [
        source: append copy system/script/path to text! source
    ]
    case [
        header [
            assert [not all]
            lib/load/header source
        ]
        all [lib/load/all source]
        true [lib/load source]
    ]
]

; Raise errors by default if mistmatch or not end of input.
; PARSE/MATCH switches in a mode to give you the input, or null on failure.
; Result is given as void to prepare for the arbitrary synthesized result.
;
parse: func [input rules /case /match] [
    f: make frame! :lib/parse
    f/input: input
    f/rules: rules
    f/case: case
    if match [
        return either do f [input] [null]
    ]
    do f else [
        probe rules
        fail "Error: PARSE rules did not match (or did not reach end)"
    ]
    return void
]

; Older Ren-C considers nulled variables to be "unset".
;
nulled?: :unset?

; The "real apply" hasn't really been designed, but it would be able to mix
; positional arguments with named ones.  This is changed by the nature of
; "refinements are their arguments" to empower more clean options.  What Ren-C
; originally called its APPLY is thus moved to the weird name APPLIQUE, that
; had previously been taken for compatibility apply (now REDBOL-APPLY)
;
applique: :apply
unset 'apply

the: :quote
quote: func [] [fail/where "Use THE instead of QUOTE for literalizing" 'return]

collect*: :collect
collect: :collect-block

modernize-action: function [
    "Account for the <maybe> annotation as a usermode feature"
    return: [block!]
    spec [block!]
    body [block!]
][
    blankers: copy []
    spec: collect [
        iterate spec [
            ;
            ; Find ANY-WORD!s (args/locals)
            ;
            if keep w: match any-word! spec/1 [
                ;
                ; Feed through any TEXT!s following the ANY-WORD!
                ;
                while [if (tail? spec: my next) [break] | text? spec/1] [
                    keep/only spec/1
                ]

                ; Substitute BLANK! for any <maybe> found, and save some code
                ; to inject for that parameter to return null if it's blank
                ;
                if find (maybe+ match block! spec/1) <maybe> [
                    keep/only replace copy spec/1 <maybe> 'blank!
                    append blankers compose [
                        if blank? (as get-word! w) [return null]
                    ]
                    continue
                ]
            ]
            keep/only spec/1
        ]
    ]
    body: compose [
        (blankers)
        (as group! body)
    ]
    return reduce [spec body]
]

func: adapt 'func [set [spec body] modernize-action spec body]
function: adapt 'function [set [spec body] modernize-action spec body]

meth: enfix adapt 'meth [set [spec body] modernize-action spec body]
method: enfix adapt 'method [set [spec body] modernize-action spec body]

trim: adapt 'trim [ ; there's a bug in TRIM/AUTO in 8994d23
    if auto [
        while [not tail? series and [series/1 = LF]] [
            take series
        ]
    ]
]

transcode: lib/function [
    return: [<opt> any-value!]
    source [text! binary!]
    /next
    next-arg [any-word!]
][
    values: lib/transcode/(either next ['next] [_])
        either text? source [to binary! source] [source]
    pos: take/last values
    assert [binary? pos]

    if next [
        assert [1 >= length of values]

        ; In order to return a text position in pre-UTF-8 everywhere, fake it
        ; by seeing how much binary was consumed and assume skipping that many
        ; bytes will sync us.  (From @rgchris's LOAD-NEXT).
        ;
        if text? source [
            rest: to text! pos
            pos: skip source subtract (length of source) (length of rest)
        ]
        set next-arg pos
        return pick values 1  ; may be null
    ]

    return values
]

split-path: lib/func [
    "Splits and returns directory component, variable for file optionally set"
    return: [<opt> file!]
    location [<opt> file! url! text!]
    /file  ; no multi-return, simulate it
        farg [any-word! any-path!]
    <local> pos dir
][
    if null? :location [return null]
    pos: _
    parse location [
        [#"/" | 1 2 #"." opt #"/"] end (dir: dirize location) |
        pos: any [thru #"/" [end | pos:]] (
            all [
                empty? dir: copy/part location at head of location index of pos
                    |
                dir: %./
            ]
            all [find [%. %..] pos: to file! pos insert tail of pos #"/"]
        )
        to end  ; !!! was plain END, but was unchecked and didn't reach it!
    ]
    if :farg [
        set farg pos
    ]
    return maybe- dir
]

join: :join-of  ; Note: JOIN now for strings and paths only (not arrays)

quit/with system/options/path  ; see [1]
