; datatypes/get-path.r
; minimum
; empty get-path test
[#1947
    (get-path? load-value ":[a]/1")
]

; ANY-PATH? are no longer positional
;
;(
;    all [
;        get-path? a: next load-value ":[a b c]/2"
;        2 == index? a
;    ]
;)


; GET-PATH! and GET-WORD! should preserve the name of the function in the
; cell after extraction.
[(
    e: sys.util.rescue [eval compose [(unrun :append) 1 <d>]]
    all [
        e.id = 'expect-arg
        e.arg1 = 'append
    ]
)]
