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
;
; REVIEW: AS TEXT! behavior for SIGIL! ?
;

(did apply :all [
    @[:: : ^ & @ $]
    /predicate item -> [sigil? item]
])

; try termination by delimiter (and molding)
[
    ("::" = mold first [::])
    (":" = mold first [:])
    ("^^" = mold first [^])  ; caret is escape in Rebol strings
    ("&" = mold first [&])
    ("@" = mold first [@])
    ("$" = mold first [$])
]

; try termination by whitespace (and forming)
[
    ("::" = form first [:: <something>])
    (":" = form first [: <something>])
    ("^^" = form first [^ <something>])  ; caret is escape in Rebol strings
    ("&" = form first [& <something>])
    ("@" = form first [@ <something>])
    ("$" = form first [$ <something>])
]

; Try quoted forms (and molding)
[
    ("'::" = mold first ['::])
    ("':" = mold first [':])
    ("'^^" = mold first ['^])  ; caret is escape in Rebol strings
    ("'&" = mold first ['&])
    ("'@" = mold first ['@])
    ("'$" = mold first ['$])
]

; Try TO TEXT! and MATCH
[
    ("::" = to text! match sigil! '::)
    (":" = to text! match sigil! ':)
    ("^^" = to text! match sigil! '^)  ; caret is escape in Rebol strings
    ("&" = to text! match sigil! '&)
    ("@" = to text! match sigil! '@)
    ("$" = to text! match sigil! '$)
]

; Quasiforms of sigil don't exist (and probably should not, as combining the
; tildes with the sigil symbols is considered undesirable, so unless there
; is a really good reason sigils shuldn't have quasi/antiforms)
[
    ~scan-invalid~ !! (transcode "~::~")
    ~scan-invalid~ !! (transcode "~:~")
    ~scan-invalid~ !! (transcode "~^^~")  ; caret is escape in Rebol strings
    ~scan-invalid~ !! (transcode "~&~")
    ~scan-invalid~ !! (transcode "~@~")
    ~scan-invalid~ !! (transcode "~$~")
]


; Tests for SIGIL OF

(did apply :all [
    @[word tu.p.le pa/th [bl o ck] (gr o up)]
    /predicate item -> [null = sigil of item]
])

(did apply :all [
    @[word: tu.p.le: pa/th: [bl o ck]: (gr o up):]
    /predicate item -> [':: = sigil of item]
])

(did apply :all [
    @[:word :tu.p.le :pa/th :[bl o ck] :(gr o up)]
    /predicate item -> [': = sigil of item]
])

(did apply :all [
    @[^word ^tu.p.le ^pa/th ^[bl o ck] ^(gr o up)]
    /predicate item -> ['^ = sigil of item]
])

(did apply :all [
    @[&word &tu.p.le &pa/th &[bl o ck] &(gr o up)]
    /predicate item -> ['& = sigil of item]
])

(did apply :all [
    @[@word @tu.p.le @pa/th @[bl o ck] @(gr o up)]
    /predicate item -> ['@ = sigil of item]
])

(did apply :all [
    @[$word $tu.p.le $pa/th $[bl o ck] $(gr o up)]
    /predicate item -> ['$ = sigil of item]
])


; :: has no meaning in the evaluator yet
[
    ~???~ !! (: <no> <meaning>)
]

; : has no meaning in the evaluator yet
[
    ~???~ !! (:: <no> <meaning>)
]

; ^ is META
[
    ((@ '3) = ^ 1 + 2)
    (null' = ^ null)

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

; @ is THE, with exception that quasiforms will reconstitute antiforms
;
; (this quirk is used in the API, though a better idea for how to achieve the
; goal is being designed)
[
    ('x = @ x)
    ('(a b c) = @ (a b c))
    (''3 = @ '3)

    (null = @ ~null~)  ; reconstitutes antiforms (used by API, revisit)
    (
        assert ['~null~ = ^ x: @ ~null~]
        x = null
    )
    ('~something~ = ^ @ ~something~)

    ~need-non-end~ !! (@)
]
