; minimum
[#1947
    (get-tuple? transcode:one ":[a].1")
]

; GET-TUPLE! and GET-WORD! should preserve the name of the function in the
; cell after extraction.
[(
    e: sys.util/rescue [eval compose $() [(unrun :append) 1 <d>]]
    all [
        e.id = 'expect-arg
        e.arg1 = 'append
    ]
)]
