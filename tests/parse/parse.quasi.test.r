; %parse-quasi.test.r
;
; When a QUASIFORM! is encountered in a parse rule, the semantic meaning
; is taken to be the antiform of that quasiform--as if PARSE had
; "evaluated it" to make the antiform.
;
; In addition to applying to literal quasiforms in rules, it is also
; what is used to process antiforms which are fetched by rules matching
; a variable, e.g. when that variable holds null, okay, void, or splice.

[
    ~bad-antiform~ !! (parse [a b] ['a ~null~ 'b])
    ~bad-antiform~ !! (parse [a b] ['a null 'b])
    ~bad-antiform~ !! ('b = parse [a b] ['a @null 'b])
    ~bad-antiform~ !! ('b = parse [a b] ['a @lib.null 'b])
    ~bad-antiform~ !! ('b = parse [a b] ['a @(null) 'b])
    ~bad-antiform~ !! ('b = parse [a b] ['a @[(null)] 'b])
]

[
    ('b = parse [a b] ['a ~[]~ 'b])
    ('b = parse [a b] ['a @void 'b])
    ('b = parse [a b] ['a @lib.void 'b])
    ('b = parse [a b] ['a @(void) 'b])
    ('b = parse [a b] ['a @[(void)] 'b])

    (void = parse [a b] ['a 'b ~[]~])
    (void = parse [a b] ['a 'b @void])
    (void = parse [a b] ['a 'b @lib.void])
    (void = parse [a b] ['a 'b @(void)])
    (void = parse [a b] ['a 'b @[(void)]])
]

; Note: variables cannot contain nihil [1]
[
    ('b = parse [a b] ['a ~[]~ 'b])
    ; (parse [a b] ['a nihil 'b])  ; [1]
    ; ('b = parse [a b] ['a @nihil 'b])  ; [1]
    ; ('b = parse [a b] ['a @lib.nihil 'b])  ; [1]
    ('b = parse [a b] ['a @(~[]~) 'b])
    ('b = parse [a b] ['a @[(~[]~)] 'b])

    ('b = parse [a b] ['a 'b ~[]~])
    ; (parse [a b] ['a 'b nihil])  ; [1]
    ; ('b = parse [a b] ['a 'b @nihil])  ; [1]
    ; ('b = parse [a b] ['a 'b @lib.nihil])  ; [1]
    ('b = parse [a b] ['a 'b @(~[]~)])
    ('b = parse [a b] ['a 'b @[(~[]~)]])
]

[
    (
        splice: ~(a b)~
        obj: make object! [splice: ~(a b)~]
        ok
    )
    (splice = parse [a b a b] [some ~(a b)~])
    ~???~ !! (parse [a b a b] [some splice])  ; disallowed case for WORD!
    (splice = parse [a b a b] [some @splice])
    (splice = parse [a b a b] [some @obj.splice])
    (splice = parse [a b a b] [some @(splice)])
    (splice = parse [a b a b] [some @[(splice)]])
]
