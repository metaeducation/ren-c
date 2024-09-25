; %sigil.test.reb
;
; Sigils cover symbols that aren't legal words, but are useful to have
; in the evaluator and dialects:
;
;     SIGIL_SET = 1,      // trailing : (represented as `::` in isolation)
;     SIGIL_GET = 2,      // leading : (represented as `:` in isolation)
;     SIGIL_META = 3,     // ^
;     SIGIL_TYPE = 4,     // &
;     SIGIL_THE = 5,      // @
;     SIGIL_VAR = 6,      // $
;     SIGIL_QUOTE = 7     // '
;     SIGIL_QUASI = 8     // ~~
;
; REVIEW: AS TEXT! behavior for SIGIL! ?
;

(
    for-each 'sigil [:: : ^ & @ $ ' ~~] [
        if not sigil? sigil [
            fail [mold sigil]
        ]
    ]
    ok
)

; try termination by delimiter (and molding)
[
    ("::" = mold first [::])
    (":" = mold first [:])
    ("^^" = mold first [^])  ; caret is escape in Rebol strings
    ("&" = mold first [&])
    ("@" = mold first [@])
    ("$" = mold first [$])
    ("'" = mold first ['])
    ("~~" = mold first [~~])
]

; try termination by whitespace (and forming)
[
    ("::" = form first [:: <something>])
    (":" = form first [: <something>])
    ("^^" = form first [^ <something>])  ; caret is escape in Rebol strings
    ("&" = form first [& <something>])
    ("@" = form first [@ <something>])
    ("$" = form first [$ <something>])
    ("'" = form first [' <something>])
    ("~~" = form first [~~ <something>])
]

; Try quoted forms (and molding)
[
    ("'::" = mold first ['::])
    ("':" = mold first [':])
    ("'^^" = mold first ['^])  ; caret is escape in Rebol strings
    ("'&" = mold first ['&])
    ("'@" = mold first ['@])
    ("'$" = mold first ['$])
    ("''" = mold first [''])
    ("'~~" = mold first ['~~])
]

; Try TO TEXT! and MATCH
[
    ("::" = to text! match sigil! '::)
    (":" = to text! match sigil! ':)
    ("^^" = to text! match sigil! '^)  ; caret is escape in Rebol strings
    ("&" = to text! match sigil! '&)
    ("@" = to text! match sigil! '@)
    ("$" = to text! match sigil! '$)
    ("'" = to text! match sigil! '')
    ("~~" = to text! match sigil! '~~)
]

; Quasiforms of sigil don't exist (and probably should not, as combining the
; tildes with the sigil symbols is considered undesirable, so unless there
; is a really good reason sigils shouldn't have quasi/antiforms)
[
    ~scan-invalid~ !! (transcode "~::~")
    ~scan-invalid~ !! (transcode "~:~")
    ~scan-invalid~ !! (transcode "~^^~")  ; caret is escape in Rebol strings
    ~scan-invalid~ !! (transcode "~&~")
    ~scan-invalid~ !! (transcode "~@~")
    ~scan-invalid~ !! (transcode "~$~")
    ~scan-invalid~ !! (transcode "~'~")
    ~scan-invalid~ !! (transcode "~~~~")
]


; Test SIGIL OF for each bindable type
(
    for-each [sigil items] [
        ~null~  [  word    tu.p.le    pa/th    [bl o ck]    (gr o up)  ]
        ::      [  word:   tu.p.le:     _      [bl o ck]:   (gr o up): ]
        :       [ :word   :tu.p.le      _     :[bl o ck]   :(gr o up)  ]
        ^       [ ^word   ^tu.p.le   ^pa/th   ^[bl o ck]   ^(gr o up)  ]
        &       [ &word   &tu.p.le   &pa/th   &[bl o ck]   &(gr o up)  ]
        @       [ @word   @tu.p.le   @pa/th   @[bl o ck]   @(gr o up)  ]
        $       [ $word   $tu.p.le   $pa/th   $[bl o ck]   $(gr o up)  ]
        '       [ 'word   'tu.p.le   'pa/th   '[bl o ck]   '(gr o up)  ]
        ~~      [ ~word~      _         _     ~[bl o ck]~  ~(gr o up)~ ]
    ][
        for-each 'item items [
            if blank? item [continue]
            assert [any [quoted? item, quasi? item, bindable? item]]
            if (degrade sigil) <> sigil of item [
                fail [mold item]
            ]
        ]
    ]
    ok
)

; : has no meaning in the evaluator yet
[
    ~???~ !! (: <no> <meaning>)
]

; :: has no meaning in the evaluator yet
[
    ~???~ !! (:: <no> <meaning>)
]

; ^ is META
[
    ((@ '3) = ^ 1 + 2)
    (^null = ^ null)

    ~need-non-end~ !! (^)
]

; & is a TRY TYPE OF alias (well, what else would it be?)
[
    (integer! = & 10 + 20)
    (null = & null)  ; TRY is built-in

    ~need-non-end~ !! (&)
]

; $ is bind to current context (faster version of IN [])
[
    (
        foo: 10
        10 = get $ first [foo bar]
    )

    ~need-non-end~ !! ($)
]

; @ is THE, with exception that it has special handling in API feeds to
; be able to reconstitute antiforms.  (See TEST-LIBREBOL)  It will bind
; its argument.
[
    ('x = @ x)
    ('(a b c) = @ (a b c))
    (''3 = @ '3)

    ('~null~ = @ ~null~)

    (
        var: 1020
        word: @ var  ; binds
        1020 = get word
    )
    (
        var: 1020
        word: the var  ; binds
        1020 = get word
    )
    ~need-non-end~ !! (@)
]

; ' is JUST, which does not bind its argument.  It also has the API exception.
[
    ('x = ' x)
    ('(a b c) = ' (a b c))
    (''3 = ' '3)

    ('~null~ = ' ~null~)

    ~not-bound~ !! (
        var: 1020
        word: ' var  ; no binding
        1020 = get word
    )
    ~not-bound~ !! (
        var: 1020
        word: just var  ; no binding
        1020 = get word
    )
    ~need-non-end~ !! (')
]

; ~~ has no meaning in the evaluator yet.
[
    ~???~ !! (~~ <no> <meaning>)
]
