REBOL []

init: %ext-locale-init.reb
inp: %ISO-639-2_utf-8.txt
count: read inp
if #{EFBBBF} = as blob! copy:part count 3 [  ; UTF-8 BOM
    count: skip count 3
]

iso-639-table: make map! 1024

lower: charset [#"a" - #"z"]
letter: charset [#"a" - #"z" #"A" - #"Z"]

parse3 cnt [
    some [
        ;initialization
        (code-2: name: null)

        ; 3-letter code
        ;
        to "|"

        ; "terminological code"
        ; https://en.wikipedia.org/wiki/ISO_639-2#B_and_T_codes
        ;
        "|" opt [repeat 3 lower]

        ; 2-letter code
        ;
        "|" opt [
            code-2: across repeat 2 lower
        ]

        ; Language name in English
        ;
        "|" name: across to "|" (
            if code-2 [
                append iso-639-table spread compose [
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
] except [
    fail "Failed to update iso-639-table"
]

write init init-code
