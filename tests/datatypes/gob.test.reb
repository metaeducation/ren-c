; datatypes/gob.r
; minimum
(gob? make gob! [])
(gob! = type of make gob! [])
[#62 (
    g: make gob! []
    1x1 == g/offset: 1x1
)]
[#1969 (
    g1: make gob! []
    g2: make gob! []
    insert g1 g2
    same? g1 g2/parent
    do "g1: _"
    do "recycle"
    g3: make gob! []
    insert g2/parent g3
    true
)]
(
    main: make gob! []
    for-each i [31 325 1] [
        clear main
        recycle
        loop i [
            append main make gob! []
        ]
    ]
    true
)
