; quasi.test.r

(not quasi? 1)

(
    v: quasi 'labeled
    all [
        quasi? v
        '~labeled~ = v
    ]
)

(word?:quasiform first [~abc~])
(not word?:quasiform first [~[a b c]~])
(not word?:quasiform first [~@a~])
(word?:pinned:quasiform first [~@a~])

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


[
    (foo: lambda [] [], ok)

    (ghost? foo)
    (ghost? (foo))

    ((lift ^ghost) = lift applique foo/ [])
    (ghost? applique foo/ [])
    (ghost? (applique foo/ []))

    ((lift ^ghost) = lift eval foo/)
    (ghost? eval foo/)
    (ghost? (eval foo/))
]

[
    (foo: func [] [], ok)

    ~???~ !! (foo)  ; no return
]

[
    (foo: func [] [return ~], ok)

    ((lift ^tripwire) = lift applique foo/ [])
    (tripwire? applique foo/ [])

    ((lift ^tripwire) = lift eval foo/)
    (tripwire? eval foo/)

    ((lift ^tripwire) = lift eval foo/)
]

; Explicit return of VOID
[
    (did foo: func [return: [any-value?]] [return ^void])

    (void? foo)
    ((lift ^void) = lift foo)

    (void? (1 + 2 foo))
]

; Not providing an argument is an error (too easy to pick up random arguments
; from another line if 0-arity were allowed)
[
    (did foo: func [return: [any-value?] x] [])

    ~unspecified-arg~ !! (foo)
]

[
    (did foo: func [return: [any-stable?]] [return ~()~])

    (hole? foo)
    (not void? foo)
    ('~()~ = lift foo)

    (hole? applique foo/ [])
    (hole? eval foo/)
]


[(
    foo: proc [] []
    (lift ^tripwire) = lift foo
)(
    data: [a b c]
    f: func [return: ~] [append data spread [1 2 3] return ~]
    (lift ^tripwire) = lift f
)]

; LOCAL null, ^LOCAL unset, and ~LOCAL~ trash
[
    (local: 100, true)

    (reeval lambda [{local}] [null? local])
    (reeval lambda [{^local}] [unset? $local])
    ; (reeval lambda [{~local~}] [trash? ^local])  ; TBD

    (local = 100)
]

; Genuine unbound words exist (e.g. product of MAKE WORD!) but there are also
; words that are attached to a context, but have no definition in that context
; or anything it inherits from.  These have never had an "emerge" operation
; (at time of writing, just assigning a SET-WORD! with the binding is enough
; to do such an emergence, though something like a JavaScript strict mode
; would demand some kind of prior declaration of intent to use the name).
;
(
    e: sys.util/recover [get meta $asiieiajiaosdfbjakbsjxbjkchasdf]
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
            void? ^a: (^void)
            void? ^a
        ]
    )
    (all [
        ghost? ()  ; empty groups make ghosts
        3 = (1 + 2 ())
    ])
    ~no-value~ !! (
        a: () 1 + 2  ; can't assign nihil
    )
]


(
    a: ~okay~
    (lift a) = '~okay~
)
(
    a: ~
    not warning? rescue [set $a '~okay~]
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
    (null = lift:lite null)
    (^void = lift:lite ^void)
    (okay = lift:lite okay)

    (['1 '2] = lift:lite pack [1 2])

    (the '[1 2] = lift:lite [1 2])
    (the ''a = lift:lite first ['a])
]

; UNMETA:LITE works on keywords, but not other "antiform" forms as a trick
; lift forms are plain forms, not quasiforms
[
    (void? unlift:lite ^void)
    (null? unlift:lite null)
    (okay? unlift:lite okay)

    ~expect-arg~ !! (
        unlift:lite ~(a b c)~
    )

    ~???~ !! (
        unlift:lite '~(a b c)~
    )
    (splice? unlift:lite the (a b c))
    (pack? unlift:lite ['1 '2])
]
