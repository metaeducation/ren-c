; quasi.test.reb

(not quasi? 1)

(
    v: quasi 'labeled
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
    for-each 'str valid wrap [
        word: parse str [/to-word between '~ '~]
        bad: transcode:one str
        assert [quasi? bad]
        assert [word = unquasi bad]
    ]
    ok
)

(
    invalid: ["~~" "~~~" "~a" "~~~a"]
    for-each 'str invalid [
        transcode:one str except e -> [
            assert [e.id = 'scan-invalid]
        ]
    ]
    ok
)


; https://forum.rebol.info/t/what-should-do-do/1426
;
(void? eval [])
(
    x: <overwritten>
    all [
        (meta void) = ^ x: eval []
        void? ^(meta x)
    ]
)
(
    x: 10
    all [
        '~,~ = x: meta eval []
        ghost? unmeta x
    ]
)
(
    x: 10
    10 = (x eval [])
)

[
    (foo: lambda [] [], ok)

    (void? foo)

    ((meta void) = ^ applique foo/ [])
    (void? applique foo/ [])

    ((meta void) = ^ eval foo/)
    (void? eval foo/)

    (void? eval foo/)
]

[
    (foo: func [] [], ok)

    (trash? foo)

    ((meta trash) = meta applique foo/ [])
    (trash? applique foo/ [])

    ((meta trash) = meta eval foo/)
    (trash? eval foo/)

    ((meta trash) = meta eval foo/)
]

; Explicit return of VOID
[
    (did foo: func [return: [any-value?]] [return void])

    (void? foo)
    ((meta void) = ^ foo)

    (void? (1 + 2 foo))
]

; Not providing an argument is an error (too easy to pick up random arguments
; from another line if 0-arity were allowed)
[
    (did foo: func [return: [any-value?] x] [])

    ~unspecified-arg~ !! (foo)
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
    foo: func [return: []] []
    (meta trash) = meta foo
)(
    data: [a b c]
    f: func [return: []] [append data spread [1 2 3]]
    (meta trash) = meta f
)]

; locals are unset before they are assigned
(
    f: func [<local> loc] [return get:any $loc]
    trash? f
)(
    f: func [<local> loc] [return reify get:any $loc]
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
(
    e: sys.util/rescue [get:any $asiieiajiaosdfbjakbsjxbjkchasdf]
    all [
        e.id = 'not-bound
        e.arg1 = 'asiieiajiaosdfbjakbsjxbjkchasdf
    ]
)

; Note: QUITs are definitional and provided by things like DO when they run
; a script, or IMPORT.  They are variants of THROW.
[
    (1 = do "Rebol [] quit:value 1")
    (trash? do "Rebol [] quit 0")
    (do "Rebol [] quit 1" except e -> [e.exit-code = 1])
    (quasi? do "Rebol [] quit:value ^^ fail -[some error]-")  ; ^^ escapes ^
    (error? do "Rebol [] quit:value fail* make warning! -[some error]-")
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
(parse3 [~foo~ ~foo~] [some '~foo~], ok)  ; acceptable
~???~ !! (parse3 [~foo~ ~foo~] [some ~foo~], ok)  ; !!! shady, rethink
~???~ !! (
    foo: '~foo~
    parse3 [~foo~ ~foo~] [some foo]
)
~bad-antiform~ !! (
    foo: ~NaN~
    parse3 [~foo~ ~foo~] [some foo]
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
        void? ()  ; empty groups make nihil
        3 = (1 + 2 ())
    ])
    ~no-value~ !! (
        a: () 1 + 2  ; can't assign nihil
    )
]


(
    a: ~okay~
    (meta a) = '~okay~
)
(
    a: ~
    not warning? trap [set $a '~okay~]
)


[
    ('null = noquasi reify ~null~)
    ('~null~ = reify null)
    ('~null~ = reify ~null~)

    (10 = reify 10)
    ((the '''a) = reify the '''a)
]

; META:LITE does not produce quasiforms
; It passes through keywords, and makes antiforms their plain forms
; Other types receive a quoting level
[
    (null = meta:lite null)
    (void = meta:lite void)
    (okay = meta:lite okay)

    (['1 '2] = meta:lite pack [1 2])

    (the '[1 2] = meta:lite [1 2])
    (the ''a = meta:lite first ['a])
]

; UNMETA:LITE works on keywords, but not other "antiform" forms as a trick
; meta forms are plain forms, not quasiforms
[
    (void? unmeta:lite void)
    (null? unmeta:lite null)
    (okay? unmeta:lite okay)

    ~expect-arg~ !! (
        unmeta:lite ~(a b c)~
    )

    ~???~ !! (
        unmeta:lite '~(a b c)~
    )
    (splice? unmeta:lite the (a b c))
    (pack? unmeta:lite ['1 '2])
]
