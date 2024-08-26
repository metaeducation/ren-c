; quasi.test.reb

(not quasi? 1)

(
    v: make quasiform! 'labeled
    all [
        quasi? v
        '~labeled~ = v
    ]
)

; quasiforms are reified values and hence conditionally truthy
;
(did first [~()~])

(
    valid: ["~abc~" "~a|b~"]
    for-each str valid [
        word: parse str [to-word/ between '~ '~]
        bad: load-value str
        assert [quasi? bad]
        assert [word = unquasi bad]
        quasiform: ^ eval reduce [bad]
        assert [bad = quasiform]
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
(void? eval [])
(
    x: <overwritten>
    all [
        void' = ^ x: eval []
        void' = ^x
    ]
)
(
    x: 10
    all [
        nihil' = x: ^ eval/undecayed []
        nihil? unmeta x
    ]
)
(
    x: 10
    10 = (x eval/undecayed [])
)

[
    (foo: lambda [] [], true)

    (void? foo)

    (void' = ^ applique :foo [])
    (void? applique :foo [])

    (void' = ^ eval :foo)
    (void? eval :foo)

    (void? eval :foo)
]

[
    (foo: func [] [], true)

    (nothing? foo)

    (nothing' = ^ applique :foo [])
    (nothing? applique :foo [])

    (nothing' = ^ eval :foo)
    (nothing? eval :foo)

    (nothing' = ^ eval :foo)
]

; Explicit return of VOID
[
    (did foo: func [return: [any-value?]] [return void])

    (void? foo)
    (void' = ^ foo)

    (void? (1 + 2 foo))
]

; Not providing an argument is an error (too easy to pick up random arguments
; from another line if 0-arity were allowed)
[
    (did foo: func [return: [any-value?] x] [])

    ~no-arg~ !! (foo)
]

; ~()~ antiforms were VOID for a short time, but void is now its own thing
[
    (did foo: func [return: [any-value?]] [return ~()~])

    (not void? foo)
    ('~()~ = ^ foo)

    ('~()~ = ^ applique :foo [])
    ('~()~ = ^ eval :foo)
]


[(
    foo: func [return: [~]] []
    nothing' = ^ foo
)(
    data: [a b c]
    f: func [return: [~]] [append data spread [1 2 3]]
    nothing' = ^ f
)]

; locals are unset before they are assigned
(
    f: func [<local> loc] [return get/any $loc]
    nothing? f
)(
    f: func [<local> loc] [return reify get/any $loc]
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
(all [
    e: sys.util.rescue [get/any $asiieiajiaosdfbjakbsjxbjkchasdf]
    e.id = 'unassigned-attach
    e.arg1 = 'asiieiajiaosdfbjakbsjxbjkchasdf
])

[
    (''~preserved~ = ^ match &quasi-word? '~preserved~)
    ('~[~null~]~ = ^ match null null)
]

; ~quit~ is the label of the quasiform you get by default from QUIT.  If the
; result is meant to be used, then QUIT should be passed an argument,
; but the idea is to help draw attention to when a script was cut short
; prematurely via a QUIT command.  Antiforms may be passed.
;
; Note: EVAL of BLOCK! does not catch quits, so TEXT! is used here.
[
    (1 = do "Rebol [] quit/with 1")
    ('~quit~ =  ^ do "Rebol [] quit")
    ('~thing~ = ^ do "Rebol [] quit/with ~thing~")
    ('~plain~ = do "Rebol [] quit/with '~plain~")
]

; Antiforms make it easier to write generic routines that handle QUASI-WORD?
; values, so long as they are "friendly" (e.g. come from picking out of a
; block vs. running it, or come from a quote evaluation).
;
([~abc~ ~def~] = collect [keep spread [~abc~], keep '~def~])

; Erroring modes of QUASI-WORD? are being fetched by WORD! and logic tests.
; They are inert values otherwise, so PARSE should treat them such.
;
; !!! Review: PARSE should probably error on rules like `some ~foo~`, and
; there needs to be a mechanism to indicate that it's okay for a rule to
; literally match something that's not set vs. be a typo.
;
(parse3 [~foo~ ~foo~] [some '~foo~], true)  ; acceptable
~???~ !! (parse3 [~foo~ ~foo~] [some ~foo~], true)  ; !!! shady, rethink
~???~ !! (
    foo: '~foo~
    parse3 [~foo~ ~foo~] [some foo]
    true
)
~bad-antiform~ !! (
    foo: ~foo~
    parse3 [~foo~ ~foo~] [some foo]
    true
)

[#68 https://github.com/metaeducation/ren-c/issues/876
    ~need-non-end~ !! (
        a:
    )
    (
        a: 1020
        all [
            void? a: (void)
            void? a
        ]
    )
    (all [
        nihil? ()  ; empty groups make nihil
        3 = (1 + 2 ())
    ])
    ~no-value~ !! (
        a: () 1 + 2  ; can't assign nihil
    )
]


(
    a: ~bad~
    ^a = '~bad~
)
(not error? trap [set $a '~bad~])


[
    ('foo = reify/unquasi ~foo~)
    ('~null~ = reify null)
    ('~null~ = reify ~null~)

    (10 = reify 10)
    ((the '''a) = reify the '''a)
]

; UNMETA* works on void, but not other "antiform" forms as a trick
[
    (void? unmeta* void)
    ~expect-arg~ !! (
        unmeta* ~foo~
    )
]
