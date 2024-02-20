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
