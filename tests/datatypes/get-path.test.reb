; datatypes/get-path.r
; minimum
; empty get-path test
[#1947
    (get-path? load-value "#[get-path! [[a] 1]]")
]

; ANY-PATH! are no longer positional
;
;(
;    all [
;        get-path? a: load-value "#[get-path! [[a b c] 2]]"
;        2 == index? a
;    ]
;)


; GET-PATH! and GET-WORD! should preserve the name of the function in the
; cell after extraction.
[(
    e: trap [do compose [(:append/only) 1 <d>]]
    did all [
        e.id = 'expect-arg
        e/arg1 = 'append
    ]
)(
    e: trap [do compose [(:append) 1 <d>]]
    did all [
        e.id = 'expect-arg
        e/arg1 = 'append
    ]
)]
