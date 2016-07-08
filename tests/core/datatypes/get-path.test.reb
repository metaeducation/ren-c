; datatypes/get-path.r
; minimum
; bug#1947
; empty get-path test
[get-path? load "#[get-path! [[a] 1]]"]
[
    all [
        get-path? a: load "#[get-path! [[a b c] 2]]"
        2 == index? a
    ]
]
