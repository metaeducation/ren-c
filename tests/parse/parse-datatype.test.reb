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
            uparse? [a] [res: word!]
            res = 'a
        ]
    )
    (
        res: ~
        res2: ~
        did all [
            uparse? [a] [res: res2: 'a]
            res = 'a
            res2 = 'a
        ]
    )
]


(
    did all [
        uparse? "***{A String} 1020" [some "*", t: text!, i: integer!]
        t = {A String}
        i = 1020
    ]
)

[
    (uparse? [a 123] ['a integer!])
    (not uparse? [a 123] ['a char!])
    (uparse? [a 123] [['a] [integer!]])
    (not uparse? [a 123] ['a [char!]])
    (uparse? [123] [any-number!])
    (not uparse? [123] [any-string!])
    (uparse? [123] [[any-number!]])
    (not uparse? [123] [[any-string!]])
]

[
    (
        res: ~
        did all [
            uparse? [a 123] ['a (res: 1) [char! (res: 2) | integer! (res: 3)]]
            res = 3
        ]
    )
    (
        res: ~
        did all [
            not uparse? [a 123] ['a (res: 1) [char! (res: 2) | text! (res: 3)]]
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

    (uparse? to binary! {https://example.org"} [
        x: across url! (assert [{https://example.org"} == as text! to url! x])
    ])
    (uparse? to binary! {a@b.com"} [
        x: across email! (assert [a@b.com == to email! to text! x])
        {"}
    ])
]

[https://github.com/red/red/issues/4678
    (_ = uparse to binary! "_" [blank!])

    (not uparse? to binary! "#(" [blank!])
    (not uparse? to binary! "(" [blank!])
    (not uparse? to binary! "[" [blank!])
]
