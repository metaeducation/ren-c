[
    ; Fixed file, content copied from:
    ; https://www.w3.org/2001/06/utf-8-test/UTF-8-demo.html
    ;
    ; Its length can be verified via Python2 with:
    ;
    ;     import codecs
    ;     with codecs.open('utf8-plain-text.txt', encoding='utf-8') as myfile:
    ;         data = myfile.read()
    ;         print(len(data))
    (
        t: to text! read %../fixtures/utf8-plain-text.txt
        tlen: length of t
        assert [tlen = 7086]

        braille: "⡕⠇⠙ ⡍⠜⠇⠑⠹ ⠺⠁⠎ ⠁⠎ ⠙⠑⠁⠙ ⠁⠎ ⠁ ⠙⠕⠕⠗⠤⠝⠁⠊⠇⠲"

        assert [37 = length of braille]

        warning: "⚠"
        assert [1 = length of warning]

        true
    )

    (
        tcopy: copy t
        replace tcopy braille null
        (length of tcopy) = (tlen - length of braille)
    )

    (
        tcopy: copy t
        replace tcopy braille warning
        (length of tcopy) = (tlen + 1 - length of braille)
    )

    (
        tcopy: copy t
        pos: find tcopy braille
        change/part pos warning length of braille
        assert [pos/1 = as issue! warning]
        (length of tcopy) = (tlen + 1 - length of braille)
    )

    (
        tcopy: copy t
        n: 0
        while [c: take tcopy] [
            n: n + 1
            assert [c = t/(n)]
        ]
        n = tlen
    )

    (
        tcopy: copy t
        n: length of t
        while [c: take/last tcopy] [
            assert [c = t/(n)]
            n: n - 1
        ]
        n = 0
    )

    (
        n: 0
        for-each c t [
            n: n + 1
            assert [c = t/(n)]
        ]
        n = tlen
    )

    (
        assert [parse t [to braille copy b to newline to end]]
        b = braille
    )
]


(
    str: "caffè"
    bin: as binary! str
    append bin 65
    did all [
        bin = #{63616666C3A841}
        str = "caffèA"
    ]
)


[
    {AS aliasing of TEXT! as BINARY! constrains binary modifications to UTF-8}
    https://github.com/metaeducation/ren-c/issues/817

    (
        t: "օʊʀֆօռǟɢɢօռ"
        b: as binary! t
        true
    )

    (
        insert b "ƈ"
        t = "ƈօʊʀֆօռǟɢɢօռ"
    )(
        append b #{C9A8}
        t = "ƈօʊʀֆօռǟɢɢօռɨ"
    )(
        e: trap [insert b #{E08080}]
        e/id = 'bad-utf8-bin-edit
    )(
        b: as binary! const "test"
        e: trap [append b 1]
        e/id = 'const-value
    )
]

[
    {AS aliasing of BINARY! as TEXT! can only be done on mutable binaries}
    https://github.com/metaeducation/ren-c/issues/817

    (
        b: #{64C990E1B49A64C9905A64C4B15A}
        t: as text! b
        did all [
            t = "dɐᴚdɐZdıZ"
            e: trap [append b #{E08080}]
            e/id = 'bad-utf8-bin-edit
        ]
    )
    (
        b: #{64C990E1B49A64C9905A64C4B15A}
        append b #{E08080}
        e: trap [as text! b]
        e/id = 'bad-utf8
    )
    (
        e: trap [as text! const #{64C990E1B49A64C9905A64C4B15A}]
        e/id = 'alias-constrains
    )
]


("σԋα ƚαʅ" = as text! as binary! skip "ɾαx σԋα ƚαʅ" 4)


; /PART for APPEND and insert speaks in terms of the limit of how much
; the target series should be allowed to grow in terms of its units.  Thus
; you can copy partial UTF-8 out into a binary...
;
; !!! Proposal is that /LIMIT would be used for this purpose, and /PART would
; refer to the amount of source material to be used.
[
    (#{} = append/part #{} "ò" 0)
    (#{C3} = append/part #{} "ò" 1)
    (#{C3B2} = append/part #{} "ò" 2)
    (#{C3B2} = append/part #{} "ò" 3)

    ("" = append/part "" #{C3B2} 0)
    ("ò" = append/part "" #{C3B2} 1)
    ("ò" = append/part "" #{C3B2} 2)

    ("ò" = append/part "" #{C3B2DECAFBAD} 1)
    (error? trap [append/part "" #{C3B2FEFEFEFE} 2])
]
