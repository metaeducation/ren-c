; quasi.test.reb

(not quasi? 1)

(
    v: make quasi! 'labeled
    did all [
        quasi? v
        '~labeled~ = v
    ]
)

; QUASI!s were initially neither true nor false, but this came to be the
; role of the isotopic forms.
;
(did first [~()~])

(
    valid: ["~abc~" "~a|b~"]
    for-each str valid [
        word: parse str [to-word/ between '~ '~]
        bad: load-value str
        assert [quasi? bad]
        assert [word = unquasi bad]
        isotope: ^ do str
        assert [bad = isotope]
    ]
    true
)

(
    invalid: ["~~" "~~~" "~a" "~~~a"]
    for-each str invalid [
        load-value str except e -> [
            assert [e.id = 'scan-invalid]
        ]
    ]
    true
)


; https://forum.rebol.info/t/what-should-do-do/1426
;
(void? do [])
(
    x: <overwritten>
    did all [
        void' = ^ x: do []
        void' = ^x
    ]
)
(
    x: 10
    did all [
        void' = ^ x: eval []
        void? :x
    ]
)
(
    x: 10
    did all [
        10 maybe x: eval []
        void? :x
    ]
)

[
    (foo: lambda [] [], true)

    (void? foo)

    (void' = ^ applique :foo [])
    (void? applique :foo [])

    (void' = ^ eval :foo)
    (void? eval :foo)

    (void? do :foo)
]

[
    (foo: func [] [], true)

    (none? foo)

    (none' = ^ applique :foo [])
    (none? applique :foo [])

    (none' = ^ eval :foo)
    (none? eval :foo)

    (none' = ^ do :foo)
]

; Explicit return of VOID is also invisible
[
    (did foo: func [return: [<opt> <void> any-value!]] [return void])

    (void? foo)
    (void' = ^ foo)

    (3 = (1 + 2 foo))
]

; Not providing an argument is an error (too easy to pick up random arguments
; from another line if 0-arity were allowed)
[
    (did foo: func [return: [<opt> <void> any-value!] x] [return none])

    ~no-arg~ !! (foo)
]

; !!! Should the ~()~ isotope be usable as a synonym for VOID?
[
    (did foo: func [return: [<opt> <void> any-value!]] [return ~()~])

    (not void? foo)
    ('~()~ = ^ foo)

    ('~()~ = ^ applique :foo [])
    ('~()~ = ^ do :foo)
]


[(
    foo: func [return: <none>] []
    none' = ^ foo
)(
    data: [a b c]
    f: func [return: <none>] [append data spread [1 2 3]]
    none' = ^ f
)]

; locals are unset before they are assigned
(
    f: func [<local> loc] [return get/any 'loc]
    none? f
)(
    f: func [<local> loc] [return reify get/any 'loc]
    f = '~
)(
    f: func [<local> loc] [return ^loc]
    f = '~
)(
    f: lambda [<local> loc] [^loc]
    f = '~
)


; Genuine unbound words exist (e.g. product of MAKE WORD!) but there are also
; words that are attached to a context, but have no definition in that context
; or anything it inherits from.  These have never had an "emerge" operation
; (at time of writing, just assigning a SET-WORD! with the binding is enough
; to do such an emergence, though something like a JavaScript strict mode
; would demand some kind of prior declaration of intent to use the name).
;
(did all [
    e: sys.util.rescue [get/any 'asiieiajiaosdfbjakbsjxbjkchasdf]
    e.id = 'unassigned-attach
    e.arg1 = 'asiieiajiaosdfbjakbsjxbjkchasdf
])

; MATCH will match a quasi-word! as-is, but falsey inputs produce isotopes
[
    (''~preserved~ = ^ match quasi-word! '~preserved~)
    ('~[~null~]~ = ^ match null null)
]

; ~quit~ is the label of the QUASI-WORD! isotope you get by default from QUIT.
; If the result is meant to be used, then QUIT should be passed an argument,
; but the idea is to help draw attention to when a script was cut short
; prematurely via a QUIT command.  Isotopes may be passed.
;
; Note: DO of BLOCK! does not catch quits, so TEXT! is used here.
[
    (1 = do "quit/with 1")
    ('~quit~ =  ^ do "quit")
    ('~isotope~ = ^ do "quit/with ~isotope~")
    ('~plain~ = do "quit/with '~plain~")
]

; Isotopes make it easier to write generic routines that handle QUASI-WORD!
; values, so long as they are "friendly" (e.g. come from picking out of a
; block vs. running it, or come from a quote evaluation).
;
([~abc~ ~def~] = collect [keep spread [~abc~], keep '~def~])

; Erroring modes of QUASI-WORD! are being fetched by WORD! and logic tests.
; They are inert values otherwise, so PARSE should treat them such.
;
; !!! Review: PARSE should probably error on rules like `some ~foo~`, and
; there needs to be a mechanism to indicate that it's okay for a rule to
; literally match something that's not set vs. be a typo.
;
(did parse3 [~foo~ ~foo~] [some '~foo~])  ; acceptable
(did parse3 [~foo~ ~foo~] [some ~foo~])  ; !!! shady, rethink
(
    foo: '~foo~
    did parse3 [~foo~ ~foo~] [some foo]
)
~bad-word-get~ !! (
    foo: ~foo~
    parse3 [~foo~ ~foo~] [some foo]
)

[#68 https://github.com/metaeducation/ren-c/issues/876
    ~need-non-end~ !! (
        a:
    )
    (
        a: 1020
        did all [
            void? a: ()
            void? :a
        ]
    )
]


(
    a: ~bad~
    ^a = '~bad~
)
(not error? trap [set 'a '~bad~])


; CONCRETIZE is used to make isotopes into the non-isotope form, pass through
; all other values.
[
    ('foo = concretize ~foo~)
    ('~null~ = reify null)
    ('~null~ = reify ~null~)

    (10 = reify 10)
    ((the '''a) = reify the '''a)
]

; UNMETA* works on void, but not other "isotopic" forms as a trick
[
    (void? unmeta* void)
    ~bad-isotope~ !! (
        unmeta* ~foo~
    )
]

; Special transcode cases (review: should be part of larger table-driven
; approach)
[
    (all [
        x: transcode/one "~@~"
        quasi? x
        '@ = unquasi x
    ])
    (all [
        x: transcode/one "~^^~"
        quasi? x
        '^ = unquasi x
    ])
]
