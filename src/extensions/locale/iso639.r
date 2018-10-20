REBOL []

init: %ext-locale-init.reb
inp: %ISO-639-2_utf-8.txt
cnt: read inp
if #{EFBBBF} == to binary! copy/part cnt 3 [ ;UTF8 BOM
    cnt: skip cnt 3
]

;cnt: to text! cnt
;print ["string cnt BOM:" mold copy/part cnt 3]

iso-639-table: make map! 1024

lower: charset [#"a" - #"z"]
letter: charset [#"a" - #"z" #"A" - #"Z"]

parse cnt [
    some [
        ;initialization
        (code-2: name: _)

        ; 3-letter code
        ;
        to "|"

        ; "terminological code"
        ; https://en.wikipedia.org/wiki/ISO_639-2#B_and_T_codes
        ;
        "|" opt [3 lower]

        ; 2-letter code
        ;
        "|" opt [
            copy code-2 2 lower
        ]

        ; Language name in English
        ;
        "|" copy name to "|" (
            if code-2 [
                append iso-639-table compose [
                    (to text! code-2) (to text! name)
                ]
            ]
        )

        ; Language name in French
        ;
        "|" to "^/"

        ["^/" | "^M"]
    ]
]

init-code: to text! read init
space: charset " ^-^M^/"
iso-639-table-cnt: find mold iso-639-table #"["
parse init-code [
    thru "iso-639-table:"
    to #"["
    change [
         #"[" thru #"]"
    ] iso-639-table-cnt
    to end
] or [
    fail "Failed to update iso-639-table"
]

write init init-code
