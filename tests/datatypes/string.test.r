; datatypes/string.r
(text? "ahoj")
(not text? 1)
(text! = type of "ahoj")
; minimum
(text? "")
; alternative literal form
("" = to text! "")
("" = make text! 0)

; !!! The test system uses TRANSCODE to get past strings with illegal content.
; That runs into trouble when trying to literally depict strings which should
; not be able to load.  Use BLOB! to depict.
[
    ~illegal-zero-byte~ !! (
        transcode #{225E4022}  ; byte sequence for ^^@ in quotes
    )
    ~illegal-zero-byte~ !! (
        transcode #{225E2830302922}  ; byte sequence for ^^(00) in quotes
    )
]

("^A" = "^(01)")
("^B" = "^(02)")
("^C" = "^(03)")
("^D" = "^(04)")
("^E" = "^(05)")
("^F" = "^(06)")
("^G" = "^(07)")
("^H" = "^(08)")
("^I" = "^(09)")
("^J" = "^(0A)")
("^K" = "^(0B)")
("^L" = "^(0C)")
("^M" = "^(0D)")
("^N" = "^(0E)")
("^O" = "^(0F)")
("^P" = "^(10)")
("^Q" = "^(11)")
("^R" = "^(12)")
("^S" = "^(13)")
("^T" = "^(14)")
("^U" = "^(15)")
("^V" = "^(16)")
("^W" = "^(17)")
("^X" = "^(18)")
("^Y" = "^(19)")
("^Z" = "^(1A)")
("^[" = "^(1B)")
("^\" = "^(1C)")
("^]" = "^(1D)")
("^!" = "^(1E)")
("^_" = "^(1F)")
(" " = "^(20)")
("!" = "^(21)")
("^"" = "^(22)")
("#" = "^(23)")
("$" = "^(24)")
("%" = "^(25)")
("&" = "^(26)")
("'" = "^(27)")
("(" = "^(28)")
(")" = "^(29)")
("*" = "^(2A)")
("+" = "^(2B)")
("," = "^(2C)")
("-" = "^(2D)")
("." = "^(2E)")
("/" = "^(2F)")
("0" = "^(30)")
("1" = "^(31)")
("2" = "^(32)")
("3" = "^(33)")
("4" = "^(34)")
("5" = "^(35)")
("6" = "^(36)")
("7" = "^(37)")
("8" = "^(38)")
("9" = "^(39)")
(":" = "^(3A)")
(";" = "^(3B)")
("<" = "^(3C)")
("=" = "^(3D)")
(">" = "^(3E)")
("?" = "^(3F)")
("@" = "^(40)")
("A" = "^(41)")
("B" = "^(42)")
("C" = "^(43)")
("D" = "^(44)")
("E" = "^(45)")
("F" = "^(46)")
("G" = "^(47)")
("H" = "^(48)")
("I" = "^(49)")
("J" = "^(4A)")
("K" = "^(4B)")
("L" = "^(4C)")
("M" = "^(4D)")
("N" = "^(4E)")
("O" = "^(4F)")
("P" = "^(50)")
("Q" = "^(51)")
("R" = "^(52)")
("S" = "^(53)")
("T" = "^(54)")
("U" = "^(55)")
("V" = "^(56)")
("W" = "^(57)")
("X" = "^(58)")
("Y" = "^(59)")
("Z" = "^(5A)")
("[" = "^(5B)")
("\" = "^(5C)")
("]" = "^(5D)")
("^^" = "^(5E)")
("_" = "^(5F)")
("`" = "^(60)")
("a" = "^(61)")
("b" = "^(62)")
("c" = "^(63)")
("d" = "^(64)")
("e" = "^(65)")
("f" = "^(66)")
("g" = "^(67)")
("h" = "^(68)")
("i" = "^(69)")
("j" = "^(6A)")
("k" = "^(6B)")
("l" = "^(6C)")
("m" = "^(6D)")
("n" = "^(6E)")
("o" = "^(6F)")
("p" = "^(70)")
("q" = "^(71)")
("r" = "^(72)")
("s" = "^(73)")
("t" = "^(74)")
("u" = "^(75)")
("v" = "^(76)")
("w" = "^(77)")
("x" = "^(78)")
("y" = "^(79)")
("z" = "^(7A)")
("{" = "^(7B)")
("|" = "^(7C)")
("}" = "^(7D)")
("~" = "^(7E)")
("^~" = "^(7F)")
("^(line)" = "^(0A)")
("^/" = "^(0A)")
("^(tab)" = "^(09)")
("^-" = "^(09)")
("^(page)" = "^(0C)")
("^(esc)" = "^(1B)")
("^(back)" = "^(08)")
("^(del)" = "^(7f)")
("ahoj" = to text! "ahoj")
("1" = to text! 1)
(-[""]- = mold "")


[#854 (
    a: <0>
    b: make tag! 0
    insert b first a
    a = b
)]


[#207
    ~bad-cast~ !! (as rune! 0)
]

[#2280 (  ; Byte-Order-Mark ("BOM") deprecated in UTF-8, don't hide it
    t: to text! #{EFBBBFC3A4C3B6C3BC}
    all [
        t = "^(FEFF)äöü"
        4 = length of t
    ]
)]

[
    ~illegal-zero-byte~ !! (
        str: "abc"
        str.2: 0
    )
    ~illegal-zero-byte~ !! (
        str: "abc"
        str.2: make-char 0
    )
]

; === NEW REN-C STRING MODE ===
;
; Dashes are used to build braces into asymmetric delimiters.

("a {b} c" = -[a {b} c]-)
("a {b c" = -[a {b c]-)
("a ---[b]--- c" = -[a ---[b]--- c]-)


; === RANDOM STRING WALK ===

; This is a poor but better-than-nothing quick test that just does random
; string edits to see if anything crashes or asserts.  It's not checked
; against a known good string library, so it relies on things like the
; assertions in DEBUG_UTF8_EVERYWHERE that do integrity checks to be of
; much use.
(
    randomize "Deterministic!"

    random-string: func [n <local> len str] [
        str: copy ""
        len: random n
        repeat len [
            either 1 = random 8 [
                append str insist [try make rune! random 1114111]
            ][
                append str make rune! 64 + random 64
            ]
        ]
        return str
    ]

    test: ""
    halftime: lambda [block] [if 2 = random 2 (block)]

    repeat 1000 [
        if 1 = random 80 [
            clear test
        ]
        switch random-pick [append insert change remove] [
            'append [
                append // [
                    test
                    random-string 30
                    part: halftime [random 30]
                    dup: halftime [random 3]
                ]
            ]
            'insert [
                insert // [
                    skip test (random 1 + length of test) - 1
                    random-string 30
                    part: halftime [random 30]
                    dup: halftime [random 3]
                ]
            ]
            'change [
                change // [
                    skip test (random 1 + length of test) - 1
                    random-string 30
                    part: halftime [random 30]
                    dup: halftime [random 3]
                ]
            ]
            'remove [
                remove // [
                    skip test (random 1 + length of test) - 1
                    part: halftime [random 30]
                ]
            ]
        ]
        if not empty? test [
            assert [find test last test]  ; random find just to iterate it
        ]
    ]
)
