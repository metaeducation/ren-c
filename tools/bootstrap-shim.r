Rebol [
    title: "Shim to bring old executables up to date to use for bootstrapping"
    rights: {
        Rebol 3 Language Interpreter and Run-time Environment
        "Ren-C" branch @ https://github.com/metaeducation/ren-c

        Copyright 2012-2018 Rebol Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }
    license: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    purpose: {
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
    usage: {
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
    notes: {
     A. Some routines in r3-8994d23 treat //NULL as opt out (e.g. APPEND,
        COMPOSE) while others treat BLANK! like an opt out (e.g. FIND,
        TO-WORD).  These have been consolidated to take NOTHING to opt out in
        modern Ren-C, and MAYBE is consolidated to just taking ~null~ and
        making NOTHING.

        BLANK! acts as ~null~ in the bootstrap case.  We can shim some places,
        but others we can't (e.g. GROUP!s in path dispatch of r3-8994d23 only
        take BLANK!).  The MAYBE+ will act like MAYBE in the new executable,
        but in the old executable passes through blanks.
    }
]

read: lib/read: adapt 'lib/read [
    ;
    ; !!! This can be useful in build 8994d23 to get better error messages
    ; about where a bad read is reading from (fixed in R3C and later)
    ;
    ; if not port? source [print ["READING:" mold source "from" what-dir]]
]


; The snapshotted Ren-C existed right before <opt-out> was legal to mark an
; argument as meaning a function returns null if that argument is blank.
; See if this causes an error, and if so assume it's the old Ren-C, not a
; new one...?
;
; What this really means is that we are only catering the shim code to the
; snapshot.  (It would be possible to rig up shim code for pretty much any
; specific other version if push came to shove, but it would be work for no
; obvious reward.)
;
trap [  ; in even older bootstrap executable, this means SYS.UTIL/RESCUE
    func [i [<opt-out> integer!]] [...]
] else [
    maybe+: :maybe  ; see [A]

    parse2: :parse/redbol  ; no `pos: <here>`, etc.

    parse: func [] [
        fail/blame "Use PARSE2 in Bootstrap" 'return
    ]

    quit/value system/options/path  ; see [1]
]

print "== SHIMMING OLDER R3 TO MODERN LANGUAGE DEFINITIONS =="

; Use FUNC3 to mean old func conventions

func3: :lib/func
function3: :lib/function

if3: :lib/if
either3: :lib/either

; ALIAS DO AS EVAL
;
eval: evaluate: :lib/do

; "WORKAROUND FOR BUGGY PRINT IN BOOTSTRAP EXECUTABLE"
;
; Commit #8994d23 circa Dec 2018 has sporadic problems printing large chunks
; (in certain mediums, e.g. to the VSCode integrated terminal).  Replace PRINT
; as early as possible in the boot process with one that uses smaller chunks.
; This seems to avoid the issue.
;
prin3-buggy: :lib/prin
print: lib/print: func3 [value <local> pos] [
    if3 value = newline [  ; new: allow newline, to mean print newline only
        prin3-buggy newline
        return ~
    ]
    value: lib/spaced value  ; uses bootstrap shim spaced (once available)
    while [1] [
        prin3-buggy copy/part value 256
        if3 tail? value: lib/skip value 256 [break]
    ]
    prin3-buggy newline
]




; Use /BLAME instead of /WHERE in FAIL (eliminates an annoying inconsistency)
;
fail-with-where: :lib/fail

fail: func3 [
    reason [<end> error! text! block!]
    /blame location [frame! any-word!]
][
    if3 not reason [  ; <end> becomes null, not legal arg to fail
        reason: "<end>"
    ]
    if3 not blame [
        fail-with-where reason
    ]
    fail-with-where/where reason location
]


;; === "ENFIX => INFIX RENAMING" ===

; Because Ren-C's "infix" functions really just took the first argument from
; the left, they weren't necessarily arity-2.  So they were called "N-ary-fix"
; or "N-FIX"... and functions were said to be "enfixed".  This really just
; confused people, and it's easier to say that infix functions are only infix
; for their first two arguments...and if they have any more than that they
; will just take them normally.
;
; The bootstrap executable had a strange idea of doing infixedness through
; SET/ENFIX, and then ENFIX was itself an enfixed function which did that
; set on the word to its left.  :-/

infix: enfix :enfix

?=: enfix :equal?
=: enfix :strict-equal?
!=: enfix :strict-not-equal?
lib/do compose [(to set-word! '<>) enfix :strict-not-equal?]

==: !==: func3 [] [
    fail/blame "Don't use == or !== anymore..." 'return
]


; With definitional errors, we're moving away from the buggy practice of
; intercepting abrupt failures casually.  The RESCUE routine is put in
; SYS/UTIL/RESCUE...and that's what old-school TRAP was.
;
append sys 'util
sys/util: make object! [rescue: 1]
sys/util/rescue: :trap
trap: func3 [] [fail/blame "USE RESCUE instead of TRAP for bootstrap" 'return]


; The bootstrap executable was picked without noticing it had an issue with
; reporting errors on file READ where it wouldn't tell you what file it was
; trying to READ.  It has been fixed, but won't be fixed until a new bootstrap
; executable is picked--which might be a while since UTF-8 Everywhere has to
; stabilize and speed up.
;
; So augment the READ with a bit more information.
;
lib-read: copy :lib/read
lib/read: read: enclose :lib-read function3 [f [frame!]] [
    saved-source: :f/source
    if error? e: sys/util/rescue [bin: eval f] [
        parse2 e/message [
            [
                {The system cannot find the } ["file" | "path"] { specified.}
                | "No such file or directory"  ; Linux
            ]
            to end
        ] then [
            fail/blame ["READ could not find file" saved-source] 'f
        ]
        print ["Some READ error besides FILE-NOT-FOUND:" saved-source]
        fail e
    ]
    bin
]


; === RE-STYLE OLD VOID AS "JUNK" ===

; r3-8994d23 lacks TRIPWIRE!, so in places where we would use a tripwire
; we have to use what it called "void".  It's meaner than tripwire in some
; sense, because you can't assign it via SET-WORD!

noop: :lib/null

junk: :lib/void  ; function that returns a very ornery value
junk?: :lib/void?

junkify: func3 [x [<opt> any-value!]] [
    if3 lib/blank? :x [return junk]
    ; if3 lib/null? :x [return junk]  ; assume null is pure null
    :x
]


; === RE-STYLE //NULL AS BOOTSTRAP EXE's VOID ===

; The properties of the bootstrap executable's "// NULL" are such that it has
; to be VOID... it vanishes in COMPOSE, etc.  It also creates errors from
; access, so it's NOTHING too... but bootstrap code doesn't generally deal
; in nothing as in most code variables are simply not supposed to be unset
; if you care about them.

null3?: :lib/null?
null3: :lib/null

~: :null3  ; conflated
trash?: func [] [
    fail/blame "NOTHING? conflated with VOID? in bootstrap" 'return
]

void?: :null3?
~void~: :null3
void: :null3

void!: func3 [] [  ; MODERNIZE-ACTION should convert all these away
    fail/blame "NOTHING! converted to <opt> in bootstrap-shim" 'return
]


; === RE-STYLE BLANK! AS BOOTSTRAP EXE's ~NULL~ ===

; Because the bootstrap EXE's BLANK! is falsey, that makes it the candidate
; for serving the role of the "~null~ antiform~.  Unfortunately that needs
; a lot of work to shim in...many functions take BLANK! to "opt out" and
; that has to be converted to take the shim VOID (e.g. null3)

blank3?: :lib/blank?
blank: blank?: func3 [] [fail/blame "No BLANK in bootstrap-shim" 'return]

null: ~null~: _
null?: :blank3?

null3-to-blank3: func3 [x [<opt> any-value!]] [  ; bootstrap TRY no "voids"
    if3 null3? :x [return _]
    :x
]
blank3-to-null3: func3 [x [<opt> any-value!]] [  ; bootstrap OPT makes "voids"
    if3 blank3? :x [return null3]
    :x
]

maybe: :blank3-to-null3
maybe+: func3 [x [<opt> any-value!]] [  ; see [A]
    if3 null3? :x [fail "MAYBE+: X is BOOTSTRAP VOID (// NULL)"]
    return :x
]

all: chain [:lib/all :null3-to-blank3]
any: chain [:lib/any :null3-to-blank3]
switch: chain [:lib/switch :null3-to-blank3]
case: chain [:lib/case :null3-to-blank3]

match: specialize :lib/either-test [branch: [_]]  ; LIB/MATCH is variadic :-(
ensure: adapt :lib/ensure [
    all [
        block? test
        find test '~null~
    ] then [
        test: replace copy test '~null~ 'blank!
    ]
]

delimit: chain [:lib/delimit :null3-to-blank3]
unspaced: chain [:lib/unspaced :null3-to-blank3]
spaced: chain [:lib/spaced :null3-to-blank3]

get-env: chain [:lib/get-env :null3-to-blank3]

then: enfix enclose :lib/then func3 [f] [
    set/opt 'f/optional blank3-to-null3 :f/optional
    junkify lib/do f
]
else: enfix enclose :lib/else func3 [f] [
    set/opt 'f/optional blank3-to-null3 :f/optional
    junkify lib/do f
]

empty?: func3 [x [<opt> any-value!]] [  ; need to expand typespec for null3
    if3 blank3? :x [fail "EMPTY?: series is bootstrap NULL (BLANK!)"]
    if3 null3? :x [return okay]
    return lib/empty? :x
]
for-each: func ['var data [<opt> any-value!] body] [  ; need to take NULL3
    if3 blank3? :data [fail "FOR-EACH: data is bootstrap NULL (BLANK!)"]
    data: null3-to-blank3 :data
    lib/for-each (var) data body
]
append: adapt :lib/append [
    if3 blank3? :value [fail "APPEND: value is bootstrap NULL (BLANK!)"]
]
insert: adapt :lib/insert [
    if3 blank3? :value [fail "INSERT: value is bootstrap NULL (BLANK!)"]
]
change: adapt :lib/change [
    if3 blank3? :value [fail "CHANGE: value is bootstrap NULL (BLANK!)"]
]
join: adapt :lib/join-of [
    if3 blank3? :value [fail "JOIN: value is bootstrap NULL (BLANK!)"]
]

find: enclose :lib/find func3 [f] [  ; !!! interface won't take null
    if3 blank3? :f/series [fail "FIND: series is bootstrap NULL (BLANK!)"]
    ; do `any [arg []]` if you want to opt out of find
    return null3-to-blank3 lib/do f
]

copy: enclose :lib/copy func3 [f] [
    if3 blank3? :f/value [fail "COPY: value is bootstrap NULL (BLANK!)"]
    return null3-to-blank3 lib/do f
]

to-file: enclose :lib/to-file func3 [f] [
    if3 blank3? :f/value [fail "TO-FILE: value is bootstrap NULL (BLANK!)"]
    return null3-to-blank3 lib/do f
]

collect: adapt :lib/collect [
    body: compose [keep: enclose 'keep func [f] [
        if3 blank3? f/value [fail "KEEP: value is bootstrap NULL (BLANK!)"]
        f/value: blank3-to-null3 :f/value
        return null3-to-blank3 lib/do f
    ]]
]


; === "FLEXIBLE LOGIC": ELIMINATE TRUE AND FALSE ===

if: enclose :if3 func [f] [
    lib/all [
        :condition
        lib/find [true false yes no on off] :f/condition
        fail/blame "IF not supposed to take [true false yes no off]" 'return
    ]
    junkify lib/do f
]

either: adapt :either [
    lib/all [
        :condition
        lib/find [true false yes no on off] :condition
        fail/blame "EITHER not supposed to take [true false yes no off]" 'return
    ]
]

wordtester: infix func3 ['name [set-word!] want [word!] dont [word!]] [
    return set name func3 [x] [
        lib/case [
            :x = want [okay]
            :x = dont [null]
        ]
        fail [to word! name "expects only" mold reduce [want dont]]
    ]
]

okay: ~okay~: true
okay?: ok?: func [x] [x = lib/true]

on: true: yes: off: false: no: func [] [
    fail/blame "No ON TRUE YES OFF FALSE NO definitions any more" 'return
]

true?: wordtester 'true 'false
false?: wordtester 'false 'true
on?: wordtester 'on 'off
off?: wordtester 'off 'on
yes?: wordtester 'yes 'no
no?: wordtester 'no 'yes

logic!: make typeset! [blank! logic!]

and: or: func [] [
    fail/blame "AND/OR in preboot EXE are non-logic, weird precedence" 'return
]


; === MAKE TRY A POOR-MAN'S DEFINITIONAL ERROR HANDLER ===

try: func3 [value [<opt> any-value!]] [  ; poor man's definitional error handler
    if error? :value [return null]
    return :value
]


; === REIFY and DEGRADE parallels ===

; There's a bit of a problem here in that old-null causes errors on variable
; access -and- it vanishes when COMPOSE'd or appended.  So it is both void
; and nothing.  When it gets reified, assume the void intent is the more
; likely one that you're trying to preserve (usually it's null that the
; bootstrap reifies)

reify: func3 [value [<opt> any-value!]] [
    case [
        lib/null? :value [return '~void~]  ; bootstrap-EXE's //NULL
        okay? :value [return '~okay~]
        null? :value [return '~null~]  ; bootstrap-EXE's blank
    ]
    return :value
]

degrade: func3 [value [any-value!]] [
    case [
        '~void~ = :value [return lib/null]  ; append [a b c] null is no-op
        '~okay~ = :value [return okay]
        '~null~ = :value [return null]
    ]
    return :value
]

opt: func3 [] [fail/blame "Use DEGRADE instead of OPT for bootstrap" 'return]


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
do: enclose :lib/do func3 [f <local> old-system-script result] [
    old-system-script: null
    if tag? :f/source [
        f/source: append copy system/script/path to text! f/source
        f/source: clean-path f/source
        old-system-script: system/script
        system/script: lib/construct system/standard/script [
            title: spaced ["Bootstrap Shim DO LOAD of:" f/source]
            header: compose [File: f/source]
            parent: system/script
            path: split-path f/source
            args: f/args
        ]
        f/source: load f/source  ; avoid dir-changing mechanic of DO FILE!
    ]
    result: lib/do f
    if old-system-script[
        system/script: old-system-script
    ]
    return :result
]
load: func3 [source /all /header] [  ; can't ENCLOSE, does not take TAG!
    if tag? source [
        source: append copy system/script/path to text! source
    ]
    case [
        header [
            assert [not all]
            lib/load/header source
        ]
        all [lib/load/all source]
        okay [lib/load source]
    ]
]

; CONSTRUCT is now arity-1

construct: specialize :construct [spec: []]


; Raise errors by default if mistmatch or not end of input.
; PARSE/MATCH switches in a mode to give you the input, or null on failure.
; Result is given as void to prepare for the arbitrary synthesized result.
;
parse2: func3 [input rules /case /match] [
    f: make frame! :lib/parse
    f/input: input
    f/rules: rules
    f/case: case
    if match [
        return either eval f [input] [null]
    ]
    eval f else [
        probe rules
        fail "Error: PARSE rules did not match (or did not reach end)"
    ]
    return junk
]

parse: func3 [] [
    fail/blame "Use PARSE2 in Bootstrap" 'return
]

; The "real apply" hasn't really been designed, but it would be able to mix
; positional arguments with named ones.  This is changed by the nature of
; "refinements are their arguments" to empower more clean options.  What Ren-C
; originally called its APPLY is thus moved to the weird name APPLIQUE, that
; had previously been taken for compatibility apply (now REDBOL-APPLY)
;
applique: :apply
unset 'apply

the: :quote
quote: func3 [] [fail/blame "Use THE instead of QUOTE for literalizing" 'return]

collect*: :collect
collect: :collect-block

modernize-action: function3 [
    "Account for the <opt-out> annotation as a usermode feature"
    return: [block!]
    spec [block!]
    body [block!]
    /fallout "used for LAMBDA (otherwise produces trash on fallout)"
][
    blankers: copy []
    spec: lib/collect [
        iterate spec [
            all [
                spec/1 = the return:
                spec/2 = [~]
            ] then [
                spec: next spec
                keep [return: [<opt>]]
                continue
            ]

            ; Find ANY-WORD!s (args/locals)
            ;
            if3 keep w: lib/match any-word! spec/1 [
                ;
                ; Feed through any TEXT!s following the ANY-WORD!
                ;
                while [
                    if (tail? spec: my next) [break]
                    text? spec/1
                ][
                    keep/only spec/1
                ]

                ; Substitute BLANK! for any <opt-out> found, and save some code
                ; to inject for that parameter to return null if it's blank
                ;
                if3 block? spec/1 [
                    typespec: copy spec/1
                    if find typespec 'blank! [
                        fail "No BLANK! in bootstrap (it's acting like null)"
                    ]
                    replace typespec '~null~ blank!
                    if find typespec <undo-opt> [  ; need to turn to blank3
                        append blankers compose/deep [
                            if void? (as get-word w) [(as set-word! w) null]
                        ]
                    ]
                    if find typespec <opt-out> [
                        replace typespec <opt-out> <opt>
                        append blankers compose [
                            if void? (as get-word! w) [return null]
                        ]
                    ]
                    keep/only typespec
                    continue
                ]
            ]
            keep/only spec/1
        ]
    ]
    body: compose [
        (blankers)
        (as group! body)
        (if not fallout ['~])
    ]
    return reduce [spec body]
]

func: adapt 'func3 [set [spec body] modernize-action spec body]
lambda: adapt 'func3 [set [spec body] modernize-action/fallout spec body]

function: adapt 'function3 [set [spec body] modernize-action spec body]

meth: infix adapt 'meth [set [spec body] modernize-action spec body]
method: infix adapt 'method [set [spec body] modernize-action spec body]

trim: adapt 'trim [ ; there's a bug in TRIM/AUTO in 8994d23
    if auto [
        while [lib/all [  ; TRIM/ALL (!)
            not tail? series
            series/1 = LF
        ]][
            take series
        ]
    ]
]

replace: specialize 'replace [all: /all]

transcode: function3 [
    return: "full block or remainder if /next3, or 'definitional' error"
        [blank! block! text! binary! error!]  ; BLANK! is bootstrap null
    source [text! binary!]
    /next3
    next-arg [any-word!] "variable to set the transcoded element to"
][
    e: sys/util/rescue [  ; !!! Some weird interactions with THEN here
        values: lib/transcode/(either next3 ['next] [_])
            either text? source [to binary! source] [source]
    ]
    if error? :e [
        return e  ; poor man's definitional error
    ]

    pos: take/last values
    assert [binary? pos]

    if next3 [
        assert [1 >= length of values]

        ; In order to return a text position in pre-UTF-8 everywhere, fake it
        ; by seeing how much binary was consumed and assume skipping that many
        ; bytes will sync us.  (From @rgchris's LOAD-NEXT).
        ;
        if text? source [
            rest: to text! pos
            pos: skip source subtract (length of source) (length of rest)
        ]
        if null3? pick values 1 [
            set next-arg _  ; match modern Ren-C optional pack item
            return _
        ]
        set next-arg pick values 1
        return pos
    ]

    return values
]

split-path: func3 [
    "Splits and returns directory component, variable for file optionally set"
    return: [blank! file!]
    location [<opt> file! url! text!]
    /file  ; no multi-return, simulate it
        farg [any-word! any-path!]
    <local> pos dir
][
    if null? :location [return null]
    pos: null  ; no TRY in preboot parse
    parse2 location [
        [#"/" | 1 2 #"." opt #"/"] end (dir: dirize location) |
        opt pos: some [thru #"/" [end | pos:]] (
            all [
                empty? dir: copy/part location at head of location index of pos
                dir: %./
            ]
            all [find [%. %..] pos: to file! pos insert tail of pos #"/"]
        )
        to end  ; !!! was plain END, but was unchecked and didn't reach it!
    ]
    if :farg [
        set farg pos
    ]
    return dir
]


; High-level CALL now defaults to being synchronous
;
call: specialize :call [wait: /wait]


; === MAKE SURE OUR IF / ELSE LOGIC IS WORKING ===

; Since we're shimming blank as null, that could break things if not careful.

assert [junk? if okay [null] else [fail "This should not have run"]]
assert [1020 = (if null [null] else [1020])]

quit/with system/options/path  ; see [1]
