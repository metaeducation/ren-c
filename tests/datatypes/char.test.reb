; datatypes/char.r
(char? #"a")
(not char? 1)
(issue! = type of #"a")

; !!! Workaround for test scanner's use of TRANSCODE that violates the ability
; to actually work with 0 byte representations in strings (even for a test
; that is character based).
;
(do load #{23225E4022203D2023225E2830302922})  ; ^ @ = ^ (00)
(do load #{23225E286E756C6C2922203D2023225E2830302922})  ; ^ (null) = ^ (00)
((load #{23225E2830302922}) = make char! 0)  ; ^ (00)

(#"^A" = #"^(01)")
(#"^B" = #"^(02)")
(#"^C" = #"^(03)")
(#"^D" = #"^(04)")
(#"^E" = #"^(05)")
(#"^F" = #"^(06)")
(#"^G" = #"^(07)")
(#"^H" = #"^(08)")
(#"^I" = #"^(09)")
(#"^J" = #"^(0A)")
(#"^K" = #"^(0B)")
(#"^L" = #"^(0C)")
(#"^M" = #"^(0D)")
(#"^N" = #"^(0E)")
(#"^O" = #"^(0F)")
(#"^P" = #"^(10)")
(#"^Q" = #"^(11)")
(#"^R" = #"^(12)")
(#"^S" = #"^(13)")
(#"^T" = #"^(14)")
(#"^U" = #"^(15)")
(#"^V" = #"^(16)")
(#"^W" = #"^(17)")
(#"^X" = #"^(18)")
(#"^Y" = #"^(19)")
(#"^Z" = #"^(1A)")
(#"^[" = #"^(1B)")
(#"^\" = #"^(1C)")
(#"^]" = #"^(1D)")
(#"^!" = #"^(1E)")
(#"^_" = #"^(1F)")
(#" " = #"^(20)")
(#"!" = #"^(21)")
(#"^"" = #"^(22)")
(#"#" = #"^(23)")
(#"$" = #"^(24)")
(#"%" = #"^(25)")
(#"&" = #"^(26)")
(#"'" = #"^(27)")
(#"(" = #"^(28)")
(#")" = #"^(29)")
(#"*" = #"^(2A)")
(#"+" = #"^(2B)")
(#"," = #"^(2C)")
(#"-" = #"^(2D)")
(#"." = #"^(2E)")
(#"/" = #"^(2F)")
(#"0" = #"^(30)")
(#"1" = #"^(31)")
(#"2" = #"^(32)")
(#"3" = #"^(33)")
(#"4" = #"^(34)")
(#"5" = #"^(35)")
(#"6" = #"^(36)")
(#"7" = #"^(37)")
(#"8" = #"^(38)")
(#"9" = #"^(39)")
(#":" = #"^(3A)")
(#";" = #"^(3B)")
(#"<" = #"^(3C)")
(#"=" = #"^(3D)")
(#">" = #"^(3E)")
(#"?" = #"^(3F)")
(#"@" = #"^(40)")
(#"A" = #"^(41)")
(#"B" = #"^(42)")
(#"C" = #"^(43)")
(#"D" = #"^(44)")
(#"E" = #"^(45)")
(#"F" = #"^(46)")
(#"G" = #"^(47)")
(#"H" = #"^(48)")
(#"I" = #"^(49)")
(#"J" = #"^(4A)")
(#"K" = #"^(4B)")
(#"L" = #"^(4C)")
(#"M" = #"^(4D)")
(#"N" = #"^(4E)")
(#"O" = #"^(4F)")
(#"P" = #"^(50)")
(#"Q" = #"^(51)")
(#"R" = #"^(52)")
(#"S" = #"^(53)")
(#"T" = #"^(54)")
(#"U" = #"^(55)")
(#"V" = #"^(56)")
(#"W" = #"^(57)")
(#"X" = #"^(58)")
(#"Y" = #"^(59)")
(#"Z" = #"^(5A)")
(#"[" = #"^(5B)")
(#"\" = #"^(5C)")
(#"]" = #"^(5D)")
(#"^^" = #"^(5E)")
(#"_" = #"^(5F)")
(#"`" = #"^(60)")
(#"a" = #"^(61)")
(#"b" = #"^(62)")
(#"c" = #"^(63)")
(#"d" = #"^(64)")
(#"e" = #"^(65)")
(#"f" = #"^(66)")
(#"g" = #"^(67)")
(#"h" = #"^(68)")
(#"i" = #"^(69)")
(#"j" = #"^(6A)")
(#"k" = #"^(6B)")
(#"l" = #"^(6C)")
(#"m" = #"^(6D)")
(#"n" = #"^(6E)")
(#"o" = #"^(6F)")
(#"p" = #"^(70)")
(#"q" = #"^(71)")
(#"r" = #"^(72)")
(#"s" = #"^(73)")
(#"t" = #"^(74)")
(#"u" = #"^(75)")
(#"v" = #"^(76)")
(#"w" = #"^(77)")
(#"x" = #"^(78)")
(#"y" = #"^(79)")
(#"z" = #"^(7A)")
(#"{" = #"^(7B)")
(#"|" = #"^(7C)")
(#"}" = #"^(7D)")
(#"~" = #"^(7E)")
(#"^~" = #"^(7F)")
; alternatives


(#"^(line)" = #"^(0A)")
(#"^/" = #"^(0A)")
(#"^(tab)" = #"^(09)")
(#"^-" = #"^(09)")
(#"^(page)" = #"^(0C)")
(#"^(esc)" = #"^(1B)")
(#"^(back)" = #"^(08)")
(#"^(del)" = #"^(7f)")

; Quotes are removed if not necessary in molding
({#a} = mold #"a")
({#a} = mold #a)

(
    c: make char! 0
    did all [
        char? c
        0 = codepoint of c
    ]
)(
    c: as issue! 0
    did all [
        char? c
        0 = codepoint of c
    ]
)

(char? #"^(ff)")  ; no longer the maximum

(0 = subtract NUL NUL)
(-1 = subtract NUL #"^(01)")
(-255 = subtract NUL #"^(ff)")
(1 = subtract #"^(01)" NUL)
(0 = subtract #"^(01)" #"^(01)")
(-254 = subtract #"^(01)" #"^(ff)")
(255 = subtract #"^(ff)" NUL)
(254 = subtract #"^(ff)" #"^(01)")
(0 = subtract #"^(ff)" #"^(ff)")

(
    e: trap [NUL - 1]
    e/id = 'type-limit
)
(
    e: trap [NUL + -1]
    e/id = 'type-limit
)

(#"Ā" = add #"^(01)" #"^(ff)")
(#"Ā" = add #"^(ff)" #"^(01)")
(#"Ǿ" = add #"^(ff)" #"^(ff)")

(
    random/seed "let's be deterministic"
    codepoints: [
        #"b"  ; 1 utf-8 byte
        #"à"  ; 2 utf-8 bytes encoded
        #"漢"  ; 3 utf-8 bytes encoded
        #"😺"  ; 4 utf-8 bytes encoded
    ]
    count-up size 4 [
        c: codepoints/(size)
        if size != length of to binary! c [
            fail "test character doesn't match expected size"
        ]
        repeat len 64 [
            s: copy {}
            e: copy {}
            picks: copy []
            repeat i len [
                append s random/only codepoints
                append e c
                append picks i
            ]
            random picks  ; randomize positions so not always in order
            for-each i picks [
                comment [
                    print [{Trying} i {/} len {in} mold s]
                ]
                s/(i): c
                if len != length of s [
                    fail ["Length not" len "for" mold s]
                ]
            ]
            if not s = e [fail ["Mismatch:" mold s "=>" mold e]]
        ]
        true
    ]
)


[#1043
    (error? trap [make char! #{}])
    (error? trap [make char! ""])
]


[#1031
    ; 1 UTF-8 byte
    (#"b" = make char! #{62})
    (#{62} = to binary! #"b")

    ; 2 UTF-8 bytes
    (#"à" = make char! #{C3A0})
    (#{C3A0} = to binary! #"à")

    ; 3 UTF-8 bytes
    (#"漢" = make char! #{E6BCA2})
    (#{E6BCA2} = to binary! #"漢")

    ; 4 UTF-8 bytes
    (#"😺" = make char! #{F09F98BA})
    (#{F09F98BA} = to binary! #"😺")
]
