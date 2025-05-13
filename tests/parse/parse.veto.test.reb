; %parse-veto.test.reb
;
; This is an implementation of what historical Redbol calls FAIL as arity-0.
; UPARSE's FAIL is arity-2
;
; It skips to the next alternate rule.

; BLOCK!
[
    ~parse-mismatch~ !! (parse [] [veto])
    ~parse-mismatch~ !! (parse [a] ['a veto])
    ~parse-mismatch~ !! (parse [a] [[veto]])
    ~parse-mismatch~ !! (parse [a] [veto | veto])
    ~parse-mismatch~ !! (parse [a] [[veto | veto]])
    ~parse-mismatch~ !! (parse [a] ['b | veto])
]

; TEXT!
[
    ~parse-mismatch~ !! (parse "" [veto])
    ~parse-mismatch~ !! (parse "a" [#a veto])
    ~parse-mismatch~ !! (parse "a" [[veto]])
    ~parse-mismatch~ !! (parse "a" [veto | veto])
    ~parse-mismatch~ !! (parse "a" [[veto | veto]])
    ~parse-mismatch~ !! (parse "a" [#b | veto])
]

; BLOB!
[
    ~parse-mismatch~ !! (parse #{} [veto])
    ~parse-mismatch~ !! (parse #{0A} [#{0A} veto])
    ~parse-mismatch~ !! (parse #{0A} [[veto]])
    ~parse-mismatch~ !! (parse #{0A} [veto | veto])
    ~parse-mismatch~ !! (parse #{0A} [[veto | veto]])
    ~parse-mismatch~ !! (parse #{0A} [#{0B} | veto])
]
