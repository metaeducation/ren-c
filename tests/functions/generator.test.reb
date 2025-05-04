; GENERATOR is the 0-arity convenience form of YIELDER.

; Basic disallowal of re-entry
(
    g: generator [g]
    'yielder-reentered = (sys.util/rescue [g]).id
)


; Errors that cross a yielder will always fail thereafter
(
    g: generator [yield 1 fail "Bad!" yield 2]

    all [
        g = 1
        error? sys.util/rescue [g]
        (sys.util/rescue [g]).id = 'yielder-failed
        (sys.util/rescue [g]).id = 'yielder-failed
    ]
)

; Throws that cross a yielder are equivalent to exiting it normally
(
    data: copy []
    gmaker: func [] [
        let g
        g: generator [yield 1 yield 2 yield 3 return g/ yield 4]
        cycle [
            append data g
        ]
    ]
    all wrap [
        action? /result: gmaker
        done? result
        done? result
        data = [1 2 3]
    ]
)


; Trying to run YIELD while a generator is suspended is an error
(
    stolen-yield: ~
    g: generator [
        stolen-yield: yield/
        yield 1
        yield 2
    ]

    did all [
        g = 1
        'frame-not-on-stack = (sys.util/rescue [stolen-yield 3]).id
    ]
)


; WHILE loop with generator
(
    g: generator [while [okay] [yield 1]]
    sum: 0
    repeat 1000 [sum: sum + g]
    sum = 1000
)
(
    [10 20 30] = collect [
        while generator [
            yield 1
            yield 2
            yield 3
        ] func [x] [
            keep x * 10
        ]
    ]
)
(
    x: 1
    g: generator [
        while [okay] [
            x: 10 + yield x
        ]
    ]
    [1 11 21 31 41 51 61 71 81 91] = array:initial 10 g/
)


; ANY with generator
(
    g: generator [
        yield 1
        yield any [
            if 1 > 2 [<bad-news>]
            find "abc" "d"
        ]
        any [
            null
            yield 2
        ]
    ]

    all [
        g = 1
        g = null
        g = 2
        done? g
        done? g
    ]
)


; ENCLOSE compatibility
(
    g: generator [
        let /yy: enclose yield/ func [f] [
            f.^atom: f.^atom * 10
            return 1 + eval-free f
        ]
        yy yy yy 10
    ]
    all [
       g = 100
       g = 1010
       g = 10110
       done? g
       done? g
    ]
)


; Generators cannot return definitional errors... with the exception of DONE
; (although that terminates the generator).
;
; Here's a trick to make it possible to generate them, and turn null into
; what generates the termination condition.
(
    e-generator: func [body [block!]] [
        let g: generator [
            yield: enclose yield/ func [f [frame!] <local> temp] [
                if null? f.^atom [
                    f.^atom: done
                ]
                return unmeta eval-free f
            ]
            eval overbind binding of $yield body
        ]
        return does [
            unmeta (g except [meta null])
        ]
    ]

    a': b: c': d: e: ~

    /g: e-generator [
        a': ^ yield done
        b: yield 1
        c': ^ yield done
        d: yield null  ; rigged to terminate the generator
        e: yield <unreachable>
    ]

    all [
        done? g
        g = 1
        done? g
        g = null
        g = null

        done? unmeta a'
        b = 1
        done? unmeta c'
        unset? $d
        unset? $e
    ]
)


; Simple CASCADE test
(
    /c: cascade [
        generator [yield 1 yield 2 yield 3]

        func [^x] [return either done? unmeta x [done] [(unmeta x) + 10]]
    ]
    all [
        c = 11
        c = 12
        c = 13
        done? c
        done? c
    ]
)


; COMPOSE test
(
    g: generator [
        yield compose:deep [
            So (yield "How") [(yield "About")] (yield "This") ?
        ]
    ]

    did all [
        g = "How"
        g = "About"
        g = "This"
        g = [So "How" ["About"] "This" ?]
        done? g
        done? g
    ]
)


; DELIMIT (SPACED) test
(
    g: generator [
        let n: 1
        while [okay] [
            yield spaced [yield "Step" yield n]
            n: n + 1
        ]
    ]

    all [
        g = "Step"
        g = 1
        g = "Step 1"
        g = "Step"
        g = 2
        g = "Step 2"
    ]
)


; PARSE tests
(
    vowelizer: func [text [text!]] [
        let vowel: charset "aeiouAEIOU"
        return generator [
            parse text [opt some [
                [to vowel
                let ch: one (yield ch)]
                | one
            ]]
        ]
    ]

    v: vowelizer "The Quick Brown Fox Jumped Over The Lazy Dogs"
    ([#"e" #"u" #"i" #"o" #"o" #"u" #"e" #"O" #"e" #"e" #"a" #"o" <end>]
        = reduce [v v v v v v v v v v v v any [try v, <end>]])
)


; TUPLE!, GET-TUPLE!, SET-TUPLE!
;
; !!! At time of writing these won't work, due to the unfinished stacklessness
; of the tuple fetching mechanics, where the design is still stabilizing.
;[
;    (
;        twenty: func [] [return 20]
;        obj: make object! [sub: make object! [x: 10 /f: twenty/]]
;        get-obj: func [] [obj]
;        true
;    )
;
;    (
;        g: generator [yield (get-obj elide yield <foo>).(yield 'sub).f]
;        [<foo> sub 20 _ _] = reduce [g g g try g try g]
;    )
;
;    (
;        g: generator [yield (get-obj elide yield <foo>).(yield 'sub).f/]
;        (compose [<foo> sub (twenty/) _ _]) = reduce [g g g try g try g]
;    )
;
;    (
;        g: generator [yield (get-obj elide yield <foo>).(yield 'sub).x: 1 + 2]
;        all [
;            [<foo> sub 3 _ _] = reduce [g g g try g try g]
;            obj.sub.x = 3
;        ]
;    )
;]

; Interoperability with multiple return values
(
    g: generator [
        yield pack ["FOO!" <foo>]
        yield pack ["BAR!" <bar>]
    ]
    all wrap [
        "FOO!" = [a b]: g
        a = "FOO!"
        b = <foo>

        "BAR!" = g

        done? g
        done? g
    ]
)
