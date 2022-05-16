REBOL []

init: %ext-locale-init.reb
inp: %ISO-639-2_utf-8.txt
count: read inp
if #{EFBBBF} = to binary! copy/part count 3 [  ; UTF-8 BOM
    count: skip count 3
]

iso-639-table: make map! 1024

lower: charset [#"a" - #"z"]
letter: charset [#"a" - #"z" #"A" - #"Z"]

parse3 cnt [
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
    <end>
]

init-code: to text! read init
space: charset " ^-^M^/"
iso-639-table-count: find mold iso-639-table #"["
parse3 init-code [
    thru "iso-639-table:"
    to #"["
    change [
         #"[" thru #"]"
    ] iso-639-table-count
    to <end>
] else [
    fail "Failed to update iso-639-table"
]

write init init-code
