; functions/math/random.r
;
; Historical Rebol had a very overloaded RANDOM function.  Ren-C breaks it out
; into several different functions:
;
;     RANDOM:SEED => RANDOMIZE
;     RANDOM:ONLY => RANDOM-PICK
;     RANDOM => if it shuffled a series, SHUFFLE, else RANDOM
;
; Additionally, the ability to get a shuffled version of immutable values is
; added via SHUFFLE-OF.  This uses the same technique as e.g. REVERSE-OF,
; which is to look if the type supports a specific SHUFFLE-OF method and if
; not fall back on running SHUFFLE on a COPY of the value.  Hence while
; ANY-SEQUENCE? and ANY-WORD? define a SHUFFLE-OF handler, ANY-LIST? does not
; but you can still get a fresh copy of a series.

[#1084 (
    randomize 0
    not any [
        negative? random 1.0
        negative? random 1.0
    ]
)]

[#1875 (
    randomize 0
    2 = random-pick next [1 2]
)]

; Test that RANDOMIZE will give back the same value for each type.
(
    for-each 'seed [
        #{DECAFBAD}
        "strings could be used in R3-Alpha/Red"
        %ren-c/lets-you-use.file
        ren-c-also-allows-words-as-seeds-now
        @all-word-variants-as-well
        #A
        #issue-of-more-than-one-char-works-now-too
        1020%
        3.04
        1020
        3:04
        21-Apr-1975
        21-Apr-1975/10:20
    ][
        randomize seed
        let a: random 10000
        randomize seed
        if a != random 10000 [
            panic ["Nondeterministic seed:" to word! type of seed]
        ]
    ]
    okay
)

; Test empty shuffles and odd + even lengths
;
; 1. The implementation of shuffling with non-ASCII codepoints is done very
;    slowly for now.  But a correct implementation is better than none at all.
(
    for-each 'series compose2 '(<*>) [
        #{}
        #{DECAFBAD}
        #{DECAFBAD02}
        ""
        "shuffle me, I'm a string"
        "shuffle me, I'm a string2"
       -[Let's shuffle C😺T and Σὲ γνωρίζω ἀπὸ τὴν]-  ; slow implementation [1]
        (<*> make tag! 0)  ; should support -<>- or --<>-- etc. as empty tag
        <tags can be shuffled>
        <tags can be shuffled2>
        []
        [shuffle a block]
        [shuffle a block 2]
        ()
        (shuffle a group)
        (shuffle a group 2)
    ][
        randomize mold series
        let shuffled: shuffle-of series  ; SHUFFLE-OF makes a copy
        if (length of shuffled) != (length of series) [
            panic ["Shuffle produced wrong length:" mold series mold shuffled]
        ]
        let diff: difference shuffled series
        if not empty? diff [
            panic ["Difference with shuffle:" mold difference]
        ]
        randomize mold series
        shuffle series  ; should be same shuffle with same RANDOMIZE seed
        assert [series = shuffled]  ; ensure shuffle mutates

        if empty? series [  ; don't do partial test
            continue
        ]

        shuffled: shuffle next next copy series  ; mutates, and not at head
        if (2 + length of shuffled) != (length of series) [
            panic ["Wrong length non-head shuffle:" mold series mold shuffled]
        ]
        all [
            (head of shuffled).1 = series.1
            (head of shuffled).2 = series.2
        ] else [
            panic ["Shuffle did not preserve head:" mold series mold shuffled]
        ]

        let diff: difference (head of shuffled) series
        if not empty? diff [
            panic ["Difference with non-head shuffle:" mold difference]
        ]
    ]
    okay
)

; RANDOM-BETWEEN is only implemented for INTEGER! at time of writing.
(
    let found: [~ ~ ~ ~ ~ ~ ~ ~ ~ ~]
    let want: [~ ~ # # # # # # ~ ~]
    repeat 100 [
        found.(random-between 3 8): #
    ]
    want = found
)

(
    for-each 'series compose2 '(<*>) [
        #{}
        #{DECAFBAD}
        ""
        "pick me, I'm a string"
        "pick me, I'm a string2"
       -[Let's pick C😺T and Σὲ γνωρίζω ἀπὸ τὴν]-  ; slow implementation [1]
        (<*> make tag! 0)  ; should support -<>- or --<>-- etc. as empty tag
        <tags can be picked>
        []
        [pick a block]
        ()
        (pick a group)
    ][
        if empty? series [
            assert [raised? random-pick series]
            assert [null = try random-pick series]
            continue
        ]
        repeat 50 [
            let item: random-pick series
            if not find series item [
                panic ["PICK-RANDOM item not in series" mold series mold item]
            ]
        ]
    ]
)

; Simple smoke test of SHUFFLE OF
(
    for-each 'immutable [
        a/ /a a/b a/b/c 1/2/3/4
        a: :a a:b a:b:c  ; numbers would be a time
        a. .a a.b a.b.c 1.2.3.4

        word
        # #a #abcdefg

        %some-file.txt
    ][
        let shuffled: shuffle of immutable
        if (type of immutable) != (type of shuffled) [
            panic ["Shuffle made different length" mold immutable mold shuffled]
        ]
        if (length of immutable) != (length of shuffled) [
            panic ["Shuffle made different length" mold immutable mold shuffled]
        ]
        let canon1: as (either any-utf8? immutable [text!] [block!]) immutable
        let canon2: as (either any-utf8? shuffled [text!] [block!]) shuffled
        if not empty? difference canon1 canon2 [
            panic ["Canon contents differ:" mold canon1 mold canon2]
        ]
    ]
    ok
)
