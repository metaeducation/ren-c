; %dictionary.test.r
;
; MAP is being reclaimed in Ren-C for the CS higher-order-function concept of
; a mapping operation.  Hence it is a verb, not a noun:
;
;     >> map x each [1 2 3] [x * 10]
;     == [10 20 30]
;
; The old MAP! datatype (which was once called HASH! in Rebol2) is slated to be
; called DICTIONARY!.  It will be renamed at an appropriate moment.

(empty? to map! [])
(empty? make map! 4)
; The length of a map is the number of key/value pairs it holds.
(2 = length of to map! [a 1 b 2])  ; 4 in R2, R2/Forward
(m: to map! [a 1 b 2] 1 = m.a)
(m: to map! [a 1 b 2] 2 = m.b)
(
    m: to map! [a 1 b 2]
    null? m.c
)
(m: to map! [a 1 b 2] m.c: 3 3 = m.c)

; Maps contain key/value pairs and must be created from blocks of even length.
;
~???~ !! (to map! [1])

(empty? clear to map! [a 1 b 2])
[#1930 (
    m: make map! 8
    clear m
    null = select m 'a
)]

(0 = select to map! [foo 0] 'foo)

[#2293 (
    thing: copy:deep [a [b]]
    m: to map! reduce [1 thing]
    m2: copy:deep m
    thing2: select m2 1
    append thing.2 spread [c]
    append thing2 spread [d]
    all [
        thing = [a [b c]]
        thing2 = [a [b] d]
    ]
)]

; Maps are able to hold multiple casings of the same keys, but a map in such
; a state must be accessed in such a way that there isn't ambiguity.  Using
; PATH! or plain SELECT will error if the key being asked about has more than
; one case form.  The way to get past this is SELECT:CASE and PUT:CASE, which
; use only the exact spelling of the key given.
;
; Creation through TO MAP! assumes case insensitivity.
[
    (
        m: to map! [AA 10 aa 20 <BB> 30 <bb> 40 #"C" 50 #"c" 60]
        ok
    )

    (10 = select:case m 'AA)
    (20 = select:case m 'aa)
    (30 = select:case m <BB>)
    (40 = select:case m <bb>)
    (50 = select:case m #"C")
    (60 = select:case m #"c")

    ~conflicting-key~ !! (m.AA)
    ~conflicting-key~ !! (m.aa)
    ~conflicting-key~ !! (select m <BB>)
    ~conflicting-key~ !! (select m <bb>)
    (m.(#"C") = 50)
    (m.(#"c") = 60)

    ~conflicting-key~ !! (poke m 'Aa 70)
    ~conflicting-key~ !! (m.(<Bb>): 80)
    (90 = m.(#"C"): 90)

    (
        poke m 'Aa 100
        poke m <Bb> 110
        poke m #"C" 120
        ok
    )

    (100 = select:case m 'Aa)
    (110 = select:case m <Bb>)
    (120 = select:case m #"C")

    (10 = select:case m 'AA)
    (20 = select:case m 'aa)
    (30 = select:case m <BB>)
    (40 = select:case m <bb>)
    (120 = select:case m #"C")
    (60 = select:case m #"c")
]

; Historically non-strict equality considered 'A and A to be equal, while strict
; equality consdered them unequal.  Ren-C has shifted to where quoted types
; are neither strict or non-strict equal to those at different quoting levels.
[
    (
        b2: copy the ''[x y]
        b4: copy the ''''[m n o p]
        m: to map! compose [
            a 0 'a 1 ''a 2 '''a 3 ''''a 4
            A 10 'A 11 ''A 12 '''A 13 ''''A 14
            (b2) II (b4) IIII
        ]
        ok
    )

    (0 = select:case m the a)
    (1 = select:case m the 'a)
    (2 = select:case m the ''a)
    (3 = select:case m the '''a)
    (4 = select:case m the ''''a)

    ~conflicting-key~ !! (select m the a)
    ~conflicting-key~ !! (m.(the a))

    ~conflicting-key~ !! (select m the ''''a)
    ~conflicting-key~ !! (m.(the ''''a))

    ('II = m.(the ''[x y]))
    ('IIII = m.(the ''''[m n o p]))

    ~series-auto-frozen~ !! (append noquote b2 'z)
    ~series-auto-frozen~ !! (append noquote b4 'q)
]


; !!! This should be extended to test instances of each datatype
[#774 (
    m: to map! []
    m.(#"A"): 1020
    1020 = m.(#"A")
)]

; Antiforms are not allowed in maps as either keys or values
[
    ~bad-antiform~ !! (
        m: to map! []
        m.key: null
    )

    ~bad-antiform~ !! (
        m: to map! []
        m.(null): 1020
    )

    ~bad-antiform~ !! (
        m: to map! []
        m.(spread [a b c]): 1020
    )
]

; Void can be used to remove keys from maps
(
    m: to map! [key <initial>]
    all [
        m.key = <initial>
        elide m.key: ~()~
        null? try m.key
    ]
)
