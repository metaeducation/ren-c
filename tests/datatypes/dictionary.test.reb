; %dictionary.test.reb
;
; MAP is being reclaimed in Ren-C for the CS higher-order-function concept of
; a mapping operation.  Hence it is a verb, not a noun:
;
;     >> map x each [1 2 3] [x * 10]
;     == [10 20 30]
;
; The old MAP! datatype (which was once called HASH! in Rebol2) is slated to be
; called DICTIONARY!.  It will be renamed at an appropriate moment.

(empty? make map! [])
(empty? make map! 4)
; The length of a map is the number of key/value pairs it holds.
(2 == length of make map! [a 1 b 2])  ; 4 in R2, R2/Forward
(m: make map! [a 1 b 2] 1 == m.a)
(m: make map! [a 1 b 2] 2 == m.b)
(
    m: make map! [a 1 b 2]
    null? m.c
)
(m: make map! [a 1 b 2] m.c: 3 3 == m.c)
; Maps contain key/value pairs and must be created from blocks of even length.
(error? trap [make map! [1]])
(empty? clear make map! [a 1 b 2])
[#1930 (
    m: make map! 8
    clear m
    not find m 'a
)]

(did find make map! [foo 0] 'foo)

[#2293 (
    thing: copy/deep [a [b]]
    m: make map! reduce [1 thing]
    m2: copy/deep m
    thing2: select m2 1
    append thing.2 [c]
    append thing2 [d]
    did all [
        thing = [a [b c]]
        thing2 = [a [b] d]
    ]
)]

; Maps are able to hold multiple casings of the same keys, but a map in such
; a state must be accessed in such a way that there isn't ambiguity.  Using
; PATH! or plain SELECT will error if the key being asked about has more than
; one case form.  The way to get past this is SELECT/CASE and PUT/CASE, which
; use only the exact spelling of the key given.
;
; Creation through MAKE MAP! assumes case insensitivity.
[
    (
        m: make map! [AA 10 aa 20 <BB> 30 <bb> 40 #"C" 50 #"c" 60]
        true
    )

    (10 = select/case m 'AA)
    (20 = select/case m 'aa)
    (30 = select/case m <BB>)
    (40 = select/case m <bb>)
    (50 = select/case m #"C")
    (60 = select/case m #"c")

    ('conflicting-key = (trap [m.AA]).id)
    ('conflicting-key = (trap [m.aa]).id)
    ('conflicting-key = (trap [select m <BB>]).id)
    ('conflicting-key = (trap [select m <bb>]).id)
    ('conflicting-key = (trap [m.(#"C")]).id)
    ('conflicting-key = (trap [m.(#"c")]).id)

    ('conflicting-key = (trap [put m 'Aa 70]).id)
    ('conflicting-key = (trap [m.(<Bb>): 80]).id)
    ('conflicting-key = (trap [m.(#"C"): 90]).id)

    (
        put/case m 'Aa 100
        put/case m <Bb> 110
        put/case m #"C" 120
        true
    )

    (100 = select/case m 'Aa)
    (110 = select/case m <Bb>)
    (120 = select/case m #"C")

    (10 = select/case m 'AA)
    (20 = select/case m 'aa)
    (30 = select/case m <BB>)
    (40 = select/case m <bb>)
    (120 = select/case m #"C")
    (60 = select/case m #"c")
]

; Historically non-strict equality considered 'A and A to be equal, while strict
; equality consdered them unequal.  Ren-C has shifted to where quoted types
; are neither strict or non-strict equal to those at different quoting levels.
[
    (
        b2: copy the ''[x y]
        b4: copy the ''''[m n o p]
        m: make map! compose [
            a 0 'a 1 ''a 2 '''a 3 ''''a 4
            A 10 'A 11 ''A 12 '''A 13 ''''A 14
            (b2) II (b4) IIII
        ]
        true
    )

    (0 = select/case m the a)
    (1 = select/case m the 'a)
    (2 = select/case m the ''a)
    (3 = select/case m the '''a)
    (4 = select/case m the ''''a)

    ((trap [select m the a]).id = 'conflicting-key)
    ((trap [m.(the a)]).id = 'conflicting-key)

    ((trap [select m the ''''a]).id = 'conflicting-key)
    ((trap [m.(the ''''a)]).id = 'conflicting-key)

    ('II = m.(the ''[x y]))
    ('IIII = m.(the ''''[m n o p]))

    ((trap [append noquote b2 'z]).id = 'series-auto-locked)
    ((trap [append noquote b4 'q]).id = 'series-auto-locked)
]


; !!! This should be extended to test instances of each datatype
[#774 (
    m: make map! []
    m.(#"A"): 1020
    1020 = m.(#"A")
)]
