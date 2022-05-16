; %parse-datatype.test.reb
;
; If a DATATYPE! is used in a BLOCK! rule, it means the item at the current
; parse position needs to match that type.  It returns the matched value.
;
; If a DATATYPE! is used in a text or binary rule, that is interpreted as a
; desire to TRANSCODE the input.
;
; !!! This feature needs more definition, e.g. to be able to transcode things
; that don't end with space or end of input.  For instance, think about how
; to handle the below rule if it was `1020****` and having a `some "*"` rule
; at the tail as well.

(123 = uparse "123" [integer!])

[
    (
        res: ~
        did all [
            'a == uparse [a] [res: word!]
            res = 'a
        ]
    )
    (
        res: ~
        res2: ~
        did all [
            'a == uparse [a] [res: res2: 'a]
            res = 'a
            res2 = 'a
        ]
    )
]


(
    did all [
        1020 == uparse "***{A String} 1020" [some "*", t: text!, i: integer!]
        t = {A String}
        i = 1020
    ]
)

[
    (123 == uparse [a 123] ['a integer!])
    (didn't uparse [a 123] ['a char!])
    (123 == uparse [a 123] [['a] [integer!]])
    (didn't uparse [a 123] ['a [char!]])
    (123 == uparse [123] [any-number!])
    (didn't uparse [123] [any-string!])
    (123 == uparse [123] [[any-number!]])
    (didn't uparse [123] [[any-string!]])
]

[
    (
        res: ~
        did all [
            3 == uparse [a 123] [
                'a (res: 1) [char! (res: 2) | integer! (res: 3)]
            ]
            res = 3
        ]
    )
    (
        res: ~
        did all [
            didn't uparse [a 123] ['a (res: 1) [char! (res: 2) | text! (res: 3)]]
            res = 1
        ]
    )
]


; !!! Ren-C is currently letting you put quotes inside URLs unescaped; this is
; because browsers render quotes in the URL bar.  For instance, this shows
; quotes in the URL bar:
;
;    https://en.wikipedia.org/wiki/%22Heroes%22_(David_Bowie_album)
;
; And this shows an accented e:
;
;    https://en.wikipedia.org/wiki/Herg%C3%A9
;
; But browsers will escape the quotes when you copy and paste.  This behavior
; is becoming more standard, and the jury is still out on exactly when and
; how transformations of this should be taking place in the language (the
; logic in the browsers is fuzzy.)
;
[https://github.com/red/red/issues/4682

    (none? uparse to binary! {https://example.org"} [
        x: across url! (assert [{https://example.org"} == as text! to url! x])
    ])
    ({"} == uparse to binary! {a@b.com"} [
        x: across email! (assert [a@b.com == to email! to text! x])
        {"}
    ])
]

[https://github.com/red/red/issues/4678
    ('~blank~ = meta uparse to binary! "_" [blank!])

    (didn't uparse to binary! "#(" [blank!])
    (didn't uparse to binary! "(" [blank!])
    (didn't uparse to binary! "[" [blank!])
]

; QUOTED! needs to be recognized (KIND OF VALUE and TYPE OF VALUE are currently
; different, and this had caused a problem)
[
    ((the 'x) == uparse ['x] [quoted!])
    ((the '[]) == uparse [' '() '[]] [3 quoted!])
]
